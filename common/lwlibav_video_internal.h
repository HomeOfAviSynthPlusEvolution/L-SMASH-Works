/*****************************************************************************
 * lwlibav_video_internal.h
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

#ifndef LWLIBAV_VIDEO_INTERNAL_H
#define LWLIBAV_VIDEO_INTERNAL_H

#define LW_VFRAME_FLAG_KEY 0x1
#define LW_VFRAME_FLAG_LEADING 0x2
#define LW_VFRAME_FLAG_CORRUPT 0x4
#define LW_VFRAME_FLAG_INVISIBLE 0x8
#define LW_VFRAME_FLAG_COUNTERPART_MISSING 0x10

typedef enum lw_field_info {
    LW_FIELD_INFO_UNKNOWN = 0, /* unknown */
    LW_FIELD_INFO_TOP, /* top field first or top field coded */
    LW_FIELD_INFO_BOTTOM, /* bottom field first or bottom field coded */
} lw_field_info_t;

typedef struct {
    int64_t pts; /* presentation timestamp */
    int64_t dts; /* decoding timestamp */
    int64_t file_offset; /* offset from the beginning of file */
    uint32_t sample_number; /* unique value in decoding order */
    int extradata_index; /* index of extradata to decode this frame */
    int flags; /* a combination of LW_VFRAME_FLAG_*s */
    int pict_type; /* may be stored as enum AVPictureType */
    int poc; /* Picture Order Count */
    int repeat_pict;
    lw_field_info_t field_info;
} video_frame_info_t;

typedef struct {
    uint32_t decoding_to_presentation;
} order_converter_t;

struct lwlibav_video_decode_handler_tag {
    /* common */
    AVFormatContext* format;
    int stream_index;
    int error;
    lw_log_handler_t lh;
    lwlibav_extradata_handler_t exh;
    AVCodecContext* ctx;
    AVIndexEntry* index_entries;
    int index_entries_count;
    int lw_seek_flags;
    int av_seek_flags;
    int dv_in_avi; /* unused */
    enum AVCodecID codec_id;
    const char** preferred_decoder_names;
    int* prefer_hw_decoder;
    AVRational time_base;
    uint32_t frame_count;
    AVFrame* frame_buffer;
    video_frame_info_t* frame_list; /* stored in presentation order */
    const char* ff_options;
    double drc; /* dummy */
    AVBufferRef* hw_device_ctx;
    /* */
    uint32_t forward_seek_threshold;
    int seek_mode;
    int max_width;
    int max_height;
    int initial_width;
    int initial_height;
    enum AVPixelFormat initial_pix_fmt;
    enum AVColorSpace initial_colorspace;
    AVPacket packet;
    order_converter_t* order_converter; /* maps of decoding to presentation stored in decoding order */
    uint8_t* keyframe_list; /* keyframe list stored in decoding order */
    uint32_t last_half_frame; /* The last frame consists of complementary field coded picture pair
                               * if set to non-zero, otherwise single frame coded picture. */
    uint32_t last_frame_number; /* the number of the last requested frame */
    uint32_t last_rap_number; /* the number of the last random accessible picture */
    uint32_t last_fed_picture_number; /* the number of the last picture fed to the decoder
                                       * This number could be larger than frame_count to handle flush. */
    uint32_t first_valid_frame_number;
    AVFrame* first_valid_frame; /* the frame buffer
                                 * where the first valid frame data is stored */
    AVFrame* last_req_frame; /* the pointer to the frame buffer
                              * where the last requested frame data is stored */
    AVFrame* last_dec_frame; /* the pointer to the frame buffer
                              * where the last output frame data from the decoder is stored */
    AVFrame* movable_frame_buffer; /* the frame buffer
                                    * where the decoder outputs temporally stored frame data */
    int64_t stream_duration;
    int64_t min_ts;
    uint32_t last_ts_frame_number;
    AVRational actual_time_base;
    int strict_cfr;
    int reuse_pkt;
};

#endif // !LWLIBAV_VIDEO_INTERNAL_H
