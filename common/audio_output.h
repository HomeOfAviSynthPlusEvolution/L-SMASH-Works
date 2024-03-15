/*****************************************************************************
 * audio_output.h
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

typedef struct
{
    SwrContext*             swr_ctx;
    uint8_t                *resampled_buffer;
    int                     resampled_buffer_size;
    int                     input_planes;
    AVChannelLayout         input_channel_layout;
    enum AVSampleFormat     input_sample_format;
    int                     input_sample_rate;
    int                     input_block_align;
    AVChannelLayout         output_channel_layout;
    enum AVSampleFormat     output_sample_format;
    int                     output_sample_rate;
    int                     output_block_align;
    int                     output_bits_per_sample;
    int                     s24_output;
    uint64_t                request_length;
    uint64_t                skip_decoded_samples;   /* Upsampling by the decoder is considered. */
    uint64_t                output_sample_offset;
} lw_audio_output_handler_t;

enum audio_output_flag
{
    AUDIO_OUTPUT_NO_FLAGS         = 0,
    AUDIO_OUTPUT_ENOUGH           = 1 << 0,
    AUDIO_DECODER_DELAY           = 1 << 1,
    AUDIO_DECODER_ERROR           = 1 << 2,
    AUDIO_RECONFIG_FAILURE        = 1 << 3,
    AUDIO_DECODER_RECEIVED_PACKET = 1 << 4,
};
CPP_DEFINE_OR_SUBSTITUTE_OPERATOR( enum audio_output_flag )

uint64_t output_pcm_samples_from_buffer
(
    lw_audio_output_handler_t *aohp,
    AVFrame                   *frame_buffer,
    uint8_t                  **output_buffer,
    enum audio_output_flag    *output_flags
);

uint64_t output_pcm_samples_from_packet
(
    lw_audio_output_handler_t *aohp,
    AVCodecContext            *ctx,
    AVPacket                  *pkt,
    AVFrame                   *frame_buffer,
    uint8_t                  **output_buffer,
    enum audio_output_flag    *output_flags
);

void lw_cleanup_audio_output_handler
(
    lw_audio_output_handler_t *aohp
);
