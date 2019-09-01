/*****************************************************************************
 * audio_output.cpp
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
 * However, when distributing its binary file, it will be under LGPL or GPL. */

#include "lsmashsource.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

#include "audio_output.h"

static inline enum AVSampleFormat as_decide_audio_output_sample_format( enum AVSampleFormat input_sample_format )
{
    /* Avisynth doesn't support IEEE double precision floating point format. */
    switch( input_sample_format )
    {
        case AV_SAMPLE_FMT_U8 :
        case AV_SAMPLE_FMT_U8P :
            return AV_SAMPLE_FMT_U8;
        case AV_SAMPLE_FMT_S16 :
        case AV_SAMPLE_FMT_S16P :
            return AV_SAMPLE_FMT_S16;
        case AV_SAMPLE_FMT_S32 :
        case AV_SAMPLE_FMT_S32P :
            return AV_SAMPLE_FMT_S32;
        default :
            return AV_SAMPLE_FMT_FLT;
    }
}

void as_setup_audio_rendering
(
    lw_audio_output_handler_t *aohp,
    AVCodecContext            *ctx,
    VideoInfo                 *vi,
    IScriptEnvironment        *env,
    const char                *filter_name,
    uint64_t                   channel_layout,
    int                        sample_rate
)
{
    /* Channel layout. */
    if( ctx->channel_layout == 0 )
        ctx->channel_layout = av_get_default_channel_layout( ctx->channels );
    if( channel_layout != 0 )
        aohp->output_channel_layout = channel_layout;
    /* Sample rate. */
    if( sample_rate > 0 )
        aohp->output_sample_rate = sample_rate;
    /* Decide output Bits Per Sample. */
    aohp->output_sample_format = as_decide_audio_output_sample_format( aohp->output_sample_format );
    if( aohp->output_sample_format == AV_SAMPLE_FMT_S32
     && (aohp->output_bits_per_sample == 0 || aohp->output_bits_per_sample == 24) )
    {
        /* 24bit signed integer output */
        aohp->s24_output             = 1;
        aohp->output_bits_per_sample = 24;
    }
    else
        aohp->output_bits_per_sample = av_get_bytes_per_sample( aohp->output_sample_format ) * 8;
    /* Set up the number of planes and the block alignment of decoded and output data. */
    int input_channels = av_get_channel_layout_nb_channels( ctx->channel_layout );
    if( av_sample_fmt_is_planar( ctx->sample_fmt ) )
    {
        aohp->input_planes      = input_channels;
        aohp->input_block_align = av_get_bytes_per_sample( ctx->sample_fmt );
    }
    else
    {
        aohp->input_planes      = 1;
        aohp->input_block_align = av_get_bytes_per_sample( ctx->sample_fmt ) * input_channels;
    }
    int output_channels = av_get_channel_layout_nb_channels( aohp->output_channel_layout );
    aohp->output_block_align = (output_channels * aohp->output_bits_per_sample) / 8;
    /* Set up resampler. */
    SwrContext *swr_ctx = aohp->swr_ctx;
    swr_ctx = swr_alloc();
    if( !swr_ctx )
        env->ThrowError( "%s: failed to swr_alloc.", filter_name );
    aohp->swr_ctx = swr_ctx;
    av_opt_set_int( swr_ctx, "in_channel_layout",   ctx->channel_layout,         0 );
    av_opt_set_int( swr_ctx, "in_sample_fmt",       ctx->sample_fmt,             0 );
    av_opt_set_int( swr_ctx, "in_sample_rate",      ctx->sample_rate,            0 );
    av_opt_set_int( swr_ctx, "out_channel_layout",  aohp->output_channel_layout, 0 );
    av_opt_set_int( swr_ctx, "out_sample_fmt",      aohp->output_sample_format,  0 );
    av_opt_set_int( swr_ctx, "out_sample_rate",     aohp->output_sample_rate,    0 );
    av_opt_set_int( swr_ctx, "internal_sample_fmt", AV_SAMPLE_FMT_FLTP,          0 );
    if( swr_init( swr_ctx ) < 0 )
        env->ThrowError( "%s: failed to open resampler.", filter_name );
    /* Set up AviSynth output format. */
    vi->nchannels                = output_channels;
    vi->audio_samples_per_second = aohp->output_sample_rate;
    switch ( aohp->output_sample_format )
    {
        case AV_SAMPLE_FMT_U8 :
        case AV_SAMPLE_FMT_U8P :
            vi->sample_type = SAMPLE_INT8;
            break;
        case AV_SAMPLE_FMT_S16 :
        case AV_SAMPLE_FMT_S16P :
            vi->sample_type = SAMPLE_INT16;
            break;
        case AV_SAMPLE_FMT_S32 :
        case AV_SAMPLE_FMT_S32P :
            vi->sample_type = aohp->s24_output ? SAMPLE_INT24 : SAMPLE_INT32;
            break;
        case AV_SAMPLE_FMT_FLT :
        case AV_SAMPLE_FMT_FLTP :
            vi->sample_type = SAMPLE_FLOAT;
            break;
        default :
            env->ThrowError( "%s: %s is not supported.", filter_name, av_get_sample_fmt_name( ctx->sample_fmt ) );
    }
}
