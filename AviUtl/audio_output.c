/*****************************************************************************
 * audio_output.c
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

/* This file is available under an ISC license.
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#define _USE_MATH_DEFINES

#include <math.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "audio_output.h"
#include "lwinput.h"

static inline enum AVSampleFormat au_decide_audio_output_sample_format(enum AVSampleFormat input_sample_format, int input_bits_per_sample)
{
    /* AviUtl doesn't support IEEE floating point format. */
    switch (input_sample_format) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return AV_SAMPLE_FMT_U8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return AV_SAMPLE_FMT_S16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return AV_SAMPLE_FMT_S32;
    default:
        if (input_bits_per_sample == 0)
            return AV_SAMPLE_FMT_S32;
        else if (input_bits_per_sample <= 8)
            return AV_SAMPLE_FMT_U8;
        else if (input_bits_per_sample <= 16)
            return AV_SAMPLE_FMT_S16;
        else
            return AV_SAMPLE_FMT_S32;
    }
}

static inline void au_opt_set_mix_level(SwrContext* swr_ctx, const char* opt, int value)
{
    av_opt_set_double(swr_ctx, opt, value == 71 ? M_SQRT1_2 : (value / 100.0), 0);
}

int au_setup_audio_rendering(lw_audio_output_handler_t* aohp, AVCodecContext* ctx, audio_option_t* opt, WAVEFORMATEX* format)
{
    /* Channel layout. */
    if (ctx->channel_layout == 0)
        ctx->channel_layout = av_get_default_channel_layout(ctx->channels);
    if (opt->channel_layout != 0)
        aohp->output_channel_layout = opt->channel_layout;
    /* Sample rate. */
    if (opt->sample_rate > 0)
        aohp->output_sample_rate = opt->sample_rate;
    /* Decide output sample format. */
    aohp->output_sample_format = au_decide_audio_output_sample_format(aohp->output_sample_format, aohp->output_bits_per_sample);
    if (aohp->output_sample_format == AV_SAMPLE_FMT_S32 && (aohp->output_bits_per_sample == 0 || aohp->output_bits_per_sample == 24)) {
        /* 24bit signed integer output */
        aohp->s24_output = 1;
        aohp->output_bits_per_sample = 24;
    } else
        aohp->output_bits_per_sample = av_get_bytes_per_sample(aohp->output_sample_format) * 8;
    /* Set up the number of planes and the block alignment of decoded and output data. */
    int input_channels = av_get_channel_layout_nb_channels(ctx->channel_layout);
    if (av_sample_fmt_is_planar(ctx->sample_fmt)) {
        aohp->input_planes = input_channels;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt);
    } else {
        aohp->input_planes = 1;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt) * input_channels;
    }
    int output_channels = av_get_channel_layout_nb_channels(aohp->output_channel_layout);
    aohp->output_block_align = (output_channels * aohp->output_bits_per_sample) / 8;
    /* Set up resampler. */
    SwrContext* swr_ctx = aohp->swr_ctx;
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        DEBUG_AUDIO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to swr_alloc.");
        return -1;
    }
    aohp->swr_ctx = swr_ctx;
    av_opt_set_int(swr_ctx, "in_channel_layout", ctx->channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_fmt", ctx->sample_fmt, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", aohp->output_channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_fmt", aohp->output_sample_format, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", aohp->output_sample_rate, 0);
    av_opt_set_int(swr_ctx, "internal_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
    au_opt_set_mix_level(swr_ctx, "center_mix_level", opt->mix_level[MIX_LEVEL_INDEX_CENTER]);
    au_opt_set_mix_level(swr_ctx, "surround_mix_level", opt->mix_level[MIX_LEVEL_INDEX_SURROUND]);
    au_opt_set_mix_level(swr_ctx, "lfe_mix_level", opt->mix_level[MIX_LEVEL_INDEX_LFE]);
    if (swr_init(swr_ctx) < 0) {
        DEBUG_AUDIO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to open resampler.");
        return -1;
    }
    /* Support of WAVEFORMATEXTENSIBLE is much restrictive on AviUtl, so we always use WAVEFORMATEX instead. */
    format->nChannels = output_channels;
    format->nSamplesPerSec = aohp->output_sample_rate;
    format->wBitsPerSample = aohp->output_bits_per_sample;
    format->nBlockAlign = aohp->output_block_align;
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->cbSize = 0;
    return 0;
}
