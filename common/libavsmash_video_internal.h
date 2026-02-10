/*****************************************************************************
 * libavsmash_video_internal.h
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

#ifndef LIBAVSMASH_VIDEO_INTERNAL_H
#define LIBAVSMASH_VIDEO_INTERNAL_H

#include "libavsmash.h"

#define SEEK_MODE_NORMAL 0
#define SEEK_MODE_UNSAFE 1
#define SEEK_MODE_AGGRESSIVE 2

typedef struct {
    uint32_t composition_to_decoding;
} order_converter_t;

struct libavsmash_video_decode_handler_tag {
    lsmash_root_t* root;
    uint32_t track_id;
    codec_configuration_t config;
    AVFrame* frame_buffer;
    AVPacket packet;
    uint32_t forward_seek_threshold;
    int seek_mode;
    order_converter_t* order_converter;
    uint8_t* keyframe_list;
    uint32_t sample_count;
    uint32_t last_sample_number;
    uint32_t last_rap_number;
    uint32_t first_valid_frame_number;
    AVFrame* first_valid_frame;
    uint32_t media_timescale;
    uint64_t media_duration;
    uint64_t min_cts;
    AVFrame* movable_frame_buffer; /* the frame buffer where the decoder outputs temporally stored frame data */
    int reuse_pkt;
};

#endif // !LIBAVSMASH_VIDEO_INTERNAL_H
