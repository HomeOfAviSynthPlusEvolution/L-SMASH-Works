/*****************************************************************************
 * video_output.h
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
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

#include "../common/video_output.h"

typedef int component_reorder_t;

typedef void func_make_black_background
(
    VSFrameRef  *vs_frame,
    const VSAPI *vsapi
);

typedef void func_make_frame
(
    lw_video_scaler_handler_t *vshp,
    AVFrame                   *av_picture,
    const component_reorder_t *component_reorder,
    VSFrameRef                *vs_frame,
    VSFrameContext            *frame_ctx,
    const VSAPI               *vsapi
);

typedef struct
{
    int                         variable_info;
    int                         direct_rendering;
    const component_reorder_t  *component_reorder[2];
    VSPresetFormat              vs_output_pixel_format;
    VSFrameRef                 *background_frame[2];
    func_make_black_background *make_black_background[2];
    func_make_frame            *make_frame[2];
    VSFrameContext             *frame_ctx;
    VSCore                     *core;
    const VSAPI                *vsapi;
} vs_video_output_handler_t;

VSPresetFormat get_vs_output_pixel_format( const char *format_name );

VSFrameRef *make_frame
(
    lw_video_output_handler_t *vohp,
    AVFrame                   *av_frame,
    int                        output_index
);

int vs_setup_video_rendering
(
    lw_video_output_handler_t *lw_vohp,
    AVCodecContext            *ctx,
    VSVideoInfo               *vi,
    VSMap                     *out,
    int                        width,
    int                        height
);

vs_video_output_handler_t *vs_allocate_video_output_handler
(
    lw_video_output_handler_t *vohp
);

void vs_set_frame_properties
(
    AVFrame        *av_frame,
    AVStream       *stream,
    int64_t         duration_num,
    int64_t         duration_den,
    VSFrameRef     *vs_frame,
    const VSAPI    *vsapi
);
