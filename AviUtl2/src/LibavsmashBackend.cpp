#include "au2/ReaderBackend.hpp"

#include "au2/AudioOutputAdapter.hpp"
#include "au2/VideoOutputAdapter.hpp"

#include "../../common/libavsmash.h"
#include "../../common/libavsmash_audio.h"
#include "../../common/libavsmash_video.h"
#include "../../common/utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <lsmash.h>
}

#include <cstring>

namespace au2 {
namespace {

struct LibavsmashVideoInfo {
    uint32_t media_timescale = 0;
    uint64_t skip_duration = 0;
    int64_t start_pts = 0;
};

struct LibavsmashAudioInfo {
    uint32_t media_timescale = 0;
    int64_t start_pts = 0;
};

struct LibavsmashHandler {
    int log_type = 0;
    lsmash_root_t* root = nullptr;
    lsmash_file_parameters_t file_param {};
    lsmash_movie_parameters_t movie_param {};
    uint32_t number_of_tracks = 0;
    AVFormatContext* format_ctx = nullptr;
    int threads = 0;
    LibavsmashVideoInfo vih;
    int libavsmash_video_media_index = 0;
    libavsmash_video_decode_handler_t* vdhp = nullptr;
    libavsmash_video_output_handler_t* vohp = nullptr;
    LibavsmashAudioInfo aih;
    int libavsmash_audio_media_index = 0;
    libavsmash_audio_decode_handler_t* adhp = nullptr;
    libavsmash_audio_output_handler_t* aohp = nullptr;
    int64_t av_gap = 0;
    int av_sync = 1;
};

void free_handler(LibavsmashHandler** hpp)
{
    if (!hpp || !*hpp) {
        return;
    }
    LibavsmashHandler* hp = *hpp;
    libavsmash_video_free_decode_handler(hp->vdhp);
    libavsmash_video_free_output_handler(hp->vohp);
    libavsmash_audio_free_decode_handler(hp->adhp);
    libavsmash_audio_free_output_handler(hp->aohp);
    delete hp;
    *hpp = nullptr;
}

LibavsmashHandler* alloc_handler()
{
    auto* hp = new LibavsmashHandler();
    if (!(hp->vdhp = libavsmash_video_alloc_decode_handler()) || !(hp->vohp = libavsmash_video_alloc_output_handler())
        || !(hp->adhp = libavsmash_audio_alloc_decode_handler()) || !(hp->aohp = libavsmash_audio_alloc_output_handler())) {
        free_handler(&hp);
        return nullptr;
    }
    return hp;
}

int get_track_of_type(SessionCore* session, uint32_t type, int track_number)
{
    int ret = -1;
    lw_log_handler_t* lhp = nullptr;
    if (type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK) {
        auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
        lhp = libavsmash_video_get_log_handler(hp->vdhp);
        libavsmash_video_set_root(hp->vdhp, hp->root);
        ret = libavsmash_video_get_track(hp->vdhp, static_cast<uint32_t>(track_number < 0 ? 0 : track_number));
    } else {
        auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
        lhp = libavsmash_audio_get_log_handler(hp->adhp);
        libavsmash_audio_set_root(hp->adhp, hp->root);
        ret = libavsmash_audio_get_track(hp->adhp, static_cast<uint32_t>(track_number < 0 ? 0 : track_number));
    }
    if (lhp) {
        lhp->level = LW_LOG_WARNING;
    }
    return ret;
}

int get_track_number_by_media_index(lsmash_root_t* root, uint32_t type, int media_index)
{
    if (media_index <= 0) {
        return 0;
    }

    lsmash_movie_parameters_t movie_param;
    if (lsmash_get_movie_parameters(root, &movie_param) < 0) {
        return -1;
    }

    int current_index = 0;
    for (uint32_t track_number = 1; track_number <= movie_param.number_of_tracks; ++track_number) {
        const uint32_t track_id = lsmash_get_track_ID(root, track_number);
        if (track_id == 0) {
            continue;
        }
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters(&media_param);
        if (lsmash_get_media_parameters(root, track_id, &media_param) < 0 || media_param.handler_type != type) {
            continue;
        }
        if (current_index == media_index) {
            return static_cast<int>(track_number);
        }
        ++current_index;
    }
    return -1;
}

int get_ctd_shift(lsmash_root_t* root, uint32_t track_id, uint32_t* ctd_shift)
{
    return lsmash_get_composition_to_decode_shift_from_media_timeline(root, track_id, ctd_shift) ? -1 : 0;
}

uint64_t get_empty_duration(lsmash_root_t* root, uint32_t track_id, uint32_t movie_timescale, uint32_t media_timescale)
{
    lsmash_edit_t edit;
    if (lsmash_get_explicit_timeline_map(root, track_id, 1, &edit)) {
        return 0;
    }
    if (edit.duration && edit.start_time == ISOM_EDIT_MODE_EMPTY) {
        return av_rescale_q(edit.duration, AVRational { 1, static_cast<int>(movie_timescale) }, AVRational { 1, static_cast<int>(media_timescale) });
    }
    return 0;
}

int64_t get_start_time(lsmash_root_t* root, uint32_t track_id)
{
    const uint32_t edit_count = lsmash_count_explicit_timeline_map(root, track_id);
    for (uint32_t edit_number = 1; edit_number <= edit_count; ++edit_number) {
        lsmash_edit_t edit;
        if (lsmash_get_explicit_timeline_map(root, track_id, edit_number, &edit)) {
            return 0;
        }
        if (edit.duration == 0) {
            return 0;
        }
        if (edit.start_time >= 0) {
            return edit.start_time;
        }
    }
    return 0;
}

int prepare_video_decoding(SessionCore* session, VideoOptions* opt)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    if (libavsmash_video_initialize_decoder_configuration(vdhp, hp->format_ctx, hp->threads) < 0) {
        return -1;
    }
    AVCodecContext* ctx = libavsmash_video_get_codec_context(vdhp);
    if (!ctx) {
        return 0;
    }
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    const int max_width = libavsmash_video_get_max_width(vdhp);
    const int max_height = libavsmash_video_get_max_height(vdhp);
    if (setup_video_rendering(vohp, opt, &session->video_format, max_width, max_height, ctx->pix_fmt) < 0) {
        return -1;
    }
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    libavsmash_video_setup_timestamp_info(vdhp, vohp, &fps_num, &fps_den);
    if (libavsmash_video_get_error(vdhp)) {
        return -1;
    }
    if (libavsmash_video_create_keyframe_list(vdhp) < 0) {
        return -1;
    }
    lw_log_handler_t* lhp = libavsmash_video_get_log_handler(vdhp);
    lhp->level = LW_LOG_FATAL;
    if (libavsmash_video_find_first_valid_frame(vdhp) < 0) {
        return -1;
    }
    const uint32_t media_timescale = libavsmash_video_get_media_timescale(vdhp);
    if (hp->av_sync) {
        const uint32_t track_id = libavsmash_video_get_track_id(vdhp);
        uint32_t ctd_shift = 0;
        if (get_ctd_shift(hp->root, track_id, &ctd_shift) < 0) {
            return -1;
        }
        const uint64_t min_cts = libavsmash_video_get_min_cts(vdhp);
        const uint32_t movie_timescale = hp->movie_param.timescale;
        hp->vih.start_pts = min_cts + ctd_shift + get_empty_duration(hp->root, track_id, movie_timescale, media_timescale);
        hp->vih.skip_duration = ctd_shift + get_start_time(hp->root, track_id);
    }
    hp->vih.media_timescale = media_timescale;
    session->framerate_num = static_cast<int>(fps_num);
    session->framerate_den = static_cast<int>(fps_den);
    session->video_sample_count = vohp->frame_count;
    libavsmash_video_force_seek(vdhp);
    return 0;
}

int prepare_audio_decoding(SessionCore* session, AudioOptions* opt)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
    libavsmash_audio_decode_handler_t* adhp = hp->adhp;
    if (libavsmash_audio_initialize_decoder_configuration(adhp, hp->format_ctx, hp->threads) < 0) {
        return -1;
    }
    AVCodecContext* ctx = libavsmash_audio_get_codec_context(adhp);
    if (!ctx) {
        return 0;
    }
    libavsmash_audio_output_handler_t* aohp = hp->aohp;
    av_channel_layout_uninit(&aohp->output_channel_layout);
    av_channel_layout_from_mask(&aohp->output_channel_layout, libavsmash_audio_get_best_used_channel_layout(adhp));
    aohp->output_sample_format = libavsmash_audio_get_best_used_sample_format(adhp);
    aohp->output_sample_rate = libavsmash_audio_get_best_used_sample_rate(adhp);
    aohp->output_bits_per_sample = libavsmash_audio_get_best_used_bits_per_sample(adhp);
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(adhp);
    lhp->level = LW_LOG_FATAL;
    if (setup_audio_rendering(aohp, ctx, opt, &session->audio_format.Format) < 0) {
        return -1;
    }
    const uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
    uint64_t start_time = 0;
    if (hp->av_sync) {
        const uint64_t min_cts = libavsmash_audio_get_min_cts(adhp);
        if (min_cts == UINT64_MAX) {
            return -1;
        }
        const uint32_t track_id = libavsmash_audio_get_track_id(adhp);
        uint32_t ctd_shift = 0;
        if (get_ctd_shift(hp->root, track_id, &ctd_shift) < 0) {
            return -1;
        }
        const uint32_t movie_timescale = hp->movie_param.timescale;
        hp->aih.start_pts = min_cts + ctd_shift + get_empty_duration(hp->root, track_id, movie_timescale, media_timescale);
        start_time = ctd_shift + get_start_time(hp->root, track_id);
    }
    hp->aih.media_timescale = media_timescale;
    session->audio_pcm_sample_count = static_cast<uint32_t>(libavsmash_audio_count_overall_pcm_samples(adhp, aohp->output_sample_rate, start_time));
    if (session->audio_pcm_sample_count == 0) {
        return -1;
    }
    if (hp->av_sync && libavsmash_video_get_track_id(hp->vdhp)) {
        AVRational audio_sample_base { 1, aohp->output_sample_rate };
        hp->av_gap = av_rescale_q(hp->aih.start_pts, AVRational { 1, static_cast<int>(hp->aih.media_timescale) }, audio_sample_base)
            - av_rescale_q(hp->vih.start_pts - hp->vih.skip_duration, AVRational { 1, static_cast<int>(hp->vih.media_timescale) }, audio_sample_base);
        session->audio_pcm_sample_count += hp->av_gap;
        hp->aohp->skip_decoded_samples = av_rescale(start_time, aohp->output_sample_rate, media_timescale);
        libavsmash_audio_apply_delay(adhp, hp->av_gap);
    }
    libavsmash_audio_force_seek(adhp);
    return 0;
}

void* open_file(char* file_name, ReaderOptions* opt)
{
    LibavsmashHandler* hp = alloc_handler();
    if (!hp) {
        return nullptr;
    }
    hp->log_type = 0;
    lw_log_handler_t* vlhp = libavsmash_video_get_log_handler(hp->vdhp);
    lw_log_handler_t* alhp = libavsmash_audio_get_log_handler(hp->adhp);
    vlhp->priv = &hp->log_type;
    vlhp->level = LW_LOG_QUIET;
    vlhp->show_log = au2_log_noop;
    *alhp = *vlhp;
    hp->root = libavsmash_open_file(&hp->format_ctx, file_name, &hp->file_param, &hp->movie_param, vlhp);
    if (!hp->root) {
        free_handler(&hp);
        return nullptr;
    }
    hp->number_of_tracks = hp->movie_param.number_of_tracks;
    hp->threads = opt->threads;
    hp->av_sync = opt->av_sync;
    hp->libavsmash_video_media_index = opt->libavsmash_video_media_index;
    hp->libavsmash_audio_media_index = opt->libavsmash_audio_media_index;
    libavsmash_video_set_preferred_decoder_names(hp->vdhp, opt->preferred_decoder_names);
    libavsmash_audio_set_preferred_decoder_names(hp->adhp, opt->preferred_decoder_names);
    *alhp = *vlhp;
    return hp;
}

int get_first_video_track(SessionCore* session, VideoOptions* opt)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
    const int track_number = get_track_number_by_media_index(hp->root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK, hp->libavsmash_video_media_index);
    if (track_number < 0) {
        return -1;
    }
    if (get_track_of_type(session, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK, track_number) != 0) {
        const uint32_t track_id = libavsmash_video_get_track_id(hp->vdhp);
        lsmash_destruct_timeline(hp->root, track_id);
        libavsmash_video_close_codec_context(hp->vdhp);
        return -1;
    }
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    libavsmash_video_set_seek_mode(vdhp, opt->seek_mode);
    libavsmash_video_set_forward_seek_threshold(vdhp, opt->forward_seek_threshold);
    vohp->vfr2cfr = opt->vfr2cfr.active;
    vohp->cfr_num = opt->vfr2cfr.framerate_num;
    vohp->cfr_den = opt->vfr2cfr.framerate_den;
    return prepare_video_decoding(session, opt);
}

int get_first_audio_track(SessionCore* session, AudioOptions* opt)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
    const int track_number = get_track_number_by_media_index(hp->root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK, hp->libavsmash_audio_media_index);
    if (track_number < 0) {
        return -1;
    }
    if (get_track_of_type(session, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK, track_number) != 0) {
        const uint32_t track_id = libavsmash_audio_get_track_id(hp->adhp);
        lsmash_destruct_timeline(hp->root, track_id);
        libavsmash_audio_close_codec_context(hp->adhp);
        return -1;
    }
    return prepare_audio_decoding(session, opt);
}

void destroy_disposable(void* private_stuff)
{
    auto* hp = static_cast<LibavsmashHandler*>(private_stuff);
    lsmash_discard_boxes(hp->root);
}

int read_video(SessionCore* session, int sample_number, void* buf)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    if (libavsmash_video_get_error(vdhp)) {
        return 0;
    }
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    ++sample_number;
    if (sample_number == 1) {
        auto* out = static_cast<VideoOutputHandler*>(vohp->private_handler);
        std::memcpy(buf, out->back_ground, out->output_frame_size);
    }
    const int ret = libavsmash_video_get_frame(vdhp, vohp, sample_number);
    if (ret != 0 && !(ret == 1 && sample_number == 1)) {
        return 0;
    }
    AVFrame* av_frame = libavsmash_video_get_frame_buffer(vdhp);
    return convert_colorspace(vohp, av_frame, static_cast<uint8_t*>(buf));
}

int read_audio(SessionCore* session, int start, int wanted_length, void* buf)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
    return static_cast<int>(libavsmash_audio_get_pcm_samples(hp->adhp, hp->aohp, buf, start, wanted_length));
}

int is_keyframe(SessionCore* session, int sample_number)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
    return libavsmash_video_is_keyframe(hp->vdhp, hp->vohp, sample_number + 1);
}

int delay_audio(SessionCore* session, int* start, int wanted_length, int audio_delay)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
    const int end = *start + wanted_length;
    audio_delay += static_cast<int>(hp->av_gap);
    if (*start < audio_delay && end <= audio_delay) {
        libavsmash_audio_force_seek(hp->adhp);
        return 0;
    }
    *start -= audio_delay;
    return 1;
}

void video_cleanup(SessionCore* session)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->video_private);
    if (!hp) {
        return;
    }
    libavsmash_video_free_decode_handler_ptr(&hp->vdhp);
    libavsmash_video_free_output_handler_ptr(&hp->vohp);
}

void audio_cleanup(SessionCore* session)
{
    auto* hp = static_cast<LibavsmashHandler*>(session->audio_private);
    if (!hp) {
        return;
    }
    libavsmash_audio_free_decode_handler_ptr(&hp->adhp);
    libavsmash_audio_free_output_handler_ptr(&hp->aohp);
}

void close_file(void* private_stuff)
{
    auto* hp = static_cast<LibavsmashHandler*>(private_stuff);
    if (!hp) {
        return;
    }
    avformat_close_input(&hp->format_ctx);
    lsmash_close_file(&hp->file_param);
    lsmash_destroy_root(hp->root);
    delete hp;
}

const ReaderCallbacks callbacks {
    ReaderType::Libavsmash,
    open_file,
    get_first_video_track,
    get_first_audio_track,
    destroy_disposable,
    read_video,
    read_audio,
    is_keyframe,
    delay_audio,
    video_cleanup,
    audio_cleanup,
    close_file,
};

} // namespace

const ReaderCallbacks& libavsmash_reader() noexcept
{
    return callbacks;
}

} // namespace au2
