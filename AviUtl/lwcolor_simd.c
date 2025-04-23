/*****************************************************************************
 * lwcolor_simd.c
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
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

/* This file is available under an ISC license. */

#include <windows.h>

#include "../common/lwsimd.h"

#include "color.h"

#ifndef BYTE
typedef unsigned char BYTE;
#endif
#ifndef USHORT
typedef unsigned short USHORT;
#endif

#ifdef __GNUC__
#pragma GCC target("sse4.1")
#endif
#include <smmintrin.h>

static LW_FORCEINLINE void fill_rgb_buffer_sse41(BYTE* rgb_buffer, BYTE* lw48_ptr)
{
    static const USHORT LW_ALIGN(16) PW_32768[8] = { 32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768 };
    static const short LW_ALIGN(16) PW_28672[8] = { 28672, 28672, 28672, 28672, 28672, 28672, 28672, 28672 };
    static const short LW_ALIGN(16) PW_9539[8] = { 9539, 9539, 9539, 9539, 9539, 9539, 9539, 9539 };
    static const short LW_ALIGN(16) PW_13074[8] = { 13074, 13074, 13074, 13074, 13074, 13074, 13074, 13074 };
    static const short LW_ALIGN(16) PW_16531[8] = { 16531, 16531, 16531, 16531, 16531, 16531, 16531, 16531 };
    static const short LW_ALIGN(16) PW_M3203_M6808[8] = { -3203, -6808, -3203, -6808, -3203, -6808, -3203, -6808 };
    static const int LW_ALIGN(16) PD_1_20[4] = { (1 << 20), (1 << 20), (1 << 20), (1 << 20) };
    static const char LW_ALIGN(16) LW48_SHUFFLE[3][16] = { { 0, 1, 6, 7, 12, 13, 2, 3, 8, 9, 14, 15, 4, 5, 10, 11 },
        { 2, 3, 8, 9, 14, 15, 4, 5, 10, 11, 0, 1, 6, 7, 12, 13 }, { 4, 5, 10, 11, 0, 1, 6, 7, 12, 13, 2, 3, 8, 9, 14, 15 } };
    __m128i x0, x1, x2, x3, x4, x5, x6, x7;
    x5 = _mm_loadu_si128((__m128i*)(lw48_ptr + 0));
    x6 = _mm_loadu_si128((__m128i*)(lw48_ptr + 16));
    x7 = _mm_loadu_si128((__m128i*)(lw48_ptr + 32));

    x0 = _mm_blend_epi16(x5, x6, 0x80 + 0x10 + 0x02);
    x0 = _mm_blend_epi16(x0, x7, 0x20 + 0x04);

    x1 = _mm_blend_epi16(x5, x6, 0x20 + 0x04);
    x1 = _mm_blend_epi16(x1, x7, 0x40 + 0x08 + 0x01);

    x2 = _mm_blend_epi16(x5, x6, 0x40 + 0x08 + 0x01);
    x2 = _mm_blend_epi16(x2, x7, 0x80 + 0x10 + 0x02);

    x0 = _mm_shuffle_epi8(x0, _mm_load_si128((__m128i*)LW48_SHUFFLE[0])); /* Y  */
    x1 = _mm_shuffle_epi8(x1, _mm_load_si128((__m128i*)LW48_SHUFFLE[1])); /* Cb */
    x2 = _mm_shuffle_epi8(x2, _mm_load_si128((__m128i*)LW48_SHUFFLE[2])); /* Cr */

    x0 = _mm_sub_epi16(x0, _mm_load_si128((__m128i*)PW_32768));
    x1 = _mm_sub_epi16(x1, _mm_load_si128((__m128i*)PW_32768));
    x2 = _mm_sub_epi16(x2, _mm_load_si128((__m128i*)PW_32768));

    /* y_tmp = ((y - 4096) * 9539) */
    /*       = ((y - 32768) + (32768 - 4096)) * 9539 */
    /*       = ((y - 32768) * 9539 + 28672 * 9539 */
    x3 = _mm_unpacklo_epi16(x0, _mm_load_si128((__m128i*)PW_28672));
    x4 = _mm_unpackhi_epi16(x0, _mm_load_si128((__m128i*)PW_28672));
    x3 = _mm_madd_epi16(x3, _mm_load_si128((__m128i*)PW_9539));
    x4 = _mm_madd_epi16(x4, _mm_load_si128((__m128i*)PW_9539));

    /* G = ((y_tmp + ((cb-32768) * -3203) + ((cr-32768) * -6808)) + (1<<20)) >> 21 */
    x5 = _mm_unpacklo_epi16(x1, x2);
    x6 = _mm_unpackhi_epi16(x1, x2);
    x5 = _mm_madd_epi16(x5, _mm_load_si128((__m128i*)PW_M3203_M6808));
    x6 = _mm_madd_epi16(x6, _mm_load_si128((__m128i*)PW_M3203_M6808));
    x5 = _mm_add_epi32(x5, x3);
    x6 = _mm_add_epi32(x6, x4);
    x5 = _mm_add_epi32(x5, _mm_load_si128((__m128i*)PD_1_20));
    x6 = _mm_add_epi32(x6, _mm_load_si128((__m128i*)PD_1_20));
    x5 = _mm_srai_epi32(x5, 21);
    x6 = _mm_srai_epi32(x6, 21);
    x5 = _mm_packs_epi32(x5, x6);
    _mm_store_si128((__m128i*)(rgb_buffer + 16), x5);

    /* R = ((y_tmp + ((cr-32768) * 13074) + (1<<20)) >> 21 */
    x0 = _mm_mullo_epi16(x2, _mm_load_si128((__m128i*)PW_13074));
    x7 = _mm_mulhi_epi16(x2, _mm_load_si128((__m128i*)PW_13074));
    x6 = _mm_unpacklo_epi16(x0, x7);
    x7 = _mm_unpackhi_epi16(x0, x7);
    x6 = _mm_add_epi32(x6, x3);
    x7 = _mm_add_epi32(x7, x4);
    x6 = _mm_add_epi32(x6, _mm_load_si128((__m128i*)PD_1_20));
    x7 = _mm_add_epi32(x7, _mm_load_si128((__m128i*)PD_1_20));
    x6 = _mm_srai_epi32(x6, 21);
    x7 = _mm_srai_epi32(x7, 21);
    x6 = _mm_packs_epi32(x6, x7);
    _mm_store_si128((__m128i*)(rgb_buffer + 32), x6);

    /* B = ((y_tmp + ((cb-32768) * 16531) + (1<<20)) >> 21 */
    x2 = _mm_mullo_epi16(x1, _mm_load_si128((__m128i*)PW_16531));
    x7 = _mm_mulhi_epi16(x1, _mm_load_si128((__m128i*)PW_16531));
    x0 = _mm_unpacklo_epi16(x2, x7);
    x7 = _mm_unpackhi_epi16(x2, x7);
    x0 = _mm_add_epi32(x0, x3);
    x7 = _mm_add_epi32(x7, x4);
    x0 = _mm_add_epi32(x0, _mm_load_si128((__m128i*)PD_1_20));
    x7 = _mm_add_epi32(x7, _mm_load_si128((__m128i*)PD_1_20));
    x0 = _mm_srai_epi32(x0, 21);
    x7 = _mm_srai_epi32(x7, 21);
    x7 = _mm_packs_epi32(x0, x7);
    _mm_store_si128((__m128i*)(rgb_buffer + 0), x7);
}

void LW_FUNC_ALIGN convert_lw48_to_rgb24_sse41(int thread_id, int thread_num, void* param1, void* param2)
{
    /* LW48 -> RGB24 using SSE4.1 */
    COLOR_PROC_INFO* cpip = (COLOR_PROC_INFO*)param1;
    int start = (cpip->h * thread_id) / thread_num;
    int end = (cpip->h * (thread_id + 1)) / thread_num;
    int w = cpip->w;
    int rgb_linesize = (w * 3 + 3) & ~3;
    BYTE* ycp_line = (BYTE*)cpip->ycp + (end - 1) * cpip->line_size;
    BYTE* pixel_line = (BYTE*)cpip->pixelp + (cpip->h - end) * rgb_linesize;
    BYTE LW_ALIGN(16) rgb_buffer[96];
    for (int y = start; y < end; y++) {
        BYTE* lw48_ptr = (BYTE*)ycp_line;
        BYTE* rgb_ptr = pixel_line;
        for (int x = 0, i_step = 0; x < w; x += i_step, lw48_ptr += i_step * 6, rgb_ptr += i_step * 3) {
            static const char LW_ALIGN(16) RGB_SHUFFLE[9][16] = {
                { 0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1, -1, 5 },
                { -1, 0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1, -1 },
                { -1, -1, 0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1 },
                { -1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1, 10, -1 },
                { 5, -1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1, 10 },
                { -1, 5, -1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1 },
                { -1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15, -1, -1 },
                { -1, -1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15, -1 },
                { 10, -1, -1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15 },
            };
            fill_rgb_buffer_sse41(rgb_buffer + 0, lw48_ptr + 0);
            fill_rgb_buffer_sse41(rgb_buffer + 48, lw48_ptr + 48);

            __m128i xB = _mm_packus_epi16(_mm_load_si128((__m128i*)(rgb_buffer + 0)), _mm_load_si128((__m128i*)(rgb_buffer + 48)));
            __m128i xG = _mm_packus_epi16(_mm_load_si128((__m128i*)(rgb_buffer + 16)), _mm_load_si128((__m128i*)(rgb_buffer + 64)));
            __m128i xR = _mm_packus_epi16(_mm_load_si128((__m128i*)(rgb_buffer + 32)), _mm_load_si128((__m128i*)(rgb_buffer + 80)));

            __m128i x0, x1, x2, x3, x4;
            x0 = _mm_shuffle_epi8(xB, _mm_load_si128((__m128i*)RGB_SHUFFLE[0]));
            x3 = _mm_shuffle_epi8(xG, _mm_load_si128((__m128i*)RGB_SHUFFLE[1]));
            x4 = _mm_shuffle_epi8(xR, _mm_load_si128((__m128i*)RGB_SHUFFLE[2]));
            x0 = _mm_or_si128(x0, x3);
            x0 = _mm_or_si128(x0, x4);
            x1 = _mm_shuffle_epi8(xB, _mm_load_si128((__m128i*)RGB_SHUFFLE[3]));
            x3 = _mm_shuffle_epi8(xG, _mm_load_si128((__m128i*)RGB_SHUFFLE[4]));
            x4 = _mm_shuffle_epi8(xR, _mm_load_si128((__m128i*)RGB_SHUFFLE[5]));
            x1 = _mm_or_si128(x1, x3);
            x1 = _mm_or_si128(x1, x4);
            x2 = _mm_shuffle_epi8(xB, _mm_load_si128((__m128i*)RGB_SHUFFLE[6]));
            x3 = _mm_shuffle_epi8(xG, _mm_load_si128((__m128i*)RGB_SHUFFLE[7]));
            x4 = _mm_shuffle_epi8(xR, _mm_load_si128((__m128i*)RGB_SHUFFLE[8]));
            x2 = _mm_or_si128(x2, x3);
            x2 = _mm_or_si128(x2, x4);
            _mm_storeu_si128((__m128i*)(rgb_ptr + 0), x0);
            _mm_storeu_si128((__m128i*)(rgb_ptr + 16), x1);
            _mm_storeu_si128((__m128i*)(rgb_ptr + 32), x2);

            int remain = w - x;
            i_step = (remain >= 16);
            i_step = (i_step << 4) + (remain & ((~(0 - i_step)) & 0x0f));
        }
        ycp_line -= cpip->line_size;
        pixel_line += rgb_linesize;
    }
}

void LW_FUNC_ALIGN convert_lw48_to_yuy2_sse41(int thread_id, int thread_num, void* param1, void* param2)
{
    /* LW48 -> YUY2 using SSE4.1 */
    COLOR_PROC_INFO* cpip = (COLOR_PROC_INFO*)param1;
    int start = (cpip->h * thread_id) / thread_num;
    int end = (cpip->h * (thread_id + 1)) / thread_num;
    int w = cpip->w;
    BYTE* ycp_line = (BYTE*)cpip->ycp + start * cpip->line_size;
    BYTE* pixel_line = (BYTE*)cpip->pixelp + start * w * 2;
    __m128i x0, x1, x2, x3, x5, x6, x7;
    static const char LW_ALIGN(16) SHUFFLE_Y[16] = { 0, 1, 6, 7, 12, 13, 2, 3, 8, 9, 14, 15, 4, 5, 10, 11 };
    for (int y = start; y < end; y++) {
        BYTE* ycp = ycp_line;
        BYTE* yuy2_ptr = pixel_line;
        for (int x = 0, i_step = 0; x < w; x += i_step, ycp += i_step * 6, yuy2_ptr += i_step * 2) {
            x5 = _mm_loadu_si128((__m128i*)(ycp + 0));
            x6 = _mm_loadu_si128((__m128i*)(ycp + 16));
            x7 = _mm_loadu_si128((__m128i*)(ycp + 32));

            x0 = _mm_blend_epi16(x5, x6, 0x80 + 0x10 + 0x02);
            x0 = _mm_blend_epi16(x0, x7, 0x20 + 0x04);

            x1 = _mm_blend_epi16(x5, x6, 0x40 + 0x20 + 0x01);
            x1 = _mm_blend_epi16(x1, x7, 0x10 + 0x08);

            x0 = _mm_shuffle_epi8(x0, _mm_load_si128((__m128i*)SHUFFLE_Y));
            x1 = _mm_alignr_epi8(x1, x1, 2);
            x1 = _mm_shuffle_epi32(x1, _MM_SHUFFLE(1, 2, 3, 0));

            x0 = _mm_srli_epi16(x0, 8);
            x1 = _mm_srli_epi16(x1, 8);

            x5 = _mm_loadu_si128((__m128i*)(ycp + 48));
            x6 = _mm_loadu_si128((__m128i*)(ycp + 64));
            x7 = _mm_loadu_si128((__m128i*)(ycp + 80));

            x2 = _mm_blend_epi16(x5, x6, 0x80 + 0x10 + 0x02);
            x2 = _mm_blend_epi16(x2, x7, 0x20 + 0x04);

            x3 = _mm_blend_epi16(x5, x6, 0x40 + 0x20 + 0x01);
            x3 = _mm_blend_epi16(x3, x7, 0x10 + 0x08);

            x2 = _mm_shuffle_epi8(x2, _mm_load_si128((__m128i*)SHUFFLE_Y));
            x3 = _mm_alignr_epi8(x3, x3, 2);
            x3 = _mm_shuffle_epi32(x3, _MM_SHUFFLE(1, 2, 3, 0));

            x2 = _mm_srli_epi16(x2, 8);
            x3 = _mm_srli_epi16(x3, 8);

            x0 = _mm_packus_epi16(x0, x2);
            x1 = _mm_packus_epi16(x1, x3);

            _mm_storeu_si128((__m128i*)(yuy2_ptr + 0), _mm_unpacklo_epi8(x0, x1));
            _mm_storeu_si128((__m128i*)(yuy2_ptr + 16), _mm_unpackhi_epi8(x0, x1));

            int remain = w - x;
            i_step = (remain >= 16);
            i_step = (i_step << 4) + (remain & ((~(0 - i_step)) & 0x0f));
        }
        ycp_line += cpip->line_size;
        pixel_line += w * 2;
    }
}
