/*****************************************************************************
 * libavsmash_audio.c / libavsmash_audio.cpp
 *****************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "libavsmash_audio.h"
#include "cpp_compat.h"
#include "resample.h"
#include <limits.h>

#include <libavutil/mathematics.h>

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
libavsmash_audio_decode_handler_t* libavsmash_audio_alloc_decode_handler(void)
{
    libavsmash_audio_decode_handler_t* adhp = (libavsmash_audio_decode_handler_t*)lw_malloc_zero(sizeof(libavsmash_audio_decode_handler_t));
    if (!adhp)
        return NULL;
    adhp->frame_buffer = av_frame_alloc();
    if (!adhp->frame_buffer) {
        libavsmash_audio_free_decode_handler(adhp);
        return NULL;
    }
    return adhp;
}

libavsmash_audio_output_handler_t* libavsmash_audio_alloc_output_handler(void)
{
    return (libavsmash_audio_output_handler_t*)lw_malloc_zero(sizeof(libavsmash_audio_output_handler_t));
}

void libavsmash_audio_free_decode_handler(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp)
        return;
    av_frame_free(&adhp->frame_buffer);
    cleanup_configuration(&adhp->config);
    lw_free(adhp);
}

void libavsmash_audio_free_output_handler(libavsmash_audio_output_handler_t* aohp)
{
    if (!aohp)
        return;
    lw_cleanup_audio_output_handler(aohp);
    lw_free(aohp);
}

void libavsmash_audio_free_decode_handler_ptr(libavsmash_audio_decode_handler_t** adhpp)
{
    if (!adhpp || !*adhpp)
        return;
    libavsmash_audio_free_decode_handler(*adhpp);
    *adhpp = NULL;
}

void libavsmash_audio_free_output_handler_ptr(libavsmash_audio_output_handler_t** aohpp)
{
    if (!aohpp || !*aohpp)
        return;
    libavsmash_audio_free_output_handler(*aohpp);
    *aohpp = NULL;
}

/*****************************************************************************
 * Setters
 *****************************************************************************/
void libavsmash_audio_set_root(libavsmash_audio_decode_handler_t* adhp, lsmash_root_t* root)
{
    adhp->root = root;
}

void libavsmash_audio_set_track_id(libavsmash_audio_decode_handler_t* adhp, uint32_t track_id)
{
    adhp->track_id = track_id;
}

void libavsmash_audio_set_preferred_decoder_names(libavsmash_audio_decode_handler_t* adhp, const char** preferred_decoder_names)
{
    adhp->config.preferred_decoder_names = preferred_decoder_names;
}

void libavsmash_audio_set_drc(libavsmash_audio_decode_handler_t* adhp, const double drc)
{
    adhp->config.drc = drc;
}

void libavsmash_audio_set_decoder_options(libavsmash_audio_decode_handler_t* adhp, const char* ff_options)
{
    adhp->config.ff_options = ff_options;
}

/*****************************************************************************
 * Getters
 *****************************************************************************/
lsmash_root_t* libavsmash_audio_get_root(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->root : NULL;
}

uint32_t libavsmash_audio_get_track_id(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->track_id : 0;
}

AVCodecContext* libavsmash_audio_get_codec_context(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.ctx : NULL;
}

const char** libavsmash_audio_get_preferred_decoder_names(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.preferred_decoder_names : NULL;
}

int libavsmash_audio_get_error(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.error : -1;
}

uint64_t libavsmash_audio_get_best_used_channel_layout(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.prefer.channel_layout : 0;
}

enum AVSampleFormat libavsmash_audio_get_best_used_sample_format(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.prefer.sample_format : AV_SAMPLE_FMT_NONE;
}

int libavsmash_audio_get_best_used_sample_rate(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.prefer.sample_rate : 0;
}

int libavsmash_audio_get_best_used_bits_per_sample(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->config.prefer.bits_per_sample : 0;
}

lw_log_handler_t* libavsmash_audio_get_log_handler(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? &adhp->config.lh : NULL;
}

uint32_t libavsmash_audio_get_sample_count(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->frame_count : 0;
}

uint32_t libavsmash_audio_get_media_timescale(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->media_timescale : 0;
}

uint64_t libavsmash_audio_get_media_duration(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->media_duration : 0;
}

uint64_t libavsmash_audio_get_min_cts(libavsmash_audio_decode_handler_t* adhp)
{
    return adhp ? adhp->min_cts : UINT64_MAX;
}

/*****************************************************************************
 * Fetchers
 *****************************************************************************/
static uint32_t libavsmash_audio_fetch_sample_count(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp)
        return 0;
    adhp->frame_count = lsmash_get_sample_count_in_media_timeline(adhp->root, adhp->track_id);
    return adhp->frame_count;
}

static uint32_t libavsmash_audio_fetch_media_timescale(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp)
        return 0;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters(&media_param);
    if (lsmash_get_media_parameters(adhp->root, adhp->track_id, &media_param) < 0)
        return 0;
    adhp->media_timescale = media_param.timescale;
    return adhp->media_timescale;
}

static uint64_t libavsmash_audio_fetch_media_duration(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp)
        return 0;
    adhp->media_duration = lsmash_get_media_duration_from_media_timeline(adhp->root, adhp->track_id);
    return adhp->media_duration;
}

/* This function assume that no audio frame reorderings in composition timeline. */
static uint64_t libavsmash_audio_fetch_min_cts(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp || lsmash_get_cts_from_media_timeline(adhp->root, adhp->track_id, 1, &adhp->min_cts) < 0)
        return UINT64_MAX;
    return adhp->min_cts;
}

/*****************************************************************************
 * Others
 *****************************************************************************/
static AVCodecParameters* libavsmash_audio_find_codecpar(AVFormatContext* format_ctx, uint32_t track_id)
{
    AVCodecParameters* fallback = NULL;
    for (uint32_t i = 0; i < format_ctx->nb_streams; i++) {
        AVStream* stream = format_ctx->streams[i];
        AVCodecParameters* codecpar = stream->codecpar;
        if (codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (!fallback)
            fallback = codecpar;
        if ((uint32_t)stream->id == track_id)
            return codecpar;
    }
    return fallback;
}

int libavsmash_audio_get_track(libavsmash_audio_decode_handler_t* adhp, uint32_t track_number)
{
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(adhp);
    uint32_t track_id
        = libavsmash_get_track_by_media_type(libavsmash_audio_get_root(adhp), ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK, track_number, lhp);
    if (track_id == 0)
        return -1;
    libavsmash_audio_set_track_id(adhp, track_id);
    (void)libavsmash_audio_fetch_sample_count(adhp);
    (void)libavsmash_audio_fetch_media_duration(adhp);
    (void)libavsmash_audio_fetch_media_timescale(adhp);
    (void)libavsmash_audio_fetch_min_cts(adhp);
    return 0;
}

int libavsmash_audio_initialize_decoder_configuration(libavsmash_audio_decode_handler_t* adhp, AVFormatContext* format_ctx, int threads)
{
    char error_string[128] = { 0 };
    if (libavsmash_audio_get_summaries(adhp) < 0)
        return -1;
    /* libavformat */
    AVCodecParameters* codecpar = libavsmash_audio_find_codecpar(format_ctx, adhp->track_id);
    if (!codecpar) {
        strcpy(error_string, "Failed to find stream by libavformat.\n");
        goto fail;
    }
    /* libavcodec */
    if (libavsmash_find_and_open_decoder(&adhp->config, codecpar, threads) < 0) {
        strcpy(error_string, "Failed to find and open the audio decoder.\n");
        goto fail;
    }
    return initialize_decoder_configuration(adhp->root, adhp->track_id, &adhp->config);
fail:;
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(adhp);
    lw_log_show(lhp, LW_LOG_FATAL, "%s", error_string);
    return -1;
}

int libavsmash_audio_get_summaries(libavsmash_audio_decode_handler_t* adhp)
{
    return get_summaries(adhp->root, adhp->track_id, &adhp->config);
}

void libavsmash_audio_force_seek(libavsmash_audio_decode_handler_t* adhp)
{
    /* Force seek before the next reading. */
    adhp->next_pcm_sample_number = adhp->pcm_sample_count + 1;
}

void libavsmash_audio_clear_error(libavsmash_audio_decode_handler_t* adhp)
{
    adhp->config.error = 0;
}

void libavsmash_audio_close_codec_context(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp || !adhp->config.ctx)
        return;
    avcodec_free_context(&adhp->config.ctx);
}

void libavsmash_audio_apply_delay(libavsmash_audio_decode_handler_t* adhp, int64_t delay)
{
    if (!adhp)
        return;
    adhp->pcm_sample_count += delay;
}

void libavsmash_audio_set_implicit_preroll(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp)
        return;
    adhp->implicit_preroll = 1;
}

static inline uint64_t count_sequence_output_pcm_samples(uint64_t sequence_pcm_count, int current_sample_rate, int output_sample_rate)
{
    uint64_t resampled_sample_count;
    if (output_sample_rate == current_sample_rate)
        resampled_sample_count = sequence_pcm_count;
    else
        resampled_sample_count = av_rescale_rnd(sequence_pcm_count, output_sample_rate, current_sample_rate, AV_ROUND_UP);
    return resampled_sample_count;
}

static int get_frame_length(
    libavsmash_audio_decode_handler_t* adhp, uint32_t frame_number, uint64_t* frame_length, extended_summary_t** esp)
{
    lsmash_sample_t sample;
    if (lsmash_get_sample_info_from_media_timeline(adhp->root, adhp->track_id, frame_number, &sample) < 0)
        return -1;
    *esp = &adhp->config.entries[sample.index - 1].extended;
    extended_summary_t* es = *esp;
    if (es->frame_length == 0) {
        /* variable frame length
         * Guess the frame length from sample duration. */
        uint32_t frame_length_32;
        if (lsmash_get_sample_delta_from_media_timeline(adhp->root, adhp->track_id, frame_number, &frame_length_32) < 0)
            return -1;
        int64_t temp_frame_length = av_rescale(frame_length_32, es->sample_rate, adhp->media_timescale);
        if (temp_frame_length < 0)
            return -1;
        *frame_length = (uint64_t)temp_frame_length;
    } else
        /* constant frame length */
        *frame_length = (uint64_t)es->frame_length;
    return 0;
}

static int parse_smpb_hex_u64(const char** pp, uint64_t* out)
{
    const char* p = *pp;
    while (*p != '\0' && isspace((unsigned char)*p))
        p++;
    /* SMPB fields are unsigned hex. Reject explicit signs. */
    if (*p == '+' || *p == '-')
        return -1;
    if (!isxdigit((unsigned char)*p))
        return -1;
    errno = 0;
    char* end = NULL;
    unsigned long long v = strtoull(p, &end, 16);
    if (errno == ERANGE || end == p)
        return -1;
    *out = (uint64_t)v;
    *pp = end;
    return 0;
}

static int parse_itun_smpb_value(const char* value, uint32_t* priming_samples, uint32_t* padding_samples, uint64_t* duration_samples)
{
    if (!value || !priming_samples || !padding_samples || !duration_samples)
        return -1;
    const char* p = value;
    for (int i = 0; i < 12; i++) {
        uint64_t val = 0;
        if (parse_smpb_hex_u64(&p, &val) < 0)
            return -1;
        // Only extract the fields we actually use. Ignore the size of the dummy fields.
        if (i == 1) {
            // Priming samples physically cannot exceed 32 bits (that would be >27 hours of audio)
            if (val > UINT32_MAX)
                return -1;
            *priming_samples = (uint32_t)val;
        } else if (i == 2) {
            if (val > UINT32_MAX)
                return -1;
            *padding_samples = (uint32_t)val;
        } else if (i == 3) {
            *duration_samples = val;
        }
    }
    return 0;
}

static char* duplicate_itunes_metadata_value_as_cstring(const lsmash_itunes_metadata_t* metadata)
{
    if (metadata->type == ITUNES_METADATA_TYPE_STRING) {
        if (!metadata->value.string)
            return NULL;
        size_t length = strlen(metadata->value.string);
        char* value = (char*)malloc(length + 1);
        if (!value)
            return NULL;
        memcpy(value, metadata->value.string, length + 1);
        return value;
    }
    if (metadata->type == ITUNES_METADATA_TYPE_BINARY) {
        if (!metadata->value.binary.data || metadata->value.binary.size == 0)
            return NULL;
        char* value = (char*)malloc((size_t)metadata->value.binary.size + 1);
        if (!value)
            return NULL;
        memcpy(value, metadata->value.binary.data, metadata->value.binary.size);
        value[metadata->value.binary.size] = '\0';
        return value;
    }
    return NULL;
}

static int is_itun_smpb_metadata(const lsmash_itunes_metadata_t* metadata)
{
    return metadata->item == ITUNES_METADATA_ITEM_CUSTOM
        && (metadata->type == ITUNES_METADATA_TYPE_STRING || metadata->type == ITUNES_METADATA_TYPE_BINARY) && metadata->meaning
        && metadata->name && strcmp(metadata->meaning, "com.apple.iTunes") == 0 && strcmp(metadata->name, "iTunSMPB") == 0;
}

int libavsmash_audio_get_itun_smpb(lsmash_root_t* root, uint32_t* priming_samples, uint32_t* padding_samples, uint64_t* duration_samples)
{
    if (!root || !priming_samples || !padding_samples || !duration_samples)
        return -1;
    uint32_t metadata_count = lsmash_count_itunes_metadata(root);
    for (uint32_t i = 1; i <= metadata_count; i++) {
        lsmash_itunes_metadata_t metadata;
        if (lsmash_get_itunes_metadata(root, i, &metadata) < 0)
            continue;
        char* value = NULL;
        if (is_itun_smpb_metadata(&metadata))
            value = duplicate_itunes_metadata_value_as_cstring(&metadata);
        lsmash_cleanup_itunes_metadata(&metadata);
        if (!value)
            continue;
        int ret = parse_itun_smpb_value(value, priming_samples, padding_samples, duration_samples);
        free(value);
        if (ret == 0)
            return 0;
    }
    return -1;
}

static int get_smpb_codec_sample_rate(libavsmash_audio_decode_handler_t* adhp, lw_audio_output_handler_t* aohp, int* codec_sample_rate)
{
    if (!adhp || !aohp || !codec_sample_rate)
        return -1;
    int output_sample_rate = aohp->output_sample_rate;
    if (output_sample_rate <= 0)
        return -1;
    int rate = 0;
    AVCodecContext* ctx = libavsmash_audio_get_codec_context(adhp);
    if (ctx && ctx->sample_rate > 0)
        rate = ctx->sample_rate;
    if (rate <= 0)
        rate = libavsmash_audio_get_best_used_sample_rate(adhp);
    if (rate <= 0) {
        uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
        if (media_timescale > 0 && media_timescale <= (uint32_t)INT_MAX)
            rate = (int)media_timescale;
    }
    if (rate <= 0)
        return -1;
    *codec_sample_rate = rate;
    return 0;
}

static int convert_smpb_samples64_to_output_rnd(libavsmash_audio_decode_handler_t* adhp, lw_audio_output_handler_t* aohp, uint64_t samples,
    uint64_t* output_samples, enum AVRounding rnd)
{
    if (!adhp || !aohp || !output_samples)
        return -1;
    if (samples == 0) {
        *output_samples = 0;
        return 0;
    }
    if (samples > (uint64_t)INT64_MAX)
        return -1;
    int codec_sample_rate = 0;
    if (get_smpb_codec_sample_rate(adhp, aohp, &codec_sample_rate) < 0)
        return -1;
    if (codec_sample_rate <= 0 || aohp->output_sample_rate <= 0)
        return -1;
    int64_t converted = av_rescale_rnd((int64_t)samples, aohp->output_sample_rate, codec_sample_rate, rnd);
    if (converted < 0)
        return -1;
    *output_samples = (uint64_t)converted;
    return 0;
}

int libavsmash_audio_convert_smpb_samples_to_output(libavsmash_audio_decode_handler_t* adhp, lw_audio_output_handler_t* aohp,
    uint64_t samples, uint64_t* output_samples, enum AVRounding rnd)
{
    return convert_smpb_samples64_to_output_rnd(adhp, aohp, samples, output_samples, rnd);
}

uint64_t libavsmash_audio_count_total_codec_samples(libavsmash_audio_decode_handler_t* adhp)
{
    if (!adhp || adhp->frame_count == 0 || adhp->media_timescale == 0)
        return 0;
    codec_configuration_t* config = &adhp->config;
    int fallback_sample_rate = config->ctx ? config->ctx->sample_rate : 0;
    extended_summary_t* es = NULL;
    int current_sample_rate = 0;
    uint64_t current_frame_length = 0;
    uint64_t total_codec_samples = 0;
    for (uint32_t i = 1; i <= adhp->frame_count; i++) {
        uint64_t frame_length;
        if (get_frame_length(adhp, i, &frame_length, &es) < 0)
            continue;
        if (!es)
            continue;
        if ((current_sample_rate != es->sample_rate && es->sample_rate > 0) || current_frame_length != frame_length) {
            current_sample_rate = es->sample_rate > 0 ? es->sample_rate : fallback_sample_rate;
            current_frame_length = frame_length;
        }
        if (current_sample_rate > 0)
            total_codec_samples += frame_length;
    }
    return total_codec_samples;
}

int libavsmash_audio_apply_tail_trim(libavsmash_audio_decode_handler_t* adhp, lw_audio_output_handler_t* aohp, int have_smpb,
    uint32_t priming_samples, uint32_t padding_samples, uint64_t duration_samples, int skip_priming, uint64_t total_codec_samples,
    uint64_t base_output_samples, uint64_t* final_output_samples)
{
    if (!adhp || !aohp || !final_output_samples)
        return -1;
    uint64_t result = base_output_samples;
    if (have_smpb) {
        uint64_t effective_priming = 0;
        if (skip_priming && priming_samples <= total_codec_samples)
            effective_priming = priming_samples;
        int trimmed_by_duration = 0;
        /*
            Preferred method:
            Use SMPB duration only when:
                - SMPB is present,
                - tail trimming is enabled,
                - priming skipping is enabled,
                - duration is sane.
            Duration is considered sane if:
                duration > 0
                duration <= total_codec_samples - effective_priming
        */
        if (skip_priming && duration_samples > 0 && duration_samples <= total_codec_samples - effective_priming) {
            uint64_t duration_out = 0;
            /*
                For VapourSynth strict EOF we use conservative rounding AV_ROUND_DOWN.
                If VapourSynth EOF is changed to match the AviSynth, more relaxed rounding AV_ROUND_UP can be used.
            */
            if (libavsmash_audio_convert_smpb_samples_to_output(adhp, aohp, duration_samples, &duration_out, AV_ROUND_DOWN) == 0
                && duration_out > 0) {
                if (duration_out > base_output_samples)
                    duration_out = base_output_samples;
                result = duration_out;
                trimmed_by_duration = 1;
            }
        }
        /*
            Fallback method:
            If duration was not used, use padding if sane.
            Padding is considered sane if:
                padding <= total_codec_samples - effective_priming
        */
        if (!trimmed_by_duration && padding_samples <= total_codec_samples - effective_priming) {
            uint64_t padding_out = 0;
            if (libavsmash_audio_convert_smpb_samples_to_output(adhp, aohp, padding_samples, &padding_out, AV_ROUND_UP) == 0) {
                if (padding_out > base_output_samples)
                    padding_out = base_output_samples;
                if (padding_out > 0 && padding_out < base_output_samples)
                    // We assume the older result is safe, instead return -1;
                    result = base_output_samples - padding_out;
            }
        }
    }
    *final_output_samples = result;
    return 0;
}

uint64_t libavsmash_audio_count_overall_pcm_samples(
    libavsmash_audio_decode_handler_t* adhp, int output_sample_rate, uint64_t start_output_samples)
{
    if (!adhp || output_sample_rate <= 0) {
        if (adhp)
            adhp->pcm_sample_count = 0;
        return 0;
    }
    codec_configuration_t* config = &adhp->config;
    int fallback_sample_rate = config->ctx ? config->ctx->sample_rate : 0;
    extended_summary_t* es = NULL;
    int current_sample_rate = 0;
    uint64_t current_frame_length = 0;
    uint64_t sequence_pcm_count = 0;
    uint64_t overall_pcm_count = 0;
    /* Count the number of output PCM audio samples in each sequence. */
    for (uint32_t i = 1; i <= adhp->frame_count; i++) {
        uint64_t frame_length;
        if (get_frame_length(adhp, i, &frame_length, &es) < 0)
            continue;
        if ((current_sample_rate != es->sample_rate && es->sample_rate > 0) || current_frame_length != frame_length) {
            /* Encountered a new sequence. */
            if (current_sample_rate > 0) {
                /* Add the number of output PCM audio samples in the previous sequence. */
                overall_pcm_count += count_sequence_output_pcm_samples(sequence_pcm_count, current_sample_rate, output_sample_rate);
                sequence_pcm_count = 0;
            }
            current_sample_rate = es->sample_rate > 0 ? es->sample_rate : fallback_sample_rate;
            current_frame_length = frame_length;
        }
        if (current_sample_rate > 0)
            sequence_pcm_count += frame_length;
    }
    if (!es || (sequence_pcm_count == 0 && overall_pcm_count == 0)) {
        adhp->pcm_sample_count = 0;
        return 0;
    }
    current_sample_rate = es->sample_rate > 0 ? es->sample_rate : fallback_sample_rate;
    if (current_sample_rate > 0) {
        overall_pcm_count += count_sequence_output_pcm_samples(sequence_pcm_count, current_sample_rate, output_sample_rate);
    }
    /*
        start_output_samples is already in the output sample rate.

        This avoids a media-time round trip and keeps the total length consistent
        with aohp->skip_decoded_samples.
    */
    overall_pcm_count = overall_pcm_count > start_output_samples ? overall_pcm_count - start_output_samples : 0;
    adhp->pcm_sample_count = overall_pcm_count;
    return overall_pcm_count;
}

/* Get pre-roll to make output samples assured to be correct, and update the target frame number if needed.
 * Some audio CODEC requires pre-roll for correct composition. */
static uint64_t get_preroll_samples(libavsmash_audio_decode_handler_t* adhp, uint64_t skip_decoded_samples, uint32_t* frame_number)
{
    lsmash_sample_property_t prop;
    if (lsmash_get_sample_property_from_media_timeline(adhp->root, adhp->track_id, *frame_number, &prop) < 0)
        return 0;
    if (prop.pre_roll.distance == 0) {
        if (skip_decoded_samples == 0 || !adhp->implicit_preroll)
            return 0;
        /* Estimate pre-roll distance. */
        for (uint32_t i = 1; i <= adhp->frame_count || skip_decoded_samples; i++) {
            extended_summary_t* dummy = NULL;
            uint64_t frame_length;
            if (get_frame_length(adhp, i, &frame_length, &dummy) < 0)
                break;
            if (skip_decoded_samples < frame_length)
                skip_decoded_samples = 0;
            else
                skip_decoded_samples -= frame_length;
            ++prop.pre_roll.distance;
        }
    }
    uint64_t preroll_samples = 0;
    for (uint32_t i = 0; i < prop.pre_roll.distance; i++) {
        if (*frame_number > 1)
            --(*frame_number);
        else
            break;
        extended_summary_t* dummy = NULL;
        uint64_t frame_length;
        if (get_frame_length(adhp, *frame_number, &frame_length, &dummy) < 0)
            break;
        preroll_samples += frame_length;
    }
    return preroll_samples;
}

static int find_start_audio_frame(
    libavsmash_audio_decode_handler_t* adhp, int output_sample_rate, uint64_t skip_decoded_samples, /* at output sampling rate */
    uint64_t start_frame_pos, /* at output sampling rate */
    uint64_t* start_offset /* at codec sampling rate since trimming by this before sending resampler */
)
{
    uint32_t frame_number = 1;
    uint64_t current_frame_pos = 0;
    uint64_t next_frame_pos = 0;
    int current_sample_rate = 0;
    uint64_t current_frame_length = 0;
    uint64_t decoded_pcm_sample_count = 0; /* the number of accumulated PCM samples before resampling per sequence */
    uint64_t resampled_sample_count = 0; /* the number of accumulated PCM samples after resampling per sequence */
    uint64_t prior_sequences_resampled_count = 0; /* the number of accumulated PCM samples of all prior sequences */
    do {
        current_frame_pos = next_frame_pos;
        extended_summary_t* es = NULL;
        uint64_t frame_length;
        if (get_frame_length(adhp, frame_number, &frame_length, &es) < 0) {
            ++frame_number;
            continue;
        }
        if ((current_sample_rate != es->sample_rate && es->sample_rate > 0) || current_frame_length != frame_length) {
            /* Encountered a new sequence. */
            prior_sequences_resampled_count += resampled_sample_count;
            decoded_pcm_sample_count = 0;
            current_sample_rate = es->sample_rate > 0 ? es->sample_rate : adhp->config.ctx->sample_rate;
            current_frame_length = frame_length;
        }
        decoded_pcm_sample_count += frame_length;
        resampled_sample_count = count_sequence_output_pcm_samples(decoded_pcm_sample_count, current_sample_rate, output_sample_rate);
        next_frame_pos = prior_sequences_resampled_count + resampled_sample_count;
        if (start_frame_pos < next_frame_pos)
            break;
        ++frame_number;
    } while (frame_number <= adhp->frame_count);
    *start_offset = start_frame_pos - current_frame_pos;
    *start_offset = av_rescale_rnd(*start_offset, current_sample_rate, output_sample_rate, AV_ROUND_UP);
    *start_offset += get_preroll_samples(adhp, av_rescale(skip_decoded_samples, current_sample_rate, output_sample_rate), &frame_number);
    return frame_number;
}

uint64_t libavsmash_audio_get_pcm_samples(
    libavsmash_audio_decode_handler_t* adhp, libavsmash_audio_output_handler_t* aohp, void* buf, int64_t start, int64_t wanted_length)
{
    codec_configuration_t* config = &adhp->config;
    if (config->error)
        return 0;
    uint32_t frame_number;
    uint64_t output_length = 0;
    enum audio_output_flag output_flags;
    aohp->request_length = wanted_length;
    if (start > 0 && start == adhp->next_pcm_sample_number) {
        frame_number = adhp->last_frame_number;
        output_flags = AUDIO_OUTPUT_NO_FLAGS;
        output_length += output_pcm_samples_from_buffer(aohp, adhp->frame_buffer, (uint8_t**)&buf, &output_flags);
        if (output_flags & AUDIO_OUTPUT_ENOUGH)
            goto audio_out;
        if (adhp->packet.size <= 0)
            ++frame_number;
        aohp->output_sample_offset = 0;
    } else {
        /* Seek audio stream. */
        if (flush_resampler_buffers(aohp->swr_ctx) < 0) {
            config->error = 1;
            lw_log_show(&config->lh, LW_LOG_FATAL,
                "Failed to flush resampler buffers.\n"
                "It is recommended you reopen the file.");
            return 0;
        }
        libavsmash_flush_buffers(config);
        if (config->error)
            return 0;
        adhp->next_pcm_sample_number = 0;
        adhp->last_frame_number = 0;
        uint64_t start_frame_pos;
        if (start >= 0)
            start_frame_pos = start;
        else {
            uint64_t silence_length = -start;
            put_silence_audio_samples((int)(silence_length * aohp->output_block_align), aohp->output_bits_per_sample == 8, (uint8_t**)&buf);
            output_length += silence_length;
            aohp->request_length -= silence_length;
            start_frame_pos = 0;
        }
        start_frame_pos += aohp->skip_decoded_samples;
        frame_number = find_start_audio_frame(
            adhp, aohp->output_sample_rate, aohp->skip_decoded_samples, start_frame_pos, &aohp->output_sample_offset);
    }
    do {
        AVPacket* pkt = &adhp->packet;
        if (frame_number > adhp->frame_count) {
            if (config->delay_count || !(output_flags & AUDIO_OUTPUT_ENOUGH)) {
                /* Null packet */
                av_packet_unref(pkt);
                if (config->delay_count)
                    config->delay_count -= 1;
            } else
                goto audio_out;
        } else if (pkt->size <= 0)
            /* Getting an audio packet must be after flushing all remaining samples in resampler's FIFO buffer. */
            while (get_sample(adhp->root, adhp->track_id, frame_number, config, pkt) == 2)
                if (config->update_pending)
                    /* Update the decoder configuration. */
                    update_configuration(adhp->root, adhp->track_id, config);
        /* Decode and output from an audio packet. */
        output_flags = AUDIO_OUTPUT_NO_FLAGS;
        output_length += output_pcm_samples_from_packet(aohp, config->ctx, pkt, adhp->frame_buffer, (uint8_t**)&buf, &output_flags);
        if (output_flags & AUDIO_DECODER_DELAY)
            ++config->delay_count;
        if (output_flags & AUDIO_RECONFIG_FAILURE) {
            config->error = 1;
            lw_log_show(&config->lh, LW_LOG_FATAL,
                "Failed to reconfigure resampler.\n"
                "It is recommended you reopen the file.");
            goto audio_out;
        }
        if (output_flags & AUDIO_OUTPUT_ENOUGH)
            goto audio_out;
        if (output_flags & (AUDIO_DECODER_ERROR | AUDIO_DECODER_RECEIVED_PACKET))
            ++frame_number;
    } while (1);
audio_out:
    adhp->next_pcm_sample_number = start + output_length;
    adhp->last_frame_number = frame_number;
    return output_length;
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

int libavsmash_audio_setup_sample_count(libavsmash_audio_decode_handler_t* adhp, libavsmash_audio_output_handler_t* aohp, int skip_priming,
    int skip_tail, uint64_t* out_final_num_samples, char* error_msg, size_t error_msg_size)
{
    if (!adhp || !aohp || !out_final_num_samples) {
        if (error_msg)
            snprintf(error_msg, error_msg_size, "invalid internal arguments.");
        return -1;
    }
    if (aohp->output_sample_rate <= 0) {
        if (error_msg)
            snprintf(error_msg, error_msg_size, "invalid output sample rate.");
        return -1;
    }
    lsmash_root_t* root = libavsmash_audio_get_root(adhp);
    uint32_t track_id = libavsmash_audio_get_track_id(adhp);
    uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
    uint32_t priming_samples = 0;
    uint32_t padding_samples = 0;
    uint64_t duration_samples = 0;
    int have_itun_smpb = (libavsmash_audio_get_itun_smpb(root, &priming_samples, &padding_samples, &duration_samples) == 0);
    uint64_t total_codec_samples = 0;
    if (have_itun_smpb && (skip_priming || skip_tail))
        total_codec_samples = libavsmash_audio_count_total_codec_samples(adhp);
    uint64_t start_output_samples = 0;
    if (skip_priming) {
        if (have_itun_smpb) {
            if (priming_samples > total_codec_samples)
                priming_samples = 0;
            uint64_t priming_skip_output = 0;
            if (libavsmash_audio_convert_smpb_samples_to_output(adhp, aohp, priming_samples, &priming_skip_output, AV_ROUND_UP) < 0) {
                if (error_msg)
                    snprintf(error_msg, error_msg_size, "invalid SMPB priming information.");
                return -1;
            }
            libavsmash_audio_set_implicit_preroll(adhp);
            aohp->skip_decoded_samples = priming_skip_output;
            start_output_samples = priming_skip_output;
        } else {
            if (media_timescale == 0) {
                if (error_msg)
                    snprintf(error_msg, error_msg_size, "invalid audio media timescale.");
                return -1;
            }
            uint32_t ctd_shift = 0;
            if (lsmash_get_composition_to_decode_shift_from_media_timeline(root, track_id, &ctd_shift)) {
                if (error_msg)
                    snprintf(error_msg, error_msg_size, "failed to get the timeline shift.");
                return -1;
            }
            int64_t edit_start = get_start_time(root, track_id);
            if (edit_start < 0)
                edit_start = 0;
            uint64_t media_start_time = (uint64_t)edit_start + (uint64_t)ctd_shift;
            int64_t skip = 0;
            if (media_start_time <= (uint64_t)INT64_MAX)
                skip = av_rescale((int64_t)media_start_time, aohp->output_sample_rate, media_timescale);
            if (skip < 0)
                skip = 0;
            aohp->skip_decoded_samples = (uint64_t)skip;
            start_output_samples = (uint64_t)skip;
        }
    }
    uint64_t base_num_samples = libavsmash_audio_count_overall_pcm_samples(adhp, aohp->output_sample_rate, start_output_samples);
    uint64_t final_num_samples = base_num_samples;
    if (skip_tail) {
        if (libavsmash_audio_apply_tail_trim(adhp, aohp, have_itun_smpb, priming_samples, padding_samples, duration_samples, skip_priming,
                total_codec_samples, base_num_samples, &final_num_samples)
            < 0) {
            if (error_msg)
                snprintf(error_msg, error_msg_size, "failed to apply tail trimming.");
            return -1;
        }
    }
    if (final_num_samples == 0) {
        if (error_msg)
            snprintf(error_msg, error_msg_size, "no valid audio frame.");
        return -1;
    }
    *out_final_num_samples = final_num_samples;
    return 0;
}
