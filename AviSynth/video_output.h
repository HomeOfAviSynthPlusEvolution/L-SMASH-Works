/*****************************************************************************
 * video_output.h
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

#include "../common/video_output.h"

typedef struct
{
    uint8_t *data    [4];
    int      linesize[4];
} as_picture_t;

typedef void func_make_black_background
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
);

typedef int func_make_frame
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
);

typedef struct
{
    func_make_black_background *make_black_background;
    func_make_frame            *make_frame;
    IScriptEnvironment         *env;
    VideoInfo                  *vi;
    int                         bitdepth_minus_8;
    int                         sub_width;
    int                         sub_height;
    as_picture_t                scaled;
} as_video_output_handler_t;

typedef struct
{
    PVideoFrame as_frame_buffer;
} as_video_buffer_handler_t;

enum AVPixelFormat get_av_output_pixel_format
(
    const char *format_name
);

int make_frame
(
    lw_video_output_handler_t *vohp,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame,
    IScriptEnvironment        *env
);

void as_free_video_output_handler
(
    void *private_handler
);

void as_setup_video_rendering
(
    lw_video_output_handler_t *vohp,
    AVCodecContext            *ctx,
    const char                *filter_name,
    int                        direct_rendering,
    enum AVPixelFormat         output_pixel_format,
    int                        output_width,
    int                        output_height
);
