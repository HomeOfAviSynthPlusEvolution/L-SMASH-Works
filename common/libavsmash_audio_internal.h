/*****************************************************************************
 * libavsmash_audio_internal.h
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

#ifndef LIBAVSMASH_AUDIO_INTERNAL_H
#define LIBAVSMASH_AUDIO_INTERNAL_H

#include "libavsmash.h"

struct libavsmash_audio_decode_handler_tag
{
    lsmash_root_t        *root;
    uint32_t              track_id;
    codec_configuration_t config;
    AVFrame              *frame_buffer;
    AVPacket              packet;
    uint64_t              pcm_sample_count;
    uint64_t              next_pcm_sample_number;
    uint32_t              last_frame_number;
    uint32_t              frame_count;
    int                   implicit_preroll;
    uint32_t              media_timescale;
    uint64_t              media_duration;   /* unused */
    uint64_t              min_cts;
};

#endif // !LIBAVSMASH_AUDIO_INTERNAL_H
