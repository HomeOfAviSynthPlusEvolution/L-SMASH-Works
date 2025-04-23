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
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "../common/video_output.h"
#include "colorspace.h"

typedef struct {
    int output_linesize;
    uint32_t output_frame_size;
    uint8_t* back_ground;
    uint8_t* another_chroma;
    uint32_t another_chroma_size;
    AVFrame* yuv444p16;
    func_convert_colorspace* convert_colorspace;
} au_video_output_handler_t;

int au_setup_video_rendering(lw_video_output_handler_t* vohp, video_option_t* opt, BITMAPINFOHEADER* format, int output_width,
    int output_height, enum AVPixelFormat input_pixel_format);

int convert_colorspace(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf);
