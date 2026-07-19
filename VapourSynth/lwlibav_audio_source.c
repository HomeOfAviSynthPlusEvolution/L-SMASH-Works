/*****************************************************************************
 * lwlibav_audio_source.c
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/mathematics.h>

#include "audio_output.h"
#include "lsmashsource.h"

#include "../common/lwindex.h"
#include "../common/lwlibav_audio.h"
#include "../common/lwlibav_audio_internal.h"
#include "../common/lwlibav_video.h"
#include "../common/progress.h"

typedef struct {
    VSAudioInfo ai;
    lwlibav_file_handler_t lwh;
    lwlibav_audio_decode_handler_t* adhp;
    lwlibav_audio_output_handler_t* aohp;
    lwlibav_video_decode_handler_t* vdhp;
    lwlibav_video_output_handler_t* vohp;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
} lwlibav_audio_handler_t;

static void free_handler(lwlibav_audio_handler_t** hpp)
{
    if (!hpp || !*hpp)
        return;
    lwlibav_audio_handler_t* hp = *hpp;
    if (hp->adhp) {
        lw_free(hp->adhp->gap_list);
        lw_free(lwlibav_audio_get_preferred_decoder_names(hp->adhp));
    }
    lwlibav_audio_free_decode_handler(hp->adhp);
    if (hp->aohp) {
        av_channel_layout_uninit(&hp->aohp->input_channel_layout);
        av_channel_layout_uninit(&hp->aohp->output_channel_layout);
    }
    lwlibav_audio_free_output_handler(hp->aohp);
    lwlibav_video_free_decode_handler(hp->vdhp);
    lwlibav_video_free_output_handler(hp->vohp);
    lw_free(hp->lwh.file_path);
    lw_free(hp);
    *hpp = NULL;
}

static lwlibav_audio_handler_t* alloc_handler(void)
{
    lwlibav_audio_handler_t* hp = (lwlibav_audio_handler_t*)lw_malloc_zero(sizeof(*hp));
    if (!hp)
        return NULL;
    hp->adhp = lwlibav_audio_alloc_decode_handler();
    hp->aohp = lwlibav_audio_alloc_output_handler();
    hp->vdhp = lwlibav_video_alloc_decode_handler();
    hp->vohp = lwlibav_video_alloc_output_handler();
    if (!hp->adhp || !hp->aohp || !hp->vdhp || !hp->vohp) {
        free_handler(&hp);
        return NULL;
    }
    return hp;
}

static int update_indicator(progress_handler_t* php, const char* message, int percent)
{
    static int last_percent = -1;
    if (!strcmp(message, "Creating Index file") && last_percent != percent) {
        last_percent = percent;
        fprintf(stderr, "Creating lwi index file %d%%\r", percent);
        fflush(stderr);
    }
    return 0;
}

static void close_indicator(progress_handler_t* php)
{
    fprintf(stderr, "\n");
}

static int make_gap_list(lwlibav_audio_handler_t* hp, VSMap* out, const VSAPI* vsapi)
{
    lwlibav_audio_decode_handler_t* adhp = hp->adhp;
    if (!hp->aohp->fill_audio_gaps || !adhp->frame_list)
        return 0;
    const audio_frame_info_t* info = adhp->frame_list;
    int gap_count = 0;
    for (uint32_t i = 1; i <= adhp->frame_count; i++) {
        if (info[i].file_offset == -1)
            gap_count++;
    }
    if (!gap_count)
        return 0;
    adhp->gap_list = (lw_audio_gap_info_t*)lw_malloc_zero((size_t)gap_count * sizeof(*adhp->gap_list));
    if (!adhp->gap_list) {
        set_error_on_init(out, vsapi, "lsmas: failed to allocate the audio gap list.");
        return -1;
    }
    int current_sample_rate = info[1].sample_rate > 0 ? info[1].sample_rate : hp->adhp->ctx->sample_rate;
    int current_frame_length = info[1].length;
    uint64_t sequence_sample_count = 0;
    uint64_t prior_sequence_sample_count = 0;
    int gap_index = 0;
    for (uint32_t i = 1; i <= adhp->frame_count; i++) {
        if ((current_sample_rate != info[i].sample_rate && info[i].sample_rate > 0) || current_frame_length != info[i].length) {
            prior_sequence_sample_count += av_rescale_rnd(
                sequence_sample_count, hp->aohp->output_sample_rate, current_sample_rate, AV_ROUND_UP);
            sequence_sample_count = 0;
            current_sample_rate = info[i].sample_rate > 0 ? info[i].sample_rate : hp->adhp->ctx->sample_rate;
            current_frame_length = info[i].length;
        }
        if (info[i].file_offset == -1) {
            int64_t gap_start = prior_sequence_sample_count
                + av_rescale_rnd(sequence_sample_count, hp->aohp->output_sample_rate, current_sample_rate, AV_ROUND_UP);
            int64_t gap_end = prior_sequence_sample_count
                + av_rescale_rnd(
                    sequence_sample_count + info[i].length, hp->aohp->output_sample_rate, current_sample_rate, AV_ROUND_UP);
            int64_t gap_length = gap_end - gap_start;
            if (gap_length <= 0 || gap_length > INT_MAX) {
                set_error_on_init(out, vsapi, "lsmas: the resampled audio gap is too large.");
                return -1;
            }
            adhp->gap_list[gap_index].pts_in_samples = gap_start + hp->lwh.av_gap;
            adhp->gap_list[gap_index].length = (int)gap_length;
            gap_index++;
        }
        sequence_sample_count += info[i].length;
    }
    adhp->gap_count = gap_count;
    return 0;
}

static int delay_audio(lwlibav_audio_handler_t* hp, int64_t* start, int64_t wanted_length)
{
    int64_t end = *start + wanted_length;
    int64_t audio_delay = hp->lwh.av_gap;
    if (*start < audio_delay && end <= audio_delay) {
        lwlibav_audio_force_seek(hp->adhp);
        return 0;
    }
    *start -= audio_delay;
    return 1;
}

static int decode_audio_range(uint8_t* buf, int64_t output_start, int64_t length, lwlibav_audio_handler_t* hp)
{
    /* Synthetic gap frames are already part of the decoder's PCM timeline. */
    int64_t source_start = output_start;
    if (!delay_audio(hp, &source_start, length))
        return 0;
    uint64_t output_length = lwlibav_audio_get_pcm_samples(hp->adhp, hp->aohp, buf, source_start, length);
    if (output_length == (uint64_t)length)
        return 0;
    /* Index-derived lengths can include decoder-delay samples at EOF. The caller pre-fills that remainder with silence. */
    return !hp->adhp->error && output_start == hp->ai.numSamples - length ? 0 : -1;
}

static int render_audio(uint8_t* buf, int64_t start, int64_t wanted_length, lwlibav_audio_handler_t* hp)
{
    lwlibav_audio_decode_handler_t* adhp = hp->adhp;
    lwlibav_audio_output_handler_t* aohp = hp->aohp;
    if (!aohp->fill_audio_gaps || !adhp->gap_list)
        return decode_audio_range(buf, start, wanted_length, hp);

    int64_t end = start + wanted_length;
    int64_t cursor = start;
    for (int i = 0; i < adhp->gap_count; i++) {
        int64_t gap_start = adhp->gap_list[i].pts_in_samples;
        int64_t gap_length = adhp->gap_list[i].length;
        int64_t gap_end = gap_start + gap_length;
        if (gap_end <= cursor)
            continue;
        if (gap_start >= end)
            break;
        if (gap_start > cursor) {
            int64_t decode_length = MIN(gap_start, end) - cursor;
            uint8_t* decode_buf = buf + (cursor - start) * aohp->output_block_align;
            if (decode_audio_range(decode_buf, cursor, decode_length, hp) < 0)
                return -1;
        }
        cursor = MAX(cursor, gap_end);
        if (cursor >= end)
            return 0;
    }
    if (cursor < end) {
        uint8_t* decode_buf = buf + (cursor - start) * aohp->output_block_align;
        if (decode_audio_range(decode_buf, cursor, end - cursor, hp) < 0)
            return -1;
    }
    return 0;
}

static const VSFrame* VS_CC audio_get_frame(
    int n, int activation_reason, void* instance_data, void** frame_data, VSFrameContext* frame_ctx, VSCore* core, const VSAPI* vsapi)
{
    if (activation_reason != arInitial)
        return NULL;
    lwlibav_audio_handler_t* hp = (lwlibav_audio_handler_t*)instance_data;
    int64_t start = (int64_t)n * VS_AUDIO_FRAME_SAMPLES;
    int sample_count = (int)MIN((int64_t)VS_AUDIO_FRAME_SAMPLES, hp->ai.numSamples - start);
    if (sample_count <= 0) {
        vsapi->setFilterError("lsmas: requested audio frame is out of range.", frame_ctx);
        return NULL;
    }

    vs_basic_handler_t vsbh = { 0 };
    vsbh.frame_ctx = frame_ctx;
    vsbh.vsapi = vsapi;
    lw_log_handler_t* lhp = lwlibav_audio_get_log_handler(hp->adhp);
    lhp->priv = &vsbh;
    lhp->show_log = set_error;

    size_t buffer_size = (size_t)sample_count * hp->aohp->output_block_align;
    uint8_t* buffer = (uint8_t*)av_malloc(buffer_size);
    if (!buffer) {
        vsapi->setFilterError("lsmas: failed to allocate an audio buffer.", frame_ctx);
        return NULL;
    }
    memset(buffer, hp->aohp->output_bits_per_sample == 8 ? 0x80 : 0, buffer_size);
    if (render_audio(buffer, start, sample_count, hp) < 0 || hp->adhp->error) {
        av_free(buffer);
        vsapi->setFilterError("lsmas: failed to output an audio frame.", frame_ctx);
        return NULL;
    }
    VSFrame* frame = vs_interleaved_audio_frame(hp->aohp, buffer, sample_count, core, vsapi);
    av_free(buffer);
    if (!frame)
        vsapi->setFilterError("lsmas: failed to create an audio frame.", frame_ctx);
    return frame;
}

static void VS_CC audio_free(void* instance_data, VSCore* core, const VSAPI* vsapi)
{
    free_handler((lwlibav_audio_handler_t**)&instance_data);
}

static void set_av_log_level(int64_t level)
{
    static const int levels[] = { AV_LOG_QUIET, AV_LOG_PANIC, AV_LOG_FATAL, AV_LOG_ERROR, AV_LOG_WARNING, AV_LOG_INFO, AV_LOG_VERBOSE,
        AV_LOG_DEBUG, AV_LOG_TRACE };
    av_log_set_level(level <= 0 ? levels[0] : levels[MIN(level, 8)]);
}

void VS_CC vs_lwlibavaudiosource_create(const VSMap* in, VSMap* out, void* user_data, VSCore* core, const VSAPI* vsapi)
{
    const char* source = vsapi->mapGetData(in, "source", 0, NULL);
    int64_t stream_index;
    int64_t cache;
    int64_t av_sync;
    int64_t rate;
    int64_t ff_loglevel;
    int64_t indexing_progress;
    int64_t fill_agaps;
    const char* cache_file;
    const char* layout;
    const char* decoder;
    const char* cache_dir;
    const char* ff_options;
    int error;
    double drc = vsapi->mapGetFloat(in, "drc_scale", 0, &error);
    if (error)
        drc = -1.0;
    set_option_int64(&stream_index, -1, "stream_index", in, vsapi);
    set_option_int64(&cache, 1, "cache", in, vsapi);
    set_option_int64(&av_sync, 0, "av_sync", in, vsapi);
    set_option_int64(&rate, 0, "rate", in, vsapi);
    set_option_int64(&ff_loglevel, 0, "ff_loglevel", in, vsapi);
    set_option_int64(&indexing_progress, 1, "indexingpr", in, vsapi);
    set_option_int64(&fill_agaps, 0, "fill_agaps", in, vsapi);
    set_option_string(&cache_file, NULL, "cachefile", in, vsapi);
    set_option_string(&layout, NULL, "layout", in, vsapi);
    set_option_string(&decoder, NULL, "decoder", in, vsapi);
    set_option_string(&cache_dir, NULL, "cachedir", in, vsapi);
    set_option_string(&ff_options, NULL, "ff_options", in, vsapi);
    set_av_log_level(ff_loglevel);

    lwlibav_audio_handler_t* hp = alloc_handler();
    if (!hp) {
        vsapi->mapSetError(out, "lsmas: failed to allocate the LWLibav audio handler.");
        return;
    }
    set_preferred_decoder_names_on_buf(hp->preferred_decoder_names_buf, decoder);
    lwlibav_audio_set_preferred_decoder_names(hp->adhp, tokenize_preferred_decoder_names(hp->preferred_decoder_names_buf));
    lwlibav_audio_set_drc(hp->adhp, drc);
    lwlibav_audio_set_decoder_options(hp->adhp, ff_options);
    hp->aohp->fill_audio_gaps = (int)fill_agaps;

    vs_basic_handler_t vsbh = { out, NULL, vsapi };
    lw_log_handler_t lh = { 0 };
    lh.name = "LWLibavAudioSource";
    lh.level = LW_LOG_FATAL;
    lh.priv = &vsbh;
    lh.show_log = set_error;
    lwlibav_audio_set_log_handler(hp->adhp, &lh);

    lwlibav_option_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.file_path = source;
    opt.cache_dir = cache_dir;
    opt.av_sync = av_sync != 0;
    opt.no_create_index = !cache;
    opt.index_file_path = cache_file;
    opt.force_video = 0;
    opt.force_video_index = -1;
    opt.force_audio = stream_index >= 0;
    opt.force_audio_index = stream_index >= 0 ? (int)stream_index : -1;

    progress_indicator_t indicator;
    memset(&indicator, 0, sizeof(indicator));
    if (indexing_progress) {
        indicator.update = update_indicator;
        indicator.close = close_indicator;
    }
    if (lwlibav_construct_index(&hp->lwh, hp->vdhp, hp->vohp, hp->adhp, hp->aohp, &lh, &opt, &indicator, NULL) < 0) {
        free_handler(&hp);
        set_error_on_init(out, vsapi, "lsmas: failed to construct an index for %s.", source);
        return;
    }
    lwlibav_video_free_decode_handler_ptr(&hp->vdhp);
    lwlibav_video_free_output_handler_ptr(&hp->vohp);
    if (lwlibav_audio_get_desired_track(hp->lwh.file_path, hp->adhp, hp->lwh.threads) < 0
        || lwlibav_import_av_index_entry((lwlibav_decode_handler_t*)hp->adhp) < 0) {
        free_handler(&hp);
        vsapi->mapSetError(out, "lsmas: failed to initialize the requested audio track.");
        return;
    }
    AVCodecContext* ctx = lwlibav_audio_get_codec_context(hp->adhp);
    if (vs_setup_audio_rendering(hp->aohp, ctx, layout, (int)MAX(rate, 0), &hp->ai, core, vsapi) < 0) {
        free_handler(&hp);
        vsapi->mapSetError(out, "lsmas: failed to configure audio output.");
        return;
    }
    hp->ai.numSamples = lwlibav_audio_count_overall_pcm_samples(hp->adhp, hp->aohp->output_sample_rate);
    if (hp->ai.numSamples <= 0) {
        free_handler(&hp);
        vsapi->mapSetError(out, "lsmas: no valid audio frame.");
        return;
    }
    if (hp->lwh.av_gap && hp->aohp->output_sample_rate != ctx->sample_rate)
        hp->lwh.av_gap = av_rescale_rnd(
            hp->lwh.av_gap, hp->aohp->output_sample_rate, ctx->sample_rate, AV_ROUND_UP);
    hp->ai.numSamples += hp->lwh.av_gap;
    hp->ai.numFrames = (int)((hp->ai.numSamples + VS_AUDIO_FRAME_SAMPLES - 1) / VS_AUDIO_FRAME_SAMPLES);
    if (make_gap_list(hp, out, vsapi) < 0) {
        free_handler(&hp);
        return;
    }
    lwlibav_audio_force_seek(hp->adhp);

    VSNode* node = vsapi->createAudioFilter2("LWLibavAudioSource", &hp->ai, audio_get_frame, audio_free, fmUnordered, NULL, 0, hp, core);
    if (!node) {
        free_handler(&hp);
        vsapi->mapSetError(out, "lsmas: failed to create the LWLibav audio node.");
        return;
    }
    vsapi->setLinearFilter(node);
    vsapi->mapConsumeNode(out, "audio", node, maAppend);
}
