/*****************************************************************************
 * lwcolor.h
 *****************************************************************************
 * Copyright (C) 2011-2015 L-SMASH Works project
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

#define YUY2_SIZE 2
#define RGB24_SIZE 3
#define RGBA_SIZE 4
#define YC48_SIZE 6
#define LW48_SIZE 6

/* LW48 pixel format / LW48 color space.
 * As a pixel format, a LW48 pixel consists of packed YUV 16:16:16, 48bpp 16Y 16Cb 16Cr, and
 * the 2-byte value for each Y/Cb/Cr component is stored as little-endian.
 * As a color space, the derivation of luminance and color-difference signals refers to Rec. ITU-R BT.601.
 *   - Luminance signal
 *     The black level corresponding to 4096 and the peak white level corresponding to 60160.
 *   - Color-difference signal
 *     The achromatic level corresponding to 32768 and the peak levels corresponding to 4096 and 61440. */
typedef struct {
    unsigned short y;
    unsigned short cb;
    unsigned short cr;
} PIXEL_LW48;

typedef enum {
    OUTPUT_YUY2 = 0,
    OUTPUT_RGB24 = 1,
    OUTPUT_RGBA = 2,
    OUTPUT_YC48 = 3,
    OUTPUT_LW48 = 4,
} output_colorspace_index;

typedef enum {
    OUTPUT_TAG_YUY2 = MAKEFOURCC('Y', 'U', 'Y', '2'),
    OUTPUT_TAG_RGB = 0x00000000,
    OUTPUT_TAG_RGBA = 0x00000000,
    OUTPUT_TAG_YC48 = MAKEFOURCC('Y', 'C', '4', '8'),
    OUTPUT_TAG_LW48 = MAKEFOURCC('L', 'W', '4', '8'),
} output_colorspace_tag;
