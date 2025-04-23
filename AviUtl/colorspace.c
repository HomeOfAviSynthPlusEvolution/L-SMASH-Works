/*****************************************************************************
 * colorspace.c
 *****************************************************************************
 * Copyright (C) 2011-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Oka Motofumi <chikuzen.mo@gmail.com>
 *          rigaya <rigaya34589@live.jp>
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

#include "lwinput.h"

#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

#include "../common/lwsimd.h"
#include "colorspace_simd.h"
#include "video_output.h"

typedef struct {
    uint8_t* data[4];
    int linesize[4];
} au_picture_t;

static void convert_yuv16le_to_lw48(uint8_t* buf, int buf_linesize, AVFrame* yuv444p16, int output_rowsize, int output_height)
{
    uint32_t offset = 0;
    while (output_height--) {
        uint8_t* p_buf = buf;
        uint8_t* p_dst[3] = { yuv444p16->data[0] + offset, yuv444p16->data[1] + offset, yuv444p16->data[2] + offset };
        for (int i = 0; i < output_rowsize; i += LW48_SIZE) {
            p_buf[0] = p_dst[0][0];
            p_buf[1] = p_dst[0][1];
            p_buf[2] = p_dst[1][0];
            p_buf[3] = p_dst[1][1];
            p_buf[4] = p_dst[2][0];
            p_buf[5] = p_dst[2][1];
            p_buf += LW48_SIZE;
            p_dst[0] += 2;
            p_dst[1] += 2;
            p_dst[2] += 2;
        }
        buf += buf_linesize;
        offset += yuv444p16->linesize[0];
    }
}

static void convert_yuv16le_to_yc48(
    uint8_t* buf, int buf_linesize, uint8_t** dst_data, int* dst_linesize, int output_rowsize, int output_height, int full_range)
{
    uint32_t offset = 0;
    while (output_height--) {
        uint8_t* p_buf = buf;
        uint8_t* p_dst[3] = { dst_data[0] + offset, dst_data[1] + offset, dst_data[2] + offset };
        for (int i = 0; i < output_rowsize; i += YC48_SIZE) {
            static const uint32_t y_coef[2] = { 1197, 4770 };
            static const uint32_t y_shift[2] = { 14, 16 };
            static const uint32_t uv_coef[2] = { 4682, 4662 };
            static const uint32_t uv_offset[2] = { 32768, 589824 };
            uint16_t y = (((int32_t)((p_dst[0][0] | (p_dst[0][1] << 8)) * y_coef[full_range])) >> y_shift[full_range]) - 299;
            uint16_t cb = ((int32_t)(((p_dst[1][0] | (p_dst[1][1] << 8)) - 32768) * uv_coef[full_range] + uv_offset[full_range])) >> 16;
            uint16_t cr = ((int32_t)(((p_dst[2][0] | (p_dst[2][1] << 8)) - 32768) * uv_coef[full_range] + uv_offset[full_range])) >> 16;
            p_dst[0] += 2;
            p_dst[1] += 2;
            p_dst[2] += 2;
            p_buf[0] = y;
            p_buf[1] = y >> 8;
            p_buf[2] = cb;
            p_buf[3] = cb >> 8;
            p_buf[4] = cr;
            p_buf[5] = cr >> 8;
            p_buf += YC48_SIZE;
        }
        buf += buf_linesize;
        offset += dst_linesize[0];
    }
}

static void convert_packed_chroma_to_planar(
    au_picture_t* planar_chroma, uint8_t* packed_chroma, int packed_linesize, int chroma_width, int chroma_height)
{
    for (int y = 0; y < chroma_height; y++) {
        uint8_t* src = packed_chroma + packed_linesize * y;
        uint8_t* dst_u = planar_chroma->data[1] + planar_chroma->linesize[1] * y;
        uint8_t* dst_v = planar_chroma->data[2] + planar_chroma->linesize[2] * y;
        for (int x = 0; x < chroma_width; x++) {
            dst_u[x] = src[2 * x];
            dst_v[x] = src[2 * x + 1];
        }
    }
}

static void inline __attribute__((always_inline)) convert_yuv420ple_i_to_yuv444p16le(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height, int bit_depth)
{
    const int lshft = 16 - bit_depth;
    /* copy luma */
    {
        uint16_t* ptr_src_line = (uint16_t*)pic_data[0];
        uint16_t* ptr_dst_line = (uint16_t*)dst[0];
        const int dst_line_len = dst_linesize[0] / sizeof(uint16_t);
        const int src_line_len = pic_linesize[0] / sizeof(uint16_t);
        const int luma_width = (output_rowsize / sizeof(uint16_t));
        for (int y = 0; y < height; y++)
            for (int x = 0; x < luma_width; x++)
                ptr_dst_line[y * dst_line_len + x] = ptr_src_line[y * src_line_len + x] << lshft;
    }
    /* chroma upsampling for interlaced yuv420 */
    const int src_chroma_width = (output_rowsize / sizeof(uint16_t)) / 2;
    for (int i_color = 1; i_color < 3; i_color++) {
        uint16_t* ptr_src_line = (uint16_t*)pic_data[i_color];
        uint16_t* ptr_dst_line = (uint16_t*)dst[i_color];
        const int dst_line_len = dst_linesize[i_color] / sizeof(uint16_t);
        const int src_line_len = pic_linesize[i_color] / sizeof(uint16_t);
        /* first 2 lines */
        int x;
        uint16_t tmp[2][4];
        uint64_t *tmp0 = (uint64_t*)tmp[0], *tmp1 = (uint64_t*)tmp[1];

        /* this inner loop branch should be deleted by forced inline expansion and "lshft" constant propagation. */
#define INTERPOLATE_CHROMA(k, x)                                                                         \
    {                                                                                                    \
        int chroma0 = (5 * ptr_src_line[0 * src_line_len + x] + 3 * ptr_src_line[2 * src_line_len + x]); \
        int chroma1 = (7 * ptr_src_line[1 * src_line_len + x] + 1 * ptr_src_line[3 * src_line_len + x]); \
        int chroma2 = (1 * ptr_src_line[0 * src_line_len + x] + 7 * ptr_src_line[2 * src_line_len + x]); \
        int chroma3 = (3 * ptr_src_line[1 * src_line_len + x] + 5 * ptr_src_line[3 * src_line_len + x]); \
        if (lshft - 3 < 0) {                                                                             \
            tmp[k][0] = (chroma0 + (1 << (2 - lshft))) >> (3 - lshft);                                   \
            tmp[k][1] = (chroma1 + (1 << (2 - lshft))) >> (3 - lshft);                                   \
            tmp[k][2] = (chroma2 + (1 << (2 - lshft))) >> (3 - lshft);                                   \
            tmp[k][3] = (chroma3 + (1 << (2 - lshft))) >> (3 - lshft);                                   \
        } else if (lshft - 3 > 0) {                                                                      \
            tmp[k][0] = chroma0 << (lshft - 3);                                                          \
            tmp[k][1] = chroma1 << (lshft - 3);                                                          \
            tmp[k][2] = chroma2 << (lshft - 3);                                                          \
            tmp[k][3] = chroma3 << (lshft - 3);                                                          \
        } else {                                                                                         \
            tmp[k][0] = chroma0;                                                                         \
            tmp[k][1] = chroma1;                                                                         \
            tmp[k][2] = chroma2;                                                                         \
            tmp[k][3] = chroma3;                                                                         \
        }                                                                                                \
    }
#define PUT_CHROMA(x, line)                                                                 \
    {                                                                                       \
        ptr_dst_line[dst_line_len * line + 2 * x + 0] = tmp[0][line];                       \
        ptr_dst_line[dst_line_len * line + 2 * x + 1] = (tmp[0][line] + tmp[1][line]) >> 1; \
    }

        tmp[0][0] = ptr_src_line[0] << lshft;
        tmp[0][1] = ptr_src_line[src_line_len] << lshft;
        for (x = 0; x < src_chroma_width - 1; x++) {
            tmp[1][0] = ptr_src_line[x + 1] << lshft;
            tmp[1][1] = ptr_src_line[x + 1 + src_line_len] << lshft;
            for (int i = 0; i < 2; i++)
                PUT_CHROMA(x, i);
            *tmp0 = *tmp1;
        }
        for (int i = 0; i < 2; i++)
            PUT_CHROMA(x, i);
        ptr_dst_line += (dst_line_len << 1);

        /* 5,3,7,1 - interlaced yuv420 to yuv422 interpolation with 1,1 - yuv422 to yuv444 interpolation. */
        for (int y = 2; y < height - 2; y += 4, ptr_dst_line += (dst_line_len << 2), ptr_src_line += (src_line_len << 1)) {
            INTERPOLATE_CHROMA(0, 0);
            for (x = 0; x < src_chroma_width - 1; x++) {
                INTERPOLATE_CHROMA(1, x + 1);
                for (int i = 0; i < 4; i++)
                    PUT_CHROMA(x, i);
                *tmp0 = *tmp1;
            }
            for (int i = 0; i < 4; i++)
                PUT_CHROMA(x, i);
        }

        /* last 2 lines */
        tmp[0][0] = ptr_src_line[0] << lshft;
        tmp[0][1] = ptr_src_line[src_line_len] << lshft;
        for (x = 0; x < src_chroma_width - 1; x++) {
            tmp[1][0] = ptr_src_line[x + 1] << lshft;
            tmp[1][1] = ptr_src_line[x + 1 + src_line_len] << lshft;
            for (int i = 0; i < 2; i++)
                PUT_CHROMA(x, i);
            *tmp0 = *tmp1;
        }
        for (int i = 0; i < 2; i++)
            PUT_CHROMA(x, i);
#undef INTERPOLATE_CHROMA
#undef PUT_CHROMA
    }
}

static void convert_yuv420p9le_i_to_yuv444p16le(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 9);
}

static void convert_yuv420p10le_i_to_yuv444p16le(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 10);
}

static void convert_yuv420p16le_i_to_yuv444p16le(
    uint8_t** dst, const int* dst_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    convert_yuv420ple_i_to_yuv444p16le(dst, dst_linesize, pic_data, pic_linesize, output_rowsize, height, 16);
}

static void convert_yv12i_to_yuy2(uint8_t* buf, int buf_linesize, uint8_t** pic_data, int* pic_linesize, int output_rowsize, int height)
{
    uint8_t* pic_y = pic_data[0];
    uint8_t* pic_u = pic_data[1];
    uint8_t* pic_v = pic_data[2];
#define INTERPOLATE_CHROMA(x, chroma, offset)                                                                                          \
    {                                                                                                                                  \
        buf[0 * buf_linesize + 4 * x + offset] = (5 * chroma[0 * pic_linesize[1] + x] + 3 * chroma[2 * pic_linesize[1] + x] + 4) >> 3; \
        buf[1 * buf_linesize + 4 * x + offset] = (7 * chroma[1 * pic_linesize[1] + x] + 1 * chroma[3 * pic_linesize[1] + x] + 4) >> 3; \
        buf[2 * buf_linesize + 4 * x + offset] = (1 * chroma[0 * pic_linesize[1] + x] + 7 * chroma[2 * pic_linesize[1] + x] + 4) >> 3; \
        buf[3 * buf_linesize + 4 * x + offset] = (3 * chroma[1 * pic_linesize[1] + x] + 5 * chroma[3 * pic_linesize[1] + x] + 4) >> 3; \
    }
#define COPY_CHROMA(x, chroma, offset)                                    \
    {                                                                     \
        buf[4 * x + offset] = chroma[x];                                  \
        buf[buf_linesize + 4 * x + offset] = chroma[pic_linesize[1] + x]; \
    }
    /* Copy all luma (Y). */
    int luma_width = output_rowsize / YUY2_SIZE;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < luma_width; x++)
            buf[y * buf_linesize + 2 * x] = pic_y[x];
        pic_y += pic_linesize[0];
    }
    /* Copy first 2 lines */
    int chroma_width = luma_width / 2;
    for (int x = 0; x < chroma_width; x++) {
        COPY_CHROMA(x, pic_u, 1);
        COPY_CHROMA(x, pic_v, 3);
    }
    buf += buf_linesize * 2;
    /* Interpolate interlaced yv12 to yuy2 with suggestion in MPEG-2 spec. */
    int four_buf_linesize = buf_linesize * 4;
    int four_chroma_linesize = pic_linesize[1] * 2;
    for (int y = 2; y < height - 2; y += 4) {
        for (int x = 0; x < chroma_width; x++) {
            INTERPOLATE_CHROMA(x, pic_u, 1); /* U */
            INTERPOLATE_CHROMA(x, pic_v, 3); /* V */
        }
        buf += four_buf_linesize;
        pic_u += four_chroma_linesize;
        pic_v += four_chroma_linesize;
    }
    /* Copy last 2 lines. */
    for (int x = 0; x < chroma_width; x++) {
        COPY_CHROMA(x, pic_u, 1);
        COPY_CHROMA(x, pic_v, 3);
    }
#undef INTERPOLATE_CHROMA
#undef COPY_CHROMA
}

static int to_yuv16le(struct SwsContext* sws_ctx, AVFrame* picture, AVFrame* yuv444p16, int width, int height)
{
    static const struct {
        enum AVPixelFormat px_fmt;
        func_convert_yuv420ple_i_to_yuv444p16le* convert[2];
    } yuv420_list[] = {
        { AV_PIX_FMT_YUV420P9LE, { convert_yuv420p9le_i_to_yuv444p16le, convert_yuv420p9le_i_to_yuv444p16le_sse41 } },
        { AV_PIX_FMT_YUV420P10LE, { convert_yuv420p10le_i_to_yuv444p16le, convert_yuv420p10le_i_to_yuv444p16le_sse41 } },
        { AV_PIX_FMT_YUV420P16LE, { convert_yuv420p16le_i_to_yuv444p16le, convert_yuv420p16le_i_to_yuv444p16le_sse41 } },
    };
    int yuv420_index = -1;
    if (picture->interlaced_frame)
        for (int idx = 0; idx < _countof(yuv420_list); idx++)
            if (picture->format == yuv420_list[idx].px_fmt) {
                yuv420_index = idx;
                break;
            }
    if (yuv420_index != -1) {
        static int sse41_available = -1;
        if (sse41_available == -1)
            sse41_available = lw_check_sse41();
        yuv420_list[yuv420_index].convert[sse41_available](
            yuv444p16->data, yuv444p16->linesize, picture->data, picture->linesize, width * sizeof(uint16_t), height);
        return height;
    } else
        return sws_scale(sws_ctx, (const uint8_t* const*)picture->data, picture->linesize, 0, height, yuv444p16->data, yuv444p16->linesize);
}

int to_yuv16le_to_lw48(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    lw_video_scaler_handler_t* vshp = &vohp->scaler;
    au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
    AVFrame* yuv444p16 = au_vohp->yuv444p16;
    int output_rowsize = vshp->input_width * LW48_SIZE;
    int output_height = to_yuv16le(vshp->sws_ctx, picture, yuv444p16, vshp->input_width, vshp->input_height);
    /* Convert planar YUV 4:4:4 48bpp little-endian into LW48. */
    convert_yuv16le_to_lw48(buf, au_vohp->output_linesize, yuv444p16, output_rowsize, output_height);
    return MAKE_AVIUTL_PITCH(output_rowsize << 3) * output_height;
}

int to_yuv16le_to_yc48(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    lw_video_scaler_handler_t* vshp = &vohp->scaler;
    au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
    AVFrame* yuv444p16 = au_vohp->yuv444p16;
    int output_rowsize = vshp->input_width * YC48_SIZE;
    int output_height = to_yuv16le(vshp->sws_ctx, picture, yuv444p16, vshp->input_width, vshp->input_height);
    /* Convert planar YUV 4:4:4 48bpp little-endian into YC48. */
    static int simd_available = -1;
    if (simd_available == -1)
        simd_available = lw_check_sse2() + (lw_check_sse2() && lw_check_sse41());
    static void (*func_yuv16le_to_yc48[3])(uint8_t*, int, uint8_t**, int*, int, int, int)
        = { convert_yuv16le_to_yc48, convert_yuv16le_to_yc48_sse2, convert_yuv16le_to_yc48_sse4_1 };
    func_yuv16le_to_yc48[simd_available * (((au_vohp->output_linesize | (size_t)buf) & 15) == 0)](
        buf, au_vohp->output_linesize, yuv444p16->data, yuv444p16->linesize, output_rowsize, output_height, vshp->input_yuv_range);
    return MAKE_AVIUTL_PITCH(output_rowsize << 3) * output_height;
}

int to_rgba(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    lw_video_scaler_handler_t* vshp = &vohp->scaler;
    au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
    uint8_t* dst_data[4] = { buf + au_vohp->output_linesize * (vohp->output_height - 1), NULL, NULL, NULL };
    int dst_linesize[4] = { -(au_vohp->output_linesize), 0, 0, 0 };
    int output_height
        = sws_scale(vshp->sws_ctx, (const uint8_t* const*)picture->data, picture->linesize, 0, vshp->input_height, dst_data, dst_linesize);
    int output_rowsize = vshp->input_width * RGBA_SIZE;
    return MAKE_AVIUTL_PITCH(output_rowsize << 3) * output_height;
}

int to_rgb24(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    lw_video_scaler_handler_t* vshp = &vohp->scaler;
    au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
    uint8_t* dst_data[4] = { buf + au_vohp->output_linesize * (vohp->output_height - 1), NULL, NULL, NULL };
    int dst_linesize[4] = { -(au_vohp->output_linesize), 0, 0, 0 };
    int output_height
        = sws_scale(vshp->sws_ctx, (const uint8_t* const*)picture->data, picture->linesize, 0, vshp->input_height, dst_data, dst_linesize);
    int output_rowsize = vshp->input_width * RGB24_SIZE;
    return MAKE_AVIUTL_PITCH(output_rowsize << 3) * output_height;
}

int to_yuy2(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    lw_video_scaler_handler_t* vshp = &vohp->scaler;
    au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
    int output_rowsize = 0;
    if (picture->interlaced_frame
        && ((picture->format == AV_PIX_FMT_YUV420P) || (picture->format == AV_PIX_FMT_YUVJ420P) || (picture->format == AV_PIX_FMT_NV12)
            || (picture->format == AV_PIX_FMT_NV21))) {
        au_picture_t au_picture = { { picture->data[0], picture->data[1], picture->data[2], NULL },
            { picture->linesize[0], picture->linesize[1], picture->linesize[2], 0 } };
        if ((picture->format == AV_PIX_FMT_NV12) || (picture->format == AV_PIX_FMT_NV21)) {
            int planar_chroma_linesize = ((picture->linesize[1] / 2) + 31) & ~31; /* 32 byte alignment */
            uint32_t another_chroma_size = planar_chroma_linesize * vshp->input_height;
            if (!au_vohp->another_chroma || au_vohp->another_chroma_size < another_chroma_size) {
                uint8_t* another_chroma = av_realloc(au_vohp->another_chroma, 2 * another_chroma_size);
                if (!another_chroma) {
                    MessageBox(HWND_DESKTOP, "Failed to allocate another chroma.", "lwinput", MB_ICONERROR | MB_OK);
                    return -1;
                }
                au_vohp->another_chroma = another_chroma;
                au_vohp->another_chroma_size = another_chroma_size;
            }
            /* Assign data set as YV12. */
            au_picture.data[picture->format == AV_PIX_FMT_NV12 ? 1 : 2] = au_vohp->another_chroma;
            au_picture.data[picture->format == AV_PIX_FMT_NV12 ? 2 : 1] = au_vohp->another_chroma + another_chroma_size;
            au_picture.linesize[1] = planar_chroma_linesize;
            au_picture.linesize[2] = planar_chroma_linesize;
            /* Convert chroma NV12 to YV12 (split packed UV into planar U and V). */
            convert_packed_chroma_to_planar(
                &au_picture, picture->data[1], picture->linesize[1], vshp->input_width / 2, vshp->input_height / 2);
        }
        /* Interlaced YV12 to YUY2 conversion */
        output_rowsize = vshp->input_width * YUY2_SIZE;
        static int ssse3_available = -1;
        if (ssse3_available == -1)
            ssse3_available = lw_check_ssse3();
        static void (*func_yv12i_to_yuy2[2])(uint8_t*, int, uint8_t**, int*, int, int)
            = { convert_yv12i_to_yuy2, convert_yv12i_to_yuy2_ssse3 };
        func_yv12i_to_yuy2[ssse3_available](
            buf, au_vohp->output_linesize, au_picture.data, au_picture.linesize, output_rowsize, vshp->input_height);
    } else {
        uint8_t* dst_data[4] = { buf, NULL, NULL, NULL };
        int dst_linesize[4] = { au_vohp->output_linesize, 0, 0, 0 };
        sws_scale(vshp->sws_ctx, (const uint8_t* const*)picture->data, picture->linesize, 0, vshp->input_height, dst_data, dst_linesize);
        output_rowsize = vshp->input_width * YUY2_SIZE;
    }
    return MAKE_AVIUTL_PITCH(output_rowsize << 3) * vohp->output_height;
}
