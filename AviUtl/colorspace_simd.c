/*****************************************************************************
 * colorspace_simd.c
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

#include <stdint.h>

#include "../common/lwsimd.h"
#include "../common/utils.h"

#ifdef __GNUC__
#pragma GCC target("ssse3")
#endif
#include <tmmintrin.h>
/* SSSE3 version of func convert_yv12i_to_yuy2 */
void LW_FUNC_ALIGN convert_yv12i_to_yuy2_ssse3(
    uint8_t* buf, int buf_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    static const uint8_t LW_ALIGN(16) Array_5371[4][16] = {
        { 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3 },
        { 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1 },
        { 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7, 1, 7 },
        { 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5 },
    };
    static const int16_t LW_ALIGN(16) Array_4[8] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    uint8_t *ptr_y, *ptr_u, *ptr_v, *ptr_dst, *ptr_dst_fin;
    __m128i x0, x1, x2, x3, x4, x5, x6, x7;
    const int y_pitch = pic_linesize[0];
    const int uv_pitch = pic_linesize[1];
    uint8_t* y_line = pic_data[0];
    uint8_t* u_line = pic_data[1];
    uint8_t* v_line = pic_data[2];
    uint8_t* dst_line = buf;
    /* copy first 2 lines */
    uint8_t* dst_line_fin = buf + buf_linesize * 2;
    for (; dst_line < dst_line_fin; dst_line += buf_linesize, y_line += y_pitch, u_line += uv_pitch, v_line += uv_pitch) {
        ptr_dst = dst_line;
        ptr_y = y_line;
        ptr_u = u_line;
        ptr_v = v_line;
        ptr_dst_fin = ptr_dst + output_rowsize;
        for (; ptr_dst < ptr_dst_fin; ptr_dst += 64, ptr_y += 32, ptr_u += 16, ptr_v += 16) {
            x0 = _mm_loadu_si128((__m128i*)ptr_u);
            x1 = _mm_loadu_si128((__m128i*)ptr_v);
            x2 = _mm_unpacklo_epi8(x0, x1);
            x3 = _mm_unpackhi_epi8(x0, x1);

            x0 = _mm_loadu_si128((__m128i*)(ptr_y + 0));
            x1 = _mm_loadu_si128((__m128i*)(ptr_y + 16));
            x4 = _mm_unpacklo_epi8(x0, x2);
            x5 = _mm_unpackhi_epi8(x0, x2);
            x6 = _mm_unpacklo_epi8(x1, x3);
            x7 = _mm_unpackhi_epi8(x1, x3);
            _mm_storeu_si128((__m128i*)(ptr_dst + 0), x4);
            _mm_storeu_si128((__m128i*)(ptr_dst + 16), x5);
            _mm_storeu_si128((__m128i*)(ptr_dst + 32), x6);
            _mm_storeu_si128((__m128i*)(ptr_dst + 48), x7);
        }
    }

    /* 5,3,7,1 interlaced yv12 to yuy2 interpolation */
    u_line = pic_data[1];
    v_line = pic_data[2];
    dst_line_fin = buf + buf_linesize * (height - 2);
    for (; dst_line < dst_line_fin;
        dst_line += (buf_linesize << 2), y_line += (y_pitch << 2), u_line += (uv_pitch << 1), v_line += (uv_pitch << 1)) {
        for (int i = 0; i < 4; i++) {
            ptr_dst = dst_line + buf_linesize * i;
            ptr_y = y_line + y_pitch * i;
            ptr_u = u_line + uv_pitch * (i & 0x01);
            ptr_v = v_line + uv_pitch * (i & 0x01);
            ptr_dst_fin = ptr_dst + output_rowsize;
            for (; ptr_dst < ptr_dst_fin; ptr_dst += 64, ptr_y += 32, ptr_u += 16, ptr_v += 16) {
                x0 = _mm_loadu_si128((__m128i*)(ptr_u));
                x1 = _mm_loadu_si128((__m128i*)(ptr_v));
                x2 = _mm_unpacklo_epi8(x0, x1);
                x3 = _mm_unpackhi_epi8(x0, x1);

                x0 = _mm_loadu_si128((__m128i*)(ptr_u + (uv_pitch << 1)));
                x1 = _mm_loadu_si128((__m128i*)(ptr_v + (uv_pitch << 1)));
                x4 = _mm_unpacklo_epi8(x0, x1);
                x5 = _mm_unpackhi_epi8(x0, x1);

                x6 = _mm_unpacklo_epi8(x2, x4);
                x7 = _mm_unpackhi_epi8(x2, x4);

                x6 = _mm_maddubs_epi16(x6, _mm_load_si128((__m128i*)Array_5371[i]));
                x7 = _mm_maddubs_epi16(x7, _mm_load_si128((__m128i*)Array_5371[i]));

                x6 = _mm_add_epi16(x6, _mm_load_si128((__m128i*)Array_4));
                x7 = _mm_add_epi16(x7, _mm_load_si128((__m128i*)Array_4));

                x6 = _mm_srai_epi16(x6, 3);
                x7 = _mm_srai_epi16(x7, 3);

                x6 = _mm_packus_epi16(x6, x7);
                x7 = _mm_loadu_si128((__m128i*)(ptr_y + 0));

                x0 = _mm_unpacklo_epi8(x7, x6);
                x1 = _mm_unpackhi_epi8(x7, x6);

                _mm_storeu_si128((__m128i*)(ptr_dst + 0), x0);
                _mm_storeu_si128((__m128i*)(ptr_dst + 16), x1);

                x6 = _mm_unpacklo_epi8(x3, x5);
                x7 = _mm_unpackhi_epi8(x3, x5);

                x6 = _mm_maddubs_epi16(x6, _mm_load_si128((__m128i*)Array_5371[i]));
                x7 = _mm_maddubs_epi16(x7, _mm_load_si128((__m128i*)Array_5371[i]));

                x6 = _mm_add_epi16(x6, _mm_load_si128((__m128i*)Array_4));
                x7 = _mm_add_epi16(x7, _mm_load_si128((__m128i*)Array_4));

                x6 = _mm_srai_epi16(x6, 3);
                x7 = _mm_srai_epi16(x7, 3);

                x6 = _mm_packus_epi16(x6, x7);
                x7 = _mm_loadu_si128((__m128i*)(ptr_y + 16));

                x0 = _mm_unpacklo_epi8(x7, x6);
                x1 = _mm_unpackhi_epi8(x7, x6);

                _mm_storeu_si128((__m128i*)(ptr_dst + 32), x0);
                _mm_storeu_si128((__m128i*)(ptr_dst + 48), x1);
            }
        }
    }

    /* copy last 2 lines */
    dst_line_fin = buf + buf_linesize * height;
    for (; dst_line < dst_line_fin; dst_line += buf_linesize, y_line += y_pitch, u_line += uv_pitch, v_line += uv_pitch) {
        ptr_dst = dst_line;
        ptr_y = y_line;
        ptr_u = u_line;
        ptr_v = v_line;
        ptr_dst_fin = ptr_dst + (output_rowsize & ~63);
        for (; ptr_dst < ptr_dst_fin; ptr_dst += 64, ptr_y += 32, ptr_u += 16, ptr_v += 16) {
            x0 = _mm_loadu_si128((__m128i*)ptr_u);
            x1 = _mm_loadu_si128((__m128i*)ptr_v);
            x2 = _mm_unpacklo_epi8(x0, x1);
            x3 = _mm_unpackhi_epi8(x0, x1);

            x0 = _mm_loadu_si128((__m128i*)(ptr_y + 0));
            x1 = _mm_loadu_si128((__m128i*)(ptr_y + 16));
            x4 = _mm_unpacklo_epi8(x0, x2);
            x5 = _mm_unpackhi_epi8(x0, x2);
            x6 = _mm_unpacklo_epi8(x1, x3);
            x7 = _mm_unpackhi_epi8(x1, x3);
            _mm_storeu_si128((__m128i*)(ptr_dst + 0), x4);
            _mm_storeu_si128((__m128i*)(ptr_dst + 16), x5);
            _mm_storeu_si128((__m128i*)(ptr_dst + 32), x6);
            _mm_storeu_si128((__m128i*)(ptr_dst + 48), x7);
        }
        ptr_dst_fin = ptr_dst + (output_rowsize & 63);
        for (; ptr_dst < ptr_dst_fin; ptr_dst += 4, ptr_y += 2, ptr_u += 1, ptr_v += 1) {
            ptr_dst[0] = ptr_y[0];
            ptr_dst[1] = ptr_u[0];
            ptr_dst[2] = ptr_y[1];
            ptr_dst[3] = ptr_v[0];
        }
    }
    const int background_fill_count = MIN((64 - (output_rowsize & 63)) & 63, buf_linesize - output_rowsize) >> 2;
    if (background_fill_count) {
        static const uint32_t yuy2_background = (128u << 24) + (128 << 8);
        /* background_fill are not needed for last 2 lines, since the copying of them won't overwrite. */
        for (int j = 0; j < height - 2; j++) {
            uint32_t* ptr = (uint32_t*)(buf + buf_linesize * j + output_rowsize);
            for (int i = 0; i < background_fill_count; i++)
                ptr[i] = yuy2_background;
        }
    }
}

#ifdef __GNUC__
#pragma GCC target("sse4.1")
#endif
#include <smmintrin.h>

/* the inner loop branch should be deleted by forced inline expansion and "bit_depth" constant propagation. */
static void LW_FUNC_ALIGN LW_FORCEINLINE convert_yuv420ple_i_to_yuv444p16le_sse41(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height, const int bit_depth)
{
    const int lshft = 16 - bit_depth;
    /* copy luma */
    {
        uint16_t* ptr_src_line = (uint16_t*)pic_data[0];
        uint16_t* ptr_dst_line = (uint16_t*)dst[0];
        const int dst_line_len = dst_linesize[0] / sizeof(uint16_t);
        const int src_line_len = pic_linesize[0] / sizeof(uint16_t);
        const int luma_width = (output_rowsize / sizeof(uint16_t));
        for (int y = 0; y < height; y++) {
            uint16_t* ptr_dst = ptr_dst_line + y * dst_line_len;
            uint16_t* ptr_src = ptr_src_line + y * src_line_len;
            uint16_t* ptr_src_fin = ptr_src + (luma_width & ~15);
            for (; ptr_src < ptr_src_fin; ptr_dst += 16, ptr_src += 16) {
                __m128i x0 = _mm_load_si128((__m128i*)(ptr_src + 0));
                __m128i x1 = _mm_load_si128((__m128i*)(ptr_src + 8));
                if (lshft) {
                    x0 = _mm_slli_epi16(x0, lshft);
                    x1 = _mm_slli_epi16(x1, lshft);
                }
                _mm_store_si128((__m128i*)(ptr_dst + 0), x0);
                _mm_store_si128((__m128i*)(ptr_dst + 8), x1);
            }
            ptr_src_fin += (luma_width & 15);
            for (; ptr_src < ptr_src_fin; ptr_dst += 2, ptr_src += 2) {
                ptr_dst[0] = ptr_src[0] << lshft;
                ptr_dst[1] = ptr_src[1] << lshft;
            }
        }
    }

    static const uint16_t LW_ALIGN(16) Array_5371[4][8] = {
        { 5, 3, 5, 3, 5, 3, 5, 3 },
        { 7, 1, 7, 1, 7, 1, 7, 1 },
        { 1, 7, 1, 7, 1, 7, 1, 7 },
        { 3, 5, 3, 5, 3, 5, 3, 5 },
    };
    const __m128i x_add = _mm_set1_epi32(1 << (2 - lshft));
    /* chroma upsampling for interlaced yuv420 */
    const int src_chroma_width = (output_rowsize / sizeof(uint16_t)) / 2;
    for (int i_color = 1; i_color < 3; i_color++) {
        __m128i x0, x1, x3, x4, x5;
        __m128i x2 = _mm_setzero_si128();
        uint16_t* ptr_src_line = (uint16_t*)pic_data[i_color];
        uint16_t* ptr_dst_line = (uint16_t*)dst[i_color];
        const int dst_line_len = dst_linesize[i_color] / sizeof(uint16_t);
        const int src_line_len = pic_linesize[i_color] / sizeof(uint16_t);
        /* first 2 lines */
        for (int i = 0; i < 2; i++) {
            uint16_t* ptr_dst = ptr_dst_line + i * dst_line_len;
            uint16_t* ptr_src = ptr_src_line + i * src_line_len;
            uint16_t* ptr_src_fin = ptr_src + (src_chroma_width & ~7) - (((src_chroma_width & 7) == 0) << 3);

            x0 = _mm_load_si128((__m128i*)ptr_src);
            if (lshft)
                x0 = _mm_slli_epi16(x0, lshft);

            for (; ptr_src < ptr_src_fin; ptr_src += 8, ptr_dst += 16) {
                x1 = _mm_load_si128((__m128i*)(ptr_src + 8));
                if (lshft)
                    x1 = _mm_slli_epi16(x1, lshft);

                x2 = _mm_alignr_epi8(x1, x0, 2);
                x2 = _mm_avg_epu16(x2, x0);

                _mm_store_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
                _mm_store_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));
                x0 = x1;
            }
            ptr_src -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3));
            ptr_dst -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3)) << 1;

            x0 = _mm_loadu_si128((__m128i*)ptr_src);
            if (lshft)
                x0 = _mm_slli_epi16(x0, lshft);
            x2 = _mm_srli_si128(x0, 2);

            x2 = _mm_blend_epi16(x2, x0, 0x80);
            x2 = _mm_avg_epu16(x2, x0);

            _mm_storeu_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
            _mm_storeu_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));
        }
        ptr_dst_line += (dst_line_len << 1);

        /* 5,3,7,1 - interlaced yuv420 to yuv422 interpolation with 1,1 - yuv422 to yuv444 interpolation. */
        for (int y = 2; y < height - 2; y += 4, ptr_dst_line += (dst_line_len << 2), ptr_src_line += (src_line_len << 1)) {
            for (int i = 0; i < 4; i++) {
                uint16_t* ptr_src = ptr_src_line + src_line_len * (i & 0x01);
                uint16_t* ptr_dst = ptr_dst_line + dst_line_len * i;
                uint16_t* ptr_src_fin = ptr_src + (src_chroma_width & ~7) - (((src_chroma_width & 7) == 0) << 3);

                x2 = _mm_cmpeq_epi16(x2, x2);
                x2 = _mm_slli_epi16(x2, 15);

                x1 = _mm_load_si128((__m128i*)(ptr_src));
                x3 = _mm_load_si128((__m128i*)(ptr_src + (src_line_len << 1)));

                x4 = _mm_unpacklo_epi16(x1, x3);
                x5 = _mm_unpackhi_epi16(x1, x3);

                x4 = _mm_sub_epi16(x4, x2);
                x5 = _mm_sub_epi16(x5, x2);

                x4 = _mm_madd_epi16(x4, _mm_load_si128((__m128i*)Array_5371[i]));
                x5 = _mm_madd_epi16(x5, _mm_load_si128((__m128i*)Array_5371[i]));

                x2 = _mm_cvtepu16_epi32(x2);
                x2 = _mm_slli_epi32(x2, 3);

                x4 = _mm_add_epi32(x4, x2);
                x5 = _mm_add_epi32(x5, x2);

                if (lshft - 3 < 0) {
                    x4 = _mm_add_epi32(x4, x_add);
                    x5 = _mm_add_epi32(x5, x_add);
                    x4 = _mm_srai_epi32(x4, 3 - lshft);
                    x5 = _mm_srai_epi32(x5, 3 - lshft);
                } else if (lshft - 3 > 0) {
                    x4 = _mm_slli_epi32(x4, lshft - 3);
                    x5 = _mm_slli_epi32(x5, lshft - 3);
                }

                x0 = _mm_packus_epi32(x4, x5);

                for (; ptr_src < ptr_src_fin; ptr_src += 8, ptr_dst += 16) {
                    x2 = _mm_cmpeq_epi16(x2, x2);
                    x2 = _mm_slli_epi16(x2, 15);

                    x1 = _mm_load_si128((__m128i*)(ptr_src + 8));
                    x3 = _mm_load_si128((__m128i*)(ptr_src + (src_line_len << 1) + 8));

                    x4 = _mm_unpacklo_epi16(x1, x3);
                    x5 = _mm_unpackhi_epi16(x1, x3);

                    x4 = _mm_sub_epi16(x4, x2);
                    x5 = _mm_sub_epi16(x5, x2);

                    x4 = _mm_madd_epi16(x4, _mm_load_si128((__m128i*)Array_5371[i]));
                    x5 = _mm_madd_epi16(x5, _mm_load_si128((__m128i*)Array_5371[i]));

                    x2 = _mm_cvtepu16_epi32(x2);
                    x2 = _mm_slli_epi32(x2, 3);

                    x4 = _mm_add_epi32(x4, x2);
                    x5 = _mm_add_epi32(x5, x2);

                    if (lshft - 3 < 0) {
                        x4 = _mm_add_epi32(x4, x_add);
                        x5 = _mm_add_epi32(x5, x_add);
                        x4 = _mm_srai_epi32(x4, 3 - lshft);
                        x5 = _mm_srai_epi32(x5, 3 - lshft);
                    } else if (lshft - 3 > 0) {
                        x4 = _mm_slli_epi32(x4, lshft - 3);
                        x5 = _mm_slli_epi32(x5, lshft - 3);
                    }

                    x1 = _mm_packus_epi32(x4, x5);

                    x2 = _mm_alignr_epi8(x1, x0, 2);
                    x2 = _mm_avg_epu16(x2, x0);
                    _mm_store_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
                    _mm_store_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));

                    x0 = x1;
                }
                ptr_src -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3));
                ptr_dst -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3)) << 1;

                x1 = _mm_loadu_si128((__m128i*)(ptr_src));
                x3 = _mm_loadu_si128((__m128i*)(ptr_src + (src_line_len << 1)));

                x2 = _mm_cmpeq_epi16(x2, x2);
                x2 = _mm_slli_epi16(x2, 15);

                x4 = _mm_unpacklo_epi16(x1, x3);
                x5 = _mm_unpackhi_epi16(x1, x3);

                x4 = _mm_sub_epi16(x4, x2);
                x5 = _mm_sub_epi16(x5, x2);

                x4 = _mm_madd_epi16(x4, _mm_load_si128((__m128i*)Array_5371[i]));
                x5 = _mm_madd_epi16(x5, _mm_load_si128((__m128i*)Array_5371[i]));

                x2 = _mm_cvtepu16_epi32(x2);
                x2 = _mm_slli_epi32(x2, 3);

                x4 = _mm_add_epi32(x4, x2);
                x5 = _mm_add_epi32(x5, x2);

                if (lshft - 3 < 0) {
                    x4 = _mm_add_epi32(x4, x_add);
                    x5 = _mm_add_epi32(x5, x_add);
                    x4 = _mm_srai_epi32(x4, 3 - lshft);
                    x5 = _mm_srai_epi32(x5, 3 - lshft);
                } else if (lshft - 3 > 0) {
                    x4 = _mm_slli_epi32(x4, lshft - 3);
                    x5 = _mm_slli_epi32(x5, lshft - 3);
                }

                x0 = _mm_packus_epi32(x4, x5);

                x2 = _mm_srli_si128(x0, 2);
                x2 = _mm_blend_epi16(x2, x0, 0x80);
                x2 = _mm_avg_epu16(x2, x0);

                _mm_storeu_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
                _mm_storeu_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));
            }
        }

        /* last 2 lines */
        for (int i = 0; i < 2; i++) {
            uint16_t* ptr_dst = ptr_dst_line + i * dst_line_len;
            uint16_t* ptr_src = ptr_src_line + i * src_line_len;
            uint16_t* ptr_src_fin = ptr_src + (src_chroma_width & ~7) - (((src_chroma_width & 7) == 0) << 3);

            x0 = _mm_load_si128((__m128i*)ptr_src);
            if (lshft)
                x0 = _mm_slli_epi16(x0, lshft);

            for (; ptr_src < ptr_src_fin; ptr_src += 8, ptr_dst += 16) {
                x1 = _mm_load_si128((__m128i*)(ptr_src + 8));
                if (lshft)
                    x1 = _mm_slli_epi16(x1, lshft);

                x2 = _mm_alignr_epi8(x1, x0, 2);
                x2 = _mm_avg_epu16(x2, x0);

                _mm_store_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
                _mm_store_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));
                x0 = x1;
            }
            ptr_src -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3));
            ptr_dst -= (8 - (src_chroma_width & 7) - (((src_chroma_width & 7) == 0) << 3)) << 1;

            x0 = _mm_loadu_si128((__m128i*)ptr_src);
            if (lshft)
                x0 = _mm_slli_epi16(x0, lshft);
            x2 = _mm_srli_si128(x0, 2);

            x2 = _mm_blend_epi16(x2, x0, 0x80);
            x2 = _mm_avg_epu16(x2, x0);

            _mm_storeu_si128((__m128i*)(ptr_dst + 0), _mm_unpacklo_epi16(x0, x2));
            _mm_storeu_si128((__m128i*)(ptr_dst + 8), _mm_unpackhi_epi16(x0, x2));
        }
    }
}

void LW_FUNC_ALIGN convert_yuv420p9le_i_to_yuv444p16le_sse41(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le_sse41(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 9);
}

void LW_FUNC_ALIGN convert_yuv420p10le_i_to_yuv444p16le_sse41(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le_sse41(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 10);
}

void LW_FUNC_ALIGN convert_yuv420p16le_i_to_yuv444p16le_sse41(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le_sse41(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 16);
}

/* SIMD version of func convert_yuv16le_to_yc48
 * dst_data[0], dst_data[1], dst_data[2], buf, buf_linesize and dst_linesize need to be mod16.
 * the inner loop branch should be deleted by forced inline expansion and "use_sse41" constant propagation. */
void LW_FUNC_ALIGN LW_FORCEINLINE convert_yuv16le_to_yc48_simd(uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize,
    int output_rowsize, int output_height, int full_range, const int use_sse41)
{
    uint8_t *ycp, *ycp_fin;
    uint8_t *p_dst_y, *p_dst_u, *p_dst_v;
    __m128i x0, x1, x2, x3, x4;
#define Y_COEF 4788
#define Y_COEF_FULL 4770
#define UV_COEF 4682
#define UV_COEF_FULL 4662
#define Y_OFFSET ((-299) + ((Y_COEF) >> 1))
#define Y_OFFSET_FULL ((-299) + ((Y_COEF_FULL) >> 1))
#define UV_OFFSET 32768
#define UV_OFFSET_FULL 589824
    static const int LW_ALIGN(16) aY_coef[2][4]
        = { { Y_COEF, Y_COEF, Y_COEF, Y_COEF }, { Y_COEF_FULL, Y_COEF_FULL, Y_COEF_FULL, Y_COEF_FULL } };
    static const int LW_ALIGN(16) aUV_coef[2][4]
        = { { UV_COEF, UV_COEF, UV_COEF, UV_COEF }, { UV_COEF_FULL, UV_COEF_FULL, UV_COEF_FULL, UV_COEF_FULL } };
    static const int16_t LW_ALIGN(16) aY_offest[2][8]
        = { { Y_OFFSET, Y_OFFSET, Y_OFFSET, Y_OFFSET, Y_OFFSET, Y_OFFSET, Y_OFFSET, Y_OFFSET },
              { Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL, Y_OFFSET_FULL } };
    static const int LW_ALIGN(16) aUV_offest[2][4]
        = { { UV_OFFSET, UV_OFFSET, UV_OFFSET, UV_OFFSET }, { UV_OFFSET_FULL, UV_OFFSET_FULL, UV_OFFSET_FULL, UV_OFFSET_FULL } };
#undef Y_COEF
#undef Y_COEF_FULL
#undef UV_COEF
#undef UV_COEF_FULL
#undef Y_OFFSET
#undef Y_OFFSET_FULL
#undef UV_OFFSET
#undef UV_OFFSET_FULL
    static const uint8_t LW_ALIGN(16) a_shuffle[16]
        = { 0x00, 0x01, 0x06, 0x07, 0x0C, 0x0D, 0x02, 0x03, 0x08, 0x09, 0x0E, 0x0F, 0x04, 0x05, 0x0A, 0x0B };
    x3 = _mm_setzero_si128(); /* Initialize to avoid warning and some weird gcc optimization. */
    /*  Y = ((( y - 32768 ) * coef)           >> 16 ) + (coef/2 - 299) */
    /* UV = (( uv - 32768 ) * coef + offset ) >> 16 */
    for (uint32_t h = 0; h < output_height; h++) {
        p_dst_y = dst_data[0] + dst_linesize[0] * h;
        p_dst_u = dst_data[1] + dst_linesize[1] * h;
        p_dst_v = dst_data[2] + dst_linesize[2] * h;
        ycp = buf + buf_linesize * h;
        for (ycp_fin = ycp + output_rowsize; ycp < ycp_fin; ycp += 48, p_dst_y += 16, p_dst_u += 16, p_dst_v += 16) {
            /* make -32768 (0x8000) */
            x3 = _mm_cmpeq_epi32(x3, x3); /* 0xffff */
            x3 = _mm_slli_epi16(x3, 15); /* 0x8000 */
            /* load */
            x0 = _mm_load_si128((__m128i*)p_dst_y);
            x1 = _mm_load_si128((__m128i*)p_dst_u);
            x2 = _mm_load_si128((__m128i*)p_dst_v);
            /* change uint16 to int16 in order to use _mm_madd_epi16()
             * range 0 - 65535 to -32768 - 32767 */
            x0 = _mm_add_epi16(x0, x3);
            x1 = _mm_add_epi16(x1, x3);
            x2 = _mm_add_epi16(x2, x3);

            /* calc Y */
            x3 = x0;
            x3 = _mm_unpackhi_epi16(x3, x0);
            x0 = _mm_unpacklo_epi16(x0, x0);

            x3 = _mm_madd_epi16(x3, _mm_load_si128((__m128i*)aY_coef[full_range]));
            x0 = _mm_madd_epi16(x0, _mm_load_si128((__m128i*)aY_coef[full_range]));

            x3 = _mm_srai_epi32(x3, 16);
            x0 = _mm_srai_epi32(x0, 16);

            x0 = _mm_packs_epi32(x0, x3);

            x0 = _mm_add_epi16(x0, _mm_load_si128((__m128i*)aY_offest[full_range]));

            /* calc U */
            x3 = x1;
            x3 = _mm_unpackhi_epi16(x3, x1);
            x1 = _mm_unpacklo_epi16(x1, x1);

            x3 = _mm_madd_epi16(x3, _mm_load_si128((__m128i*)aUV_coef[full_range]));
            x1 = _mm_madd_epi16(x1, _mm_load_si128((__m128i*)aUV_coef[full_range]));

            x3 = _mm_add_epi32(x3, _mm_load_si128((__m128i*)aUV_offest[full_range]));
            x1 = _mm_add_epi32(x1, _mm_load_si128((__m128i*)aUV_offest[full_range]));

            x3 = _mm_srai_epi32(x3, 16);
            x1 = _mm_srai_epi32(x1, 16);

            x1 = _mm_packs_epi32(x1, x3);

            /* calc V */
            x3 = x2;
            x3 = _mm_unpackhi_epi16(x3, x2);
            x2 = _mm_unpacklo_epi16(x2, x2);

            x3 = _mm_madd_epi16(x3, _mm_load_si128((__m128i*)aUV_coef[full_range]));
            x2 = _mm_madd_epi16(x2, _mm_load_si128((__m128i*)aUV_coef[full_range]));

            x3 = _mm_add_epi32(x3, _mm_load_si128((__m128i*)aUV_offest[full_range]));
            x2 = _mm_add_epi32(x2, _mm_load_si128((__m128i*)aUV_offest[full_range]));

            x3 = _mm_srai_epi32(x3, 16);
            x2 = _mm_srai_epi32(x2, 16);

            x2 = _mm_packs_epi32(x2, x3);

            if (use_sse41) {
                x4 = _mm_load_si128((__m128i*)a_shuffle);
                x0 = _mm_shuffle_epi8(x0, x4);
                x1 = _mm_shuffle_epi8(x1, _mm_alignr_epi8(x4, x4, 14));
                x2 = _mm_shuffle_epi8(x2, _mm_alignr_epi8(x4, x4, 12));

                x3 = _mm_blend_epi16(x0, x1, 0x80 + 0x10 + 0x02);
                x3 = _mm_blend_epi16(x3, x2, 0x20 + 0x04);
                x2 = _mm_blend_epi16(x2, x1, 0x20 + 0x04);
                x4 = x2;
                x1 = _mm_blend_epi16(x1, x0, 0x20 + 0x04);
                x2 = _mm_blend_epi16(x2, x0, 0x80 + 0x10 + 0x02);
                x1 = _mm_blend_epi16(x1, x4, 0x80 + 0x10 + 0x02);
                x0 = x3;
            } else {
                /* shuffle order 7,6,5,4,3,2,1,0 to 7,3,5,1,6,2,4,0 */
                x0 = _mm_shufflelo_epi16(x0, _MM_SHUFFLE(3, 1, 2, 0)); /* 7,6,5,4,3,1,2,0 */
                x0 = _mm_shufflehi_epi16(x0, _MM_SHUFFLE(3, 1, 2, 0)); /* 7,5,6,4,3,1,2,0 */
                x0 = _mm_shuffle_epi32(x0, _MM_SHUFFLE(3, 1, 2, 0)); /* 7,5,3,1,6,4,2,0 */
                x0 = _mm_shufflelo_epi16(x0, _MM_SHUFFLE(3, 1, 2, 0)); /* 7,5,3,1,6,2,4,0 */
                x0 = _mm_shufflehi_epi16(x0, _MM_SHUFFLE(3, 1, 2, 0)); /* 7,3,5,1,6,2,4,0 */

                x1 = _mm_shufflelo_epi16(x1, _MM_SHUFFLE(3, 1, 2, 0));
                x1 = _mm_shufflehi_epi16(x1, _MM_SHUFFLE(3, 1, 2, 0));
                x1 = _mm_shuffle_epi32(x1, _MM_SHUFFLE(3, 1, 2, 0));
                x1 = _mm_shufflelo_epi16(x1, _MM_SHUFFLE(3, 1, 2, 0));
                x1 = _mm_shufflehi_epi16(x1, _MM_SHUFFLE(3, 1, 2, 0));

                x2 = _mm_shufflelo_epi16(x2, _MM_SHUFFLE(3, 1, 2, 0));
                x2 = _mm_shufflehi_epi16(x2, _MM_SHUFFLE(3, 1, 2, 0));
                x2 = _mm_shuffle_epi32(x2, _MM_SHUFFLE(3, 1, 2, 0));
                x2 = _mm_shufflelo_epi16(x2, _MM_SHUFFLE(3, 1, 2, 0));
                x2 = _mm_shufflehi_epi16(x2, _MM_SHUFFLE(3, 1, 2, 0));

                /* shuffle to PIXEL_YC */
                x3 = _mm_shuffle_epi32(x0, _MM_SHUFFLE(3, 2, 3, 2));
                x0 = _mm_unpacklo_epi16(x0, x1);
                x1 = _mm_unpackhi_epi16(x1, x2);
                x2 = _mm_unpacklo_epi16(x2, x3);

                x3 = _mm_shuffle_epi32(x0, _MM_SHUFFLE(3, 2, 3, 2));
                x0 = _mm_unpacklo_epi32(x0, x2);
                x2 = _mm_unpackhi_epi32(x2, x1);
                x1 = _mm_unpacklo_epi32(x1, x3);

                x3 = _mm_shuffle_epi32(x0, _MM_SHUFFLE(3, 2, 3, 2));
                x0 = _mm_unpacklo_epi64(x0, x1);
                x1 = _mm_unpackhi_epi64(x1, x2);
                x2 = _mm_unpacklo_epi64(x2, x3);
            }

            /* store */
            _mm_stream_si128((__m128i*)&ycp[0], x0);
            _mm_stream_si128((__m128i*)&ycp[16], x2);
            _mm_stream_si128((__m128i*)&ycp[32], x1);
        }
    }
    const int background_fill_count = ((48 - (output_rowsize % 48)) % 48) >> 1;
    if (background_fill_count)
        for (int j = 0; j < output_height; j++) {
            int16_t* ptr = (int16_t*)(buf + buf_linesize * j + output_rowsize);
            for (int i = 0; i < background_fill_count; i++)
                ptr[i] = 0;
        }
}

void LW_FUNC_ALIGN convert_yuv16le_to_yc48_sse2(
    uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize, int output_rowsize, int output_height, int full_range)
{
    convert_yuv16le_to_yc48_simd(buf, buf_linesize, dst_data, dst_linesize, output_rowsize, output_height, full_range, 0);
}

void LW_FUNC_ALIGN convert_yuv16le_to_yc48_sse4_1(
    uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize, int output_rowsize, int output_height, int full_range)
{
    convert_yuv16le_to_yc48_simd(buf, buf_linesize, dst_data, dst_linesize, output_rowsize, output_height, full_range, 1);
}
