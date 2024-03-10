/*****************************************************************************
 * decode.h
 *****************************************************************************
 * Copyright (C) 2012-2016 L-SMASH Works project
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

static inline uint32_t get_decoder_delay
(
    AVCodecContext *ctx
)
{
    if (!strcmp(ctx->codec->name, "libdav1d"))
        return ctx->delay;
    else
        return ctx->has_b_frames + ((ctx->active_thread_type & FF_THREAD_FRAME) ? ctx->thread_count - 1 : 0);
}

const AVCodec *find_decoder
(
    enum AVCodecID           codec_id,
    const AVCodecParameters *codecpar,
    const char             **preferred_decoder_names,
    const int                prefer_hw_decoder
);

int open_decoder
(
    AVCodecContext         **ctx,
    const AVCodecParameters *codecpar,
    const AVCodec           *codec,
    const int                thread_count,
    const double             drc,
    const char              *ff_options
);

int find_and_open_decoder
(
    AVCodecContext         **ctx,
    const AVCodecParameters *codecpar,
    const char             **preferred_decoder_names,
    const int                prefer_hw_decoder,
    const int                thread_count,
    const double             drc,
    const char              *ff_options
);

int decode_video_packet
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int            *got_frame,
    AVPacket       *pkt
);

int decode_audio_packet
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int            *got_frame,
    AVPacket       *pkt
);
