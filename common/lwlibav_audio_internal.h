/*****************************************************************************
 * lwlibav_audio_internal.h
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

typedef struct
{
    int64_t  pts;
    int64_t  dts;
    int64_t  file_offset;
    uint32_t sample_number;
    int      extradata_index;
    uint8_t  keyframe;
    int      length;
    int      sample_rate;
} audio_frame_info_t;

struct lwlibav_audio_decode_handler_tag
{
    /* common */
    AVFormatContext    *format;
    int                 stream_index;
    int                 error;
    lw_log_handler_t    lh;
    lwlibav_extradata_handler_t exh;
    AVCodecContext     *ctx;
    AVIndexEntry       *index_entries;
    int                 index_entries_count;
    int                 lw_seek_flags;
    int                 av_seek_flags;  /* unused */
    int                 dv_in_avi;      /* 1 = 'DV in AVI Type-1', 0 = otherwise */
    enum AVCodecID      codec_id;
    const char        **preferred_decoder_names;
    int                 prefer_hw_decoder;
    double              drc;
    AVRational          time_base;
    uint32_t            frame_count;
    AVFrame            *frame_buffer;
    audio_frame_info_t *frame_list;
    /* */
    AVPacket            packet;         /* for getting and freeing */
    AVPacket            alter_packet;   /* for consumed by the decoder instead of 'packet'. */
    uint32_t            frame_length;
    uint32_t            last_frame_number;
    uint64_t            pcm_sample_count;
    uint64_t            next_pcm_sample_number;
};
