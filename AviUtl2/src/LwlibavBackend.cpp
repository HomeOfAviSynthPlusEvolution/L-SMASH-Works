#include "au2/ReaderBackend.hpp"

#include "au2/AudioOutputAdapter.hpp"
#include "au2/VideoOutputAdapter.hpp"

#include "../../common/lwindex.h"
#include "../../common/lwlibav_audio.h"
#include "../../common/lwlibav_dec.h"
#include "../../common/lwlibav_video.h"
#include "../../common/progress.h"
#include "../../common/utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstring>

namespace au2 {
namespace {

struct LwlibavHandler {
    int log_type = 0;
    lwlibav_file_handler_t lwh {};
    lwlibav_video_decode_handler_t* vdhp = nullptr;
    lwlibav_video_output_handler_t* vohp = nullptr;
    lwlibav_audio_decode_handler_t* adhp = nullptr;
    lwlibav_audio_output_handler_t* aohp = nullptr;
};

void free_handler(LwlibavHandler** hpp)
{
    if (!hpp || !*hpp) {
        return;
    }
    LwlibavHandler* hp = *hpp;
    lwlibav_video_free_decode_handler(hp->vdhp);
    lwlibav_video_free_output_handler(hp->vohp);
    lwlibav_audio_free_decode_handler(hp->adhp);
    lwlibav_audio_free_output_handler(hp->aohp);
    delete hp;
    *hpp = nullptr;
}

LwlibavHandler* alloc_handler()
{
    auto* hp = new LwlibavHandler();
    if (!(hp->vdhp = lwlibav_video_alloc_decode_handler()) || !(hp->vohp = lwlibav_video_alloc_output_handler())
        || !(hp->adhp = lwlibav_audio_alloc_decode_handler()) || !(hp->aohp = lwlibav_audio_alloc_output_handler())) {
        free_handler(&hp);
        return nullptr;
    }
    return hp;
}

void open_indicator(progress_handler_t*) {}
int update_indicator(progress_handler_t*, const char*, int) { return 0; }
void close_indicator(progress_handler_t*) {}

int prepare_video_decoding(SessionCore* session, VideoOptions* opt)
{
    auto* hp = static_cast<LwlibavHandler*>(session->video_private);
    lwlibav_video_decode_handler_t* vdhp = hp->vdhp;
    AVCodecContext* ctx = lwlibav_video_get_codec_context(vdhp);
    if (!ctx) {
        return 0;
    }
    lwlibav_video_set_seek_mode(vdhp, opt->seek_mode);
    lwlibav_video_set_forward_seek_threshold(vdhp, opt->forward_seek_threshold);
    lwlibav_video_output_handler_t* vohp = hp->vohp;
    if (lwlibav_import_av_index_entry(reinterpret_cast<lwlibav_decode_handler_t*>(vdhp)) < 0) {
        return -1;
    }
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    lwlibav_video_setup_timestamp_info(&hp->lwh, vdhp, vohp, &fps_num, &fps_den, opt->apply_repeat_flag);
    session->framerate_num = static_cast<int>(fps_num);
    session->framerate_den = static_cast<int>(fps_den);
    session->video_sample_count = vohp->frame_count;
    lwlibav_video_set_initial_input_format(vdhp);
    const int max_width = lwlibav_video_get_max_width(vdhp);
    const int max_height = lwlibav_video_get_max_height(vdhp);
    if (setup_video_rendering(vohp, opt, &session->video_format, max_width, max_height, ctx->pix_fmt) < 0) {
        return -1;
    }
    lw_log_handler_t* lhp = lwlibav_video_get_log_handler(vdhp);
    lhp->level = LW_LOG_FATAL;
    if (lwlibav_video_find_first_valid_frame(vdhp) < 0) {
        return -1;
    }
    lwlibav_video_force_seek(vdhp);
    return 0;
}

int prepare_audio_decoding(SessionCore* session, AudioOptions* opt)
{
    auto* hp = static_cast<LwlibavHandler*>(session->audio_private);
    lwlibav_audio_decode_handler_t* adhp = hp->adhp;
    AVCodecContext* ctx = lwlibav_audio_get_codec_context(adhp);
    if (!ctx) {
        return 0;
    }
    if (lwlibav_import_av_index_entry(reinterpret_cast<lwlibav_decode_handler_t*>(adhp)) < 0) {
        return -1;
    }
    lw_log_handler_t* lhp = lwlibav_audio_get_log_handler(adhp);
    lhp->level = LW_LOG_FATAL;
    lwlibav_audio_output_handler_t* aohp = hp->aohp;
    if (setup_audio_rendering(aohp, ctx, opt, &session->audio_format.Format) < 0) {
        return -1;
    }
    session->audio_pcm_sample_count = static_cast<uint32_t>(lwlibav_audio_count_overall_pcm_samples(adhp, aohp->output_sample_rate));
    if (session->audio_pcm_sample_count == 0) {
        return -1;
    }
    if (hp->lwh.av_gap && aohp->output_sample_rate != ctx->sample_rate) {
        hp->lwh.av_gap = (static_cast<int64_t>(hp->lwh.av_gap) * aohp->output_sample_rate - 1) / ctx->sample_rate + 1;
    }
    session->audio_pcm_sample_count += hp->lwh.av_gap;
    lwlibav_audio_force_seek(adhp);
    return 0;
}

void* open_file(char* file_path, ReaderOptions* opt)
{
    LwlibavHandler* hp = alloc_handler();
    if (!hp) {
        return nullptr;
    }
    lw_log_handler_t* vlhp = lwlibav_video_get_log_handler(hp->vdhp);
    lw_log_handler_t* alhp = lwlibav_audio_get_log_handler(hp->adhp);
    vlhp->level = LW_LOG_FATAL;
    vlhp->priv = &hp->log_type;
    vlhp->show_log = au2_log_noop;
    *alhp = *vlhp;

    lwlibav_option_t lwlibav_opt {};
    lwlibav_opt.file_path = file_path;
    lwlibav_opt.cache_dir = nullptr;
    lwlibav_opt.threads = opt->threads;
    lwlibav_opt.av_sync = opt->av_sync;
    lwlibav_opt.no_create_index = opt->no_create_index;
    lwlibav_opt.index_file_path = nullptr;
    lwlibav_opt.force_video = opt->force_video;
    lwlibav_opt.force_video_index = opt->force_video_index;
    lwlibav_opt.force_audio = opt->force_audio;
    lwlibav_opt.force_audio_index = opt->force_audio_index;
    lwlibav_opt.apply_repeat_flag = opt->video.apply_repeat_flag;
    lwlibav_opt.field_dominance = opt->video.field_dominance;
    lwlibav_opt.vfr2cfr.active = opt->video.vfr2cfr.active;
    lwlibav_opt.vfr2cfr.fps_num = opt->video.vfr2cfr.framerate_num;
    lwlibav_opt.vfr2cfr.fps_den = opt->video.vfr2cfr.framerate_den;
    lwlibav_video_set_preferred_decoder_names(hp->vdhp, opt->preferred_decoder_names);
    lwlibav_audio_set_preferred_decoder_names(hp->adhp, opt->preferred_decoder_names);

    progress_indicator_t indicator {};
    indicator.open = open_indicator;
    indicator.update = update_indicator;
    indicator.close = close_indicator;
    progress_handler_t* progress_handler = nullptr;
    if (lwlibav_construct_index(&hp->lwh, hp->vdhp, hp->vohp, hp->adhp, hp->aohp, vlhp, &lwlibav_opt, &indicator, progress_handler) < 0) {
        free_handler(&hp);
        return nullptr;
    }
    return hp;
}

int get_video_track(SessionCore* session, VideoOptions* opt)
{
    auto* hp = static_cast<LwlibavHandler*>(session->video_private);
    if (lwlibav_video_get_desired_track(hp->lwh.file_path, hp->vdhp, hp->lwh.threads) < 0) {
        return -1;
    }
    lw_log_handler_t* lhp = lwlibav_video_get_log_handler(hp->vdhp);
    lhp->level = LW_LOG_WARNING;
    lhp->priv = &hp->log_type;
    lhp->show_log = au2_log_noop;
    return prepare_video_decoding(session, opt);
}

int get_audio_track(SessionCore* session, AudioOptions* opt)
{
    auto* hp = static_cast<LwlibavHandler*>(session->audio_private);
    if (lwlibav_audio_get_desired_track(hp->lwh.file_path, hp->adhp, hp->lwh.threads) < 0) {
        return -1;
    }
    lw_log_handler_t* lhp = lwlibav_audio_get_log_handler(hp->adhp);
    lhp->level = LW_LOG_WARNING;
    lhp->priv = &hp->log_type;
    lhp->show_log = au2_log_noop;
    return prepare_audio_decoding(session, opt);
}

int read_video(SessionCore* session, int frame_number, void* buf)
{
    auto* hp = static_cast<LwlibavHandler*>(session->video_private);
    lwlibav_video_decode_handler_t* vdhp = hp->vdhp;
    if (lwlibav_video_get_error(vdhp)) {
        return 0;
    }
    lwlibav_video_output_handler_t* vohp = hp->vohp;
    ++frame_number;
    if (frame_number == 1) {
        auto* out = static_cast<VideoOutputHandler*>(vohp->private_handler);
        std::memcpy(buf, out->back_ground, out->output_frame_size);
    }
    const int ret = lwlibav_video_get_frame(vdhp, vohp, frame_number);
    if (ret != 0 && !(ret == 1 && frame_number == 1)) {
        return 0;
    }
    AVFrame* av_frame = lwlibav_video_get_frame_buffer(vdhp);
    return convert_colorspace(vohp, av_frame, static_cast<uint8_t*>(buf));
}

int read_audio(SessionCore* session, int start, int wanted_length, void* buf)
{
    auto* hp = static_cast<LwlibavHandler*>(session->audio_private);
    return static_cast<int>(lwlibav_audio_get_pcm_samples(hp->adhp, hp->aohp, buf, start, wanted_length));
}

int is_keyframe(SessionCore* session, int frame_number)
{
    auto* hp = static_cast<LwlibavHandler*>(session->video_private);
    return lwlibav_video_is_keyframe(hp->vdhp, hp->vohp, frame_number + 1);
}

int delay_audio(SessionCore* session, int* start, int wanted_length, int audio_delay)
{
    auto* hp = static_cast<LwlibavHandler*>(session->audio_private);
    const int end = *start + wanted_length;
    audio_delay += hp->lwh.av_gap;
    if (*start < audio_delay && end <= audio_delay) {
        lwlibav_audio_force_seek(hp->adhp);
        return 0;
    }
    *start -= audio_delay;
    return 1;
}

void video_cleanup(SessionCore* session)
{
    auto* hp = static_cast<LwlibavHandler*>(session->video_private);
    if (!hp) {
        return;
    }
    lwlibav_video_free_decode_handler_ptr(&hp->vdhp);
    lwlibav_video_free_output_handler_ptr(&hp->vohp);
}

void audio_cleanup(SessionCore* session)
{
    auto* hp = static_cast<LwlibavHandler*>(session->audio_private);
    if (!hp) {
        return;
    }
    lwlibav_audio_free_decode_handler_ptr(&hp->adhp);
    lwlibav_audio_free_output_handler_ptr(&hp->aohp);
}

void close_file(void* private_stuff)
{
    auto* hp = static_cast<LwlibavHandler*>(private_stuff);
    if (!hp) {
        return;
    }
    lwlibav_video_free_decode_handler_ptr(&hp->vdhp);
    lwlibav_video_free_output_handler_ptr(&hp->vohp);
    lwlibav_audio_free_decode_handler_ptr(&hp->adhp);
    lwlibav_audio_free_output_handler_ptr(&hp->aohp);
    lw_free(hp->lwh.file_path);
    delete hp;
}

const ReaderCallbacks callbacks {
    ReaderType::Lwlibav,
    open_file,
    get_video_track,
    get_audio_track,
    nullptr,
    read_video,
    read_audio,
    is_keyframe,
    delay_audio,
    video_cleanup,
    audio_cleanup,
    close_file,
};

} // namespace

const ReaderCallbacks& lwlibav_reader() noexcept
{
    return callbacks;
}

} // namespace au2
