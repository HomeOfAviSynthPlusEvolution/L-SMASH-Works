/*****************************************************************************
 * libavsmash_audio_source.c
 *****************************************************************************/

#include <inttypes.h>
#include <stdlib.h>

#include <lsmash.h>

#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/mathematics.h>

#include "audio_output.h"
#include "lsmashsource.h"

#include "../common/libavsmash.h"
#include "../common/libavsmash_audio.h"

typedef struct {
    VSAudioInfo ai;
    libavsmash_audio_decode_handler_t* adhp;
    libavsmash_audio_output_handler_t* aohp;
    lsmash_file_parameters_t file_param;
    AVFormatContext* format_ctx;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
} lsmas_audio_handler_t;

static void free_handler(lsmas_audio_handler_t** hpp)
{
    if (!hpp || !*hpp)
        return;
    lsmas_audio_handler_t* hp = *hpp;
    lsmash_root_t* root = hp->adhp ? libavsmash_audio_get_root(hp->adhp) : NULL;
    if (hp->adhp)
        lw_free(libavsmash_audio_get_preferred_decoder_names(hp->adhp));
    libavsmash_audio_free_decode_handler(hp->adhp);
    if (hp->aohp) {
        av_channel_layout_uninit(&hp->aohp->input_channel_layout);
        av_channel_layout_uninit(&hp->aohp->output_channel_layout);
    }
    libavsmash_audio_free_output_handler(hp->aohp);
    avformat_close_input(&hp->format_ctx);
    lsmash_close_file(&hp->file_param);
    if (root)
        lsmash_destroy_root(root);
    lw_free(hp);
    *hpp = NULL;
}

static lsmas_audio_handler_t* alloc_handler(void)
{
    lsmas_audio_handler_t* hp = (lsmas_audio_handler_t*)lw_malloc_zero(sizeof(*hp));
    if (!hp)
        return NULL;
    hp->adhp = libavsmash_audio_alloc_decode_handler();
    hp->aohp = libavsmash_audio_alloc_output_handler();
    if (!hp->adhp || !hp->aohp) {
        free_handler(&hp);
        return NULL;
    }
    return hp;
}

static int64_t get_start_time(lsmash_root_t* root, uint32_t track_id)
{
    uint32_t edit_count = lsmash_count_explicit_timeline_map(root, track_id);
    for (uint32_t edit_number = 1; edit_number <= edit_count; edit_number++) {
        lsmash_edit_t edit;
        if (lsmash_get_explicit_timeline_map(root, track_id, edit_number, &edit) || edit.duration == 0)
            return 0;
        if (edit.start_time >= 0)
            return edit.start_time;
    }
    return 0;
}

static char* duplicate_string(const void* src, size_t length)
{
    char* dst = (char*)malloc(length + 1);
    if (!dst)
        return NULL;
    memcpy(dst, src, length);
    dst[length] = '\0';
    return dst;
}

static int count_output_audio_samples(lsmas_audio_handler_t* hp, int skip_priming, VSMap* out, const VSAPI* vsapi)
{
    libavsmash_audio_decode_handler_t* adhp = hp->adhp;
    libavsmash_audio_output_handler_t* aohp = hp->aohp;
    lsmash_root_t* root = libavsmash_audio_get_root(adhp);
    uint32_t track_id = libavsmash_audio_get_track_id(adhp);
    uint64_t start_time = 0;
    if (skip_priming) {
        uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
        uint32_t metadata_count = lsmash_count_itunes_metadata(root);
        for (uint32_t i = 1; i <= metadata_count; i++) {
            lsmash_itunes_metadata_t metadata;
            if (lsmash_get_itunes_metadata(root, i, &metadata) < 0)
                continue;
            int matches = metadata.item == ITUNES_METADATA_ITEM_CUSTOM
                && (metadata.type == ITUNES_METADATA_TYPE_STRING || metadata.type == ITUNES_METADATA_TYPE_BINARY) && metadata.meaning
                && metadata.name && !strcmp(metadata.meaning, "com.apple.iTunes") && !strcmp(metadata.name, "iTunSMPB");
            char* value = NULL;
            if (matches && metadata.type == ITUNES_METADATA_TYPE_STRING) {
                size_t length = strlen(metadata.value.string);
                if (length >= 116)
                    value = duplicate_string(metadata.value.string, length);
            } else if (matches && metadata.value.binary.size >= 116)
                value = duplicate_string(metadata.value.binary.data, metadata.value.binary.size);
            lsmash_cleanup_itunes_metadata(&metadata);
            if (!value)
                continue;
            uint32_t dummy[9];
            uint32_t priming_samples;
            uint32_t padding;
            uint64_t duration;
            int fields = sscanf(value, " %x %x %x %" SCNx64 " %x %x %x %x %x %x %x %x", &dummy[0], &priming_samples, &padding,
                &duration, &dummy[1], &dummy[2], &dummy[3], &dummy[4], &dummy[5], &dummy[6], &dummy[7], &dummy[8]);
            lw_free(value);
            if (fields != 12)
                continue;
            libavsmash_audio_set_implicit_preroll(adhp);
            start_time = av_rescale(priming_samples, media_timescale, aohp->output_sample_rate);
            aohp->skip_decoded_samples = priming_samples;
            break;
        }
        if (aohp->skip_decoded_samples == 0) {
            uint32_t ctd_shift;
            if (lsmash_get_composition_to_decode_shift_from_media_timeline(root, track_id, &ctd_shift)) {
                set_error_on_init(out, vsapi, "lsmas: failed to get the audio timeline shift.");
                return -1;
            }
            start_time = ctd_shift + get_start_time(root, track_id);
            aohp->skip_decoded_samples = av_rescale(start_time, aohp->output_sample_rate, media_timescale);
        }
    }
    hp->ai.numSamples = libavsmash_audio_count_overall_pcm_samples(adhp, aohp->output_sample_rate, start_time);
    if (hp->ai.numSamples <= 0) {
        set_error_on_init(out, vsapi, "lsmas: no valid audio frame.");
        return -1;
    }
    hp->ai.numFrames = (int)((hp->ai.numSamples + VS_AUDIO_FRAME_SAMPLES - 1) / VS_AUDIO_FRAME_SAMPLES);
    return 0;
}

static const VSFrame* VS_CC audio_get_frame(
    int n, int activation_reason, void* instance_data, void** frame_data, VSFrameContext* frame_ctx, VSCore* core, const VSAPI* vsapi)
{
    if (activation_reason != arInitial)
        return NULL;
    lsmas_audio_handler_t* hp = (lsmas_audio_handler_t*)instance_data;
    int64_t start = (int64_t)n * VS_AUDIO_FRAME_SAMPLES;
    int sample_count = (int)MIN((int64_t)VS_AUDIO_FRAME_SAMPLES, hp->ai.numSamples - start);
    if (sample_count <= 0) {
        vsapi->setFilterError("lsmas: requested audio frame is out of range.", frame_ctx);
        return NULL;
    }

    vs_basic_handler_t vsbh = { 0 };
    vsbh.frame_ctx = frame_ctx;
    vsbh.vsapi = vsapi;
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(hp->adhp);
    lhp->priv = &vsbh;
    lhp->show_log = set_error;

    size_t buffer_size = (size_t)sample_count * hp->aohp->output_block_align;
    uint8_t* buffer = (uint8_t*)av_malloc(buffer_size);
    if (!buffer) {
        vsapi->setFilterError("lsmas: failed to allocate an audio buffer.", frame_ctx);
        return NULL;
    }
    memset(buffer, hp->aohp->output_bits_per_sample == 8 ? 0x80 : 0, buffer_size);
    uint64_t output_count = libavsmash_audio_get_pcm_samples(hp->adhp, hp->aohp, buffer, start, sample_count);
    if (output_count != (uint64_t)sample_count || libavsmash_audio_get_error(hp->adhp)) {
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
    free_handler((lsmas_audio_handler_t**)&instance_data);
}

static void set_av_log_level(int64_t level)
{
    static const int levels[] = { AV_LOG_QUIET, AV_LOG_PANIC, AV_LOG_FATAL, AV_LOG_ERROR, AV_LOG_WARNING, AV_LOG_INFO, AV_LOG_VERBOSE,
        AV_LOG_DEBUG, AV_LOG_TRACE };
    av_log_set_level(level <= 0 ? levels[0] : levels[MIN(level, 8)]);
}

void VS_CC vs_libavsmashaudiosource_create(const VSMap* in, VSMap* out, void* user_data, VSCore* core, const VSAPI* vsapi)
{
    const char* source = vsapi->mapGetData(in, "source", 0, NULL);
    int64_t track;
    int64_t skip_priming;
    int64_t rate;
    int64_t ff_loglevel;
    const char* layout;
    const char* decoder;
    const char* ff_options;
    int error;
    double drc = vsapi->mapGetFloat(in, "drc_scale", 0, &error);
    if (error)
        drc = -1.0;
    set_option_int64(&track, 0, "track", in, vsapi);
    set_option_int64(&skip_priming, 1, "skip_priming", in, vsapi);
    set_option_int64(&rate, 0, "rate", in, vsapi);
    set_option_int64(&ff_loglevel, 0, "ff_loglevel", in, vsapi);
    set_option_string(&layout, NULL, "layout", in, vsapi);
    set_option_string(&decoder, NULL, "decoder", in, vsapi);
    set_option_string(&ff_options, NULL, "ff_options", in, vsapi);
    set_av_log_level(ff_loglevel);

    lsmas_audio_handler_t* hp = alloc_handler();
    if (!hp) {
        vsapi->mapSetError(out, "lsmas: failed to allocate the LibavSMASH audio handler.");
        return;
    }
    vs_basic_handler_t vsbh = { out, NULL, vsapi };
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(hp->adhp);
    lhp->name = "LibavSMASHAudioSource";
    lhp->level = LW_LOG_FATAL;
    lhp->priv = &vsbh;
    lhp->show_log = set_error;
    set_preferred_decoder_names_on_buf(hp->preferred_decoder_names_buf, decoder);
    libavsmash_audio_set_preferred_decoder_names(hp->adhp, tokenize_preferred_decoder_names(hp->preferred_decoder_names_buf));
    libavsmash_audio_set_drc(hp->adhp, drc);
    libavsmash_audio_set_decoder_options(hp->adhp, ff_options);

    lsmash_movie_parameters_t movie_param;
    lsmash_root_t* root = libavsmash_open_file(&hp->format_ctx, source, &hp->file_param, &movie_param, lhp);
    if (!root) {
        free_handler(&hp);
        set_error_on_init(out, vsapi, "lsmas: failed to open %s.", source);
        return;
    }
    libavsmash_audio_set_root(hp->adhp, root);
    if ((track > 0 && track > movie_param.number_of_tracks) || libavsmash_audio_get_track(hp->adhp, (uint32_t)track) < 0) {
        free_handler(&hp);
        set_error_on_init(out, vsapi, "lsmas: failed to get the requested audio track.");
        return;
    }
    if (libavsmash_audio_initialize_decoder_configuration(hp->adhp, hp->format_ctx, 0) < 0) {
        free_handler(&hp);
        set_error_on_init(out, vsapi, "lsmas: failed to initialize the audio decoder configuration.");
        return;
    }
    av_channel_layout_from_mask(&hp->aohp->output_channel_layout, libavsmash_audio_get_best_used_channel_layout(hp->adhp));
    hp->aohp->output_sample_format = libavsmash_audio_get_best_used_sample_format(hp->adhp);
    hp->aohp->output_sample_rate = libavsmash_audio_get_best_used_sample_rate(hp->adhp);
    hp->aohp->output_bits_per_sample = libavsmash_audio_get_best_used_bits_per_sample(hp->adhp);
    AVCodecContext* ctx = libavsmash_audio_get_codec_context(hp->adhp);
    if (vs_setup_audio_rendering(hp->aohp, ctx, layout, (int)MAX(rate, 0), &hp->ai, core, vsapi) < 0
        || count_output_audio_samples(hp, skip_priming != 0, out, vsapi) < 0) {
        free_handler(&hp);
        if (!vsapi->mapGetError(out))
            set_error_on_init(out, vsapi, "lsmas: failed to configure audio output.");
        return;
    }
    libavsmash_audio_force_seek(hp->adhp);
    lsmash_discard_boxes(root);

    VSNode* node = vsapi->createAudioFilter2(
        "LibavSMASHAudioSource", &hp->ai, audio_get_frame, audio_free, fmUnordered, NULL, 0, hp, core);
    if (!node) {
        free_handler(&hp);
        vsapi->mapSetError(out, "lsmas: failed to create the LibavSMASH audio node.");
        return;
    }
    vsapi->setLinearFilter(node);
    vsapi->mapConsumeNode(out, "audio", node, maAppend);
}
