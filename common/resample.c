/*****************************************************************************
 * resample.c / resample.cpp
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

#include "cpp_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "resample.h"

int resample_s32_to_s24( uint8_t **out_data, uint8_t *in_data, int data_size )
{
    /* Assume little endianess here.
     *   in[0]  in[1]  in[2]  in[3]  in[4]  in[5]   in[6]  in[7] ...
     *      X  out[0] out[1] out[2]     X  out[3]  out[4] out[5] ... */
    data_size &= ~3;
    int resampled_size = 0;
    for( int i = 0; i < data_size; i += 4 )
    {
        *((*out_data) + resampled_size    ) = in_data[i + 1];
        *((*out_data) + resampled_size + 1) = in_data[i + 2];
        *((*out_data) + resampled_size + 2) = in_data[i + 3];
        resampled_size += 3;
    }
    *out_data += resampled_size;
    return resampled_size;
}

int flush_resampler_buffers(SwrContext *swr )
{
    swr_close( swr );
    return swr_init( swr ) < 0 ? -1 : 0;
}

int update_resampler_configuration(SwrContext *swr,
                                    uint64_t out_channel_layout, int out_sample_rate, enum AVSampleFormat out_sample_fmt,
                                    uint64_t  in_channel_layout, int  in_sample_rate, enum AVSampleFormat  in_sample_fmt,
                                    int *input_planes, int *input_block_align )
{
    /* Reopen the resampler. */
    swr_close( swr );
    av_opt_set_int( swr, "in_channel_layout",   in_channel_layout,  0 );
    av_opt_set_int( swr, "in_sample_fmt",       in_sample_fmt,      0 );
    av_opt_set_int( swr, "in_sample_rate",      in_sample_rate,     0 );
    av_opt_set_int( swr, "out_channel_layout",  out_channel_layout, 0 );
    av_opt_set_int( swr, "out_sample_fmt",      out_sample_fmt,     0 );
    av_opt_set_int( swr, "out_sample_rate",     out_sample_rate,    0 );
    av_opt_set_int( swr, "internal_sample_fmt", AV_SAMPLE_FMT_FLTP, 0 );
    if( swr_init( swr ) < 0 )
        return -1;
    /* Set up the number of planes and the block alignment of input audio frame. */
    int input_channels = av_get_channel_layout_nb_channels( in_channel_layout );
    if( av_sample_fmt_is_planar( in_sample_fmt ) )
    {
        *input_planes      = input_channels;
        *input_block_align = av_get_bytes_per_sample( in_sample_fmt );
    }
    else
    {
        *input_planes      = 1;
        *input_block_align = av_get_bytes_per_sample( in_sample_fmt ) * input_channels;
    }
    return 0;
}

int resample_audio(SwrContext *swr, audio_samples_t *out, audio_samples_t *in )
{
    /* Don't call this function over different block aligns. */
    uint8_t *out_orig = *out->data;
    int out_channels = get_channel_layout_nb_channels( out->channel_layout );
    int block_align = av_get_bytes_per_sample( out->sample_format ) * out_channels;
    int request_sample_count = out->sample_count;
    if( swr_get_delay( swr,  request_sample_count) > 0 )
    {
        int resampled_count = swr_convert( swr, out->data, request_sample_count, NULL, 0 );
        if( resampled_count < 0 )
            return 0;
        request_sample_count -= resampled_count;
        *out->data += resampled_count * block_align;
    }
    uint8_t **in_data = in->sample_count > 0 ? in->data : NULL;
	const uint8_t **indata = (const uint8_t**)in_data;
    int resampled_count = swr_convert( swr, out->data, request_sample_count,
                                                     indata,  in->sample_count );
    if( resampled_count < 0 )
        return 0;
    *out->data += resampled_count * block_align;
    return *out->data - out_orig;
}
