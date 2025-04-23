/*****************************************************************************
 * colorspace_simd.h
 *****************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: rigaya <rigaya34589@live.jp>
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

void convert_yuv16le_to_yc48_sse2(
    uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize, int output_rowsize, int output_height, int full_range);
void convert_yuv16le_to_yc48_sse4_1(
    uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize, int output_rowsize, int output_height, int full_range);
void convert_yv12i_to_yuy2_ssse3(uint8_t* buf, int buf_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height);

typedef void func_convert_yuv420ple_i_to_yuv444p16le(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height);

func_convert_yuv420ple_i_to_yuv444p16le convert_yuv420p9le_i_to_yuv444p16le_sse41;
func_convert_yuv420ple_i_to_yuv444p16le convert_yuv420p10le_i_to_yuv444p16le_sse41;
func_convert_yuv420ple_i_to_yuv444p16le convert_yuv420p16le_i_to_yuv444p16le_sse41;
