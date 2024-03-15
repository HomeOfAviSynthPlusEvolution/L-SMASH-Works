/*****************************************************************************
 * resample.h
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

#include <string.h>

typedef struct
{
    uint64_t            channel_layout;
    int                 sample_count;
    enum AVSampleFormat sample_format;
    uint8_t           **data;
} audio_samples_t;

static inline void put_silence_audio_samples( int silence_data_size, int is_u8, uint8_t **out_data )
{
    memset( *out_data, is_u8 ? 0x80 : 0x00, silence_data_size );
    *out_data += silence_data_size;
}

static inline int get_channel_layout_nb_channels( uint64_t channel_layout )
{
    AVChannelLayout output;
    av_channel_layout_from_mask(&output, channel_layout);
    int channels = output.nb_channels;
    av_channel_layout_uninit(&output);
    return channels > 0 ? channels : 1;
}

static inline int get_linesize( int channel_count, int sample_count, enum AVSampleFormat sample_format )
{
    int linesize;
    av_samples_get_buffer_size( &linesize, channel_count, sample_count, sample_format, 0 );
    return linesize;
}

int resample_s32_to_s24( uint8_t **out_data, uint8_t *in_data, int data_size );
int flush_resampler_buffers( SwrContext *swr );
int update_resampler_configuration( SwrContext *swr,
                                    AVChannelLayout* out_channel_layout, int out_sample_rate, enum AVSampleFormat out_sample_fmt,
                                    AVChannelLayout*  in_channel_layout, int  in_sample_rate, enum AVSampleFormat  in_sample_fmt,
                                    int *input_planes, int *input_block_align );
int resample_audio( SwrContext *swr, audio_samples_t *out, audio_samples_t *in );
