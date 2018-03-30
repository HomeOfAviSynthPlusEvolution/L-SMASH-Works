/*****************************************************************************
 * video_output.cpp
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

#include "lsmashsource.h"

#if _MSC_VER >= 1700
#define VC_HAS_AVX2 1
#else
#define VC_HAS_AVX2 0
#endif

#include <emmintrin.h>  /* SSE2 */
#if VC_HAS_AVX2
#include <immintrin.h>  /* AVX, AVX2 */
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
}

#include "../common/lwsimd.h"

#include "video_output.h"

#if (LIBAVUTIL_VERSION_MICRO >= 100) && (LIBSWSCALE_VERSION_MICRO >= 100)
#define FFMPEG_HIGH_DEPTH_SUPPORT 1
#else
#define FFMPEG_HIGH_DEPTH_SUPPORT 0
#endif

static const int sse2_available = lw_check_sse2();
static const int avx2_available = VC_HAS_AVX2 && lw_check_avx2();

static void make_black_background_planar_yuv
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_Y ), 0x00,                   frame->GetPitch( PLANAR_Y ) * frame->GetHeight( PLANAR_Y ) );
    memset( frame->GetWritePtr( PLANAR_U ), 0x80<<bitdepth_minus_8, frame->GetPitch( PLANAR_U ) * frame->GetHeight( PLANAR_U ) );
    memset( frame->GetWritePtr( PLANAR_V ), 0x80<<bitdepth_minus_8, frame->GetPitch( PLANAR_V ) * frame->GetHeight( PLANAR_V ) );
}

static void make_black_background_planar_yuv_interleaved
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_Y ), 0x00, frame->GetPitch( PLANAR_Y ) * frame->GetHeight( PLANAR_Y ) );
    uint8_t msb  = (uint8_t)((0x80U << bitdepth_minus_8) >> 8);
    int     size = frame->GetPitch( PLANAR_U ) * frame->GetHeight( PLANAR_U );
    for( int i = 0; i < size; i++ )
        if( i & 1 )
        {
            *(frame->GetWritePtr( PLANAR_U ) + i) = msb;
            *(frame->GetWritePtr( PLANAR_V ) + i) = msb;
        }
        else
        {
            *(frame->GetWritePtr( PLANAR_U ) + i) = 0x00;
            *(frame->GetWritePtr( PLANAR_V ) + i) = 0x00;
        }
}

static void make_black_background_packed_yuv422
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    uint32_t *dst = (uint32_t *)frame->GetWritePtr();
    int num_loops = frame->GetPitch() * frame->GetHeight() / 4;
    for( int i = 0; i < num_loops; i++ )
        *dst++ = 0x00800080;
}

static void make_black_background_packed_all_zero
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr(), 0x00, frame->GetPitch() * frame->GetHeight() );
}

/* This source filter always uses lines aligned to an address dividable by 32.
 * Furthermore it seems Avisynth bulit-in BitBlt is slow.
 * So, I think it's OK that we always use swscale instead. */
static inline int convert_av_pixel_format
(
    struct SwsContext *sws_ctx,
    int                height,
    AVFrame           *av_frame,
    as_picture_t      *as_picture
)
{
    int ret = sws_scale( sws_ctx,
                         (const uint8_t * const *)av_frame->data, av_frame->linesize,
                         0, height,
                         as_picture->data, as_picture->linesize );
    return ret > 0 ? ret : -1;
}

static inline void as_assign_planar_yuv
(
    PVideoFrame  &as_frame,
    as_picture_t *as_picture
)
{
    as_picture->data    [0] = as_frame->GetWritePtr( PLANAR_Y );
    as_picture->data    [1] = as_frame->GetWritePtr( PLANAR_U );
    as_picture->data    [2] = as_frame->GetWritePtr( PLANAR_V );
    as_picture->linesize[0] = as_frame->GetPitch   ( PLANAR_Y );
    as_picture->linesize[1] = as_frame->GetPitch   ( PLANAR_U );
    as_picture->linesize[2] = as_frame->GetPitch   ( PLANAR_V );
}

static int make_frame_planar_yuv
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { { NULL } } };
    as_assign_planar_yuv( as_frame, &as_picture );
    return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

static int make_frame_packed_yuv
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { { NULL } } };
    as_picture.data    [0] = as_frame->GetWritePtr();
    as_picture.linesize[0] = as_frame->GetPitch   ();
    return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

static int make_frame_packed_rgb
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { { NULL } } };
    as_picture.data    [0] = as_frame->GetWritePtr() + as_frame->GetPitch() * (as_frame->GetHeight() - 1);
    as_picture.linesize[0] = -as_frame->GetPitch();
    return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

enum AVPixelFormat get_av_output_pixel_format
(
    const char *format_name
)
{
    if( !format_name )
        return AV_PIX_FMT_NONE;
    static const struct
    {
        const char        *format_name;
        enum AVPixelFormat av_output_pixel_format;
    } format_table[] =
        {
            { "YUV420P8",  AV_PIX_FMT_YUV420P     },
            { "YUV422P8",  AV_PIX_FMT_YUV422P     },
            { "YUV444P8",  AV_PIX_FMT_YUV444P     },
            { "YUV410P8",  AV_PIX_FMT_YUV410P     },
            { "YUV411P8",  AV_PIX_FMT_YUV411P     },
            { "YUV440P8",  AV_PIX_FMT_YUV440P     },
            { "YUV420P9",  AV_PIX_FMT_YUV420P9LE  },
            { "YUV422P9",  AV_PIX_FMT_YUV422P9LE  },
            { "YUV444P9",  AV_PIX_FMT_YUV444P9LE  },
            { "YUV420P10", AV_PIX_FMT_YUV420P10LE },
            { "YUV422P10", AV_PIX_FMT_YUV422P10LE },
            { "YUV444P10", AV_PIX_FMT_YUV444P10LE },
            { "YUV420P16", AV_PIX_FMT_YUV420P16LE },
            { "YUV422P16", AV_PIX_FMT_YUV422P16LE },
            { "YUV444P16", AV_PIX_FMT_YUV444P16LE },
            { "YUY2",      AV_PIX_FMT_YUYV422     },
            { "Y8",        AV_PIX_FMT_GRAY8       },
            { "Y12",       AV_PIX_FMT_GRAY12LE    },
            { "Y16",       AV_PIX_FMT_GRAY16LE    },
            { "RGB24",     AV_PIX_FMT_BGR24       },
            { "RGB32",     AV_PIX_FMT_BGRA        },
            { "RGB48",     AV_PIX_FMT_BGR48LE     },
#if FFMPEG_HIGH_DEPTH_SUPPORT
            { "YUV420P12", AV_PIX_FMT_YUV420P12LE },
            { "YUV420P14", AV_PIX_FMT_YUV420P14LE },
            { "YUV422P12", AV_PIX_FMT_YUV422P12LE },
            { "YUV422P14", AV_PIX_FMT_YUV422P14LE },
            { "YUV444P12", AV_PIX_FMT_YUV444P12LE },
            { "YUV444P14", AV_PIX_FMT_YUV444P14LE },
            { "Y10",       AV_PIX_FMT_GRAY10LE    },
#endif
            { NULL,        AV_PIX_FMT_NONE        }
        };
    for( int i = 0; format_table[i].format_name; i++ )
        if( stricmp( format_name, format_table[i].format_name ) == 0 )
            return format_table[i].av_output_pixel_format;
    return AV_PIX_FMT_NONE;
}

static int determine_colorspace_conversion
(
    as_video_output_handler_t *as_vohp,
    enum AVPixelFormat         input_pixel_format,
    enum AVPixelFormat        *output_pixel_format,
    int                       *output_pixel_type
)
{
    avoid_yuv_scale_conversion( &input_pixel_format );
    const struct
    {
        enum AVPixelFormat input_pixel_format;
        enum AVPixelFormat output_pixel_format;
        int                output_pixel_type;
        int                output_bitdepth_minus_8;
        int                output_sub_width;
        int                output_sub_height;
    } conversion_table[] =
        {
            { AV_PIX_FMT_YUV420P,     AV_PIX_FMT_YUV420P,     VideoInfo::CS_I420,      0, 1, 1 },
            { AV_PIX_FMT_NV12,        AV_PIX_FMT_YUV420P,     VideoInfo::CS_I420,      0, 1, 1 },
            { AV_PIX_FMT_NV21,        AV_PIX_FMT_YUV420P,     VideoInfo::CS_I420,      0, 1, 1 },
            { AV_PIX_FMT_YUV420P9LE,  AV_PIX_FMT_YUV420P10LE, VideoInfo::CS_YUV420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUV420P9BE,  AV_PIX_FMT_YUV420P10LE, VideoInfo::CS_YUV420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV420P10LE, VideoInfo::CS_YUV420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV420P10LE, VideoInfo::CS_YUV420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUV420P16LE, AV_PIX_FMT_YUV420P16LE, VideoInfo::CS_YUV420P16, 8, 1, 1 },
            { AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUV420P16LE, VideoInfo::CS_YUV420P16, 8, 1, 1 },
            { AV_PIX_FMT_YUYV422,     AV_PIX_FMT_YUYV422,     VideoInfo::CS_YUY2,      0, 1, 0 },
            { AV_PIX_FMT_YUV422P,     AV_PIX_FMT_YUYV422,     VideoInfo::CS_YUY2,      0, 1, 0 },
            { AV_PIX_FMT_UYVY422,     AV_PIX_FMT_YUYV422,     VideoInfo::CS_YUY2,      0, 1, 0 },
            { AV_PIX_FMT_YUV422P,     AV_PIX_FMT_YUV422P,     VideoInfo::CS_YV16,      0, 1, 0 },
            { AV_PIX_FMT_YUV422P9LE,  AV_PIX_FMT_YUV422P10LE, VideoInfo::CS_YUV422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUV422P9BE,  AV_PIX_FMT_YUV422P10LE, VideoInfo::CS_YUV422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV422P10LE, VideoInfo::CS_YUV422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10LE, VideoInfo::CS_YUV422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUV422P16LE, AV_PIX_FMT_YUV422P16LE, VideoInfo::CS_YUV422P16, 8, 1, 0 },
            { AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUV422P16LE, VideoInfo::CS_YUV422P16, 8, 1, 0 },
            { AV_PIX_FMT_YUV444P,     AV_PIX_FMT_YUV444P,     VideoInfo::CS_YV24,      0, 0, 0 },
            { AV_PIX_FMT_YUV444P9LE,  AV_PIX_FMT_YUV444P10LE, VideoInfo::CS_YUV444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUV444P9BE,  AV_PIX_FMT_YUV444P10LE, VideoInfo::CS_YUV444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV444P10LE, VideoInfo::CS_YUV444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUV444P10LE, VideoInfo::CS_YUV444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUV444P16LE, VideoInfo::CS_YUV444P16, 8, 0, 0 },
            { AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16LE, VideoInfo::CS_YUV444P16, 8, 0, 0 },
            { AV_PIX_FMT_YUV410P,     AV_PIX_FMT_YUV410P,     VideoInfo::CS_YUV9,      0, 2, 2 },
            { AV_PIX_FMT_YUV411P,     AV_PIX_FMT_YUV411P,     VideoInfo::CS_YV411,     0, 2, 0 },
            { AV_PIX_FMT_UYYVYY411,   AV_PIX_FMT_YUV411P,     VideoInfo::CS_YV411,     0, 2, 0 },
            { AV_PIX_FMT_GRAY8,       AV_PIX_FMT_GRAY8,       VideoInfo::CS_Y8,        0, 0, 0 },
            { AV_PIX_FMT_GRAY12LE,    AV_PIX_FMT_GRAY12LE,    VideoInfo::CS_Y12,       4, 0, 0 },
            { AV_PIX_FMT_GRAY12BE,    AV_PIX_FMT_GRAY12LE,    VideoInfo::CS_Y12,       4, 0, 0 },
            { AV_PIX_FMT_GRAY16LE,    AV_PIX_FMT_GRAY16LE,    VideoInfo::CS_Y16,       8, 0, 0 },
            { AV_PIX_FMT_GRAY16BE,    AV_PIX_FMT_GRAY16LE,    VideoInfo::CS_Y16,       8, 0, 0 },
            { AV_PIX_FMT_RGB24,       AV_PIX_FMT_BGR24,       VideoInfo::CS_BGR24,     0, 0, 0 },
            { AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24,       VideoInfo::CS_BGR24,     0, 0, 0 },
            { AV_PIX_FMT_ARGB,        AV_PIX_FMT_BGRA,        VideoInfo::CS_BGR32,     0, 0, 0 },
            { AV_PIX_FMT_RGBA,        AV_PIX_FMT_BGRA,        VideoInfo::CS_BGR32,     0, 0, 0 },
            { AV_PIX_FMT_ABGR,        AV_PIX_FMT_BGRA,        VideoInfo::CS_BGR32,     0, 0, 0 },
            { AV_PIX_FMT_BGRA,        AV_PIX_FMT_BGRA,        VideoInfo::CS_BGR32,     0, 0, 0 },
            { AV_PIX_FMT_RGB48LE,     AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_BGR48BE,     AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_GBRP,        AV_PIX_FMT_BGR24,       VideoInfo::CS_BGR24,     0, 0, 0 },
            { AV_PIX_FMT_GBRP9BE,     AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_GBRP10LE,    AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_GBRP10BE,    AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_GBRP16LE,    AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
            { AV_PIX_FMT_GBRP16BE,    AV_PIX_FMT_BGR48LE,     VideoInfo::CS_BGR48,     8, 0, 0 },
#if FFMPEG_HIGH_DEPTH_SUPPORT
            { AV_PIX_FMT_YUV420P12LE, AV_PIX_FMT_YUV420P12LE, VideoInfo::CS_YUV420P12, 4, 1, 1 },
            { AV_PIX_FMT_YUV420P12BE, AV_PIX_FMT_YUV420P12LE, VideoInfo::CS_YUV420P12, 4, 1, 1 },
            { AV_PIX_FMT_YUV420P14LE, AV_PIX_FMT_YUV420P14LE, VideoInfo::CS_YUV420P14, 6, 1, 1 },
            { AV_PIX_FMT_YUV420P14BE, AV_PIX_FMT_YUV420P14LE, VideoInfo::CS_YUV420P14, 6, 1, 1 },
            { AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV422P12LE, VideoInfo::CS_YUV422P12, 4, 1, 0 },
            { AV_PIX_FMT_YUV422P12BE, AV_PIX_FMT_YUV422P12LE, VideoInfo::CS_YUV422P12, 4, 1, 0 },
            { AV_PIX_FMT_YUV422P14LE, AV_PIX_FMT_YUV422P14LE, VideoInfo::CS_YUV422P14, 6, 1, 0 },
            { AV_PIX_FMT_YUV422P14BE, AV_PIX_FMT_YUV422P14LE, VideoInfo::CS_YUV422P14, 6, 1, 0 },
            { AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV444P12LE, VideoInfo::CS_YUV444P12, 4, 0, 0 },
            { AV_PIX_FMT_YUV444P12BE, AV_PIX_FMT_YUV444P12LE, VideoInfo::CS_YUV444P12, 4, 0, 0 },
            { AV_PIX_FMT_YUV444P14LE, AV_PIX_FMT_YUV444P14LE, VideoInfo::CS_YUV444P14, 6, 0, 0 },
            { AV_PIX_FMT_YUV444P14BE, AV_PIX_FMT_YUV444P14LE, VideoInfo::CS_YUV444P14, 6, 0, 0 },
            { AV_PIX_FMT_GRAY10LE,    AV_PIX_FMT_GRAY10LE,    VideoInfo::CS_Y10,       2, 0, 0 },
            { AV_PIX_FMT_GRAY10BE,    AV_PIX_FMT_GRAY10LE,    VideoInfo::CS_Y10,       2, 0, 0 },
#endif
            { AV_PIX_FMT_NONE,        AV_PIX_FMT_NONE,        VideoInfo::CS_UNKNOWN,   0, 0, 0 }
        };
    as_vohp->bitdepth_minus_8 = 0;
    int i = 0;
    if( *output_pixel_format == AV_PIX_FMT_NONE )
    {
        for( i = 0; conversion_table[i].input_pixel_format != AV_PIX_FMT_NONE; i++ )
            if( conversion_table[i].input_pixel_format == input_pixel_format )
            {
                *output_pixel_format = conversion_table[i].output_pixel_format;
                break;
            }
    }
    else
    {
        for( i = 0; conversion_table[i].input_pixel_format != AV_PIX_FMT_NONE; i++ )
            if( conversion_table[i].output_pixel_format == *output_pixel_format )
                break;
    }
    *output_pixel_type        = conversion_table[i].output_pixel_type;
    as_vohp->bitdepth_minus_8 = conversion_table[i].output_bitdepth_minus_8;
    as_vohp->sub_width        = conversion_table[i].output_sub_width;
    as_vohp->sub_height       = conversion_table[i].output_sub_height;
    switch( *output_pixel_format )
    {
        case AV_PIX_FMT_YUV420P     :   /* planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples) */
        case AV_PIX_FMT_YUV422P     :   /* planar YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples) */
        case AV_PIX_FMT_YUV444P     :   /* planar YUV 4:4:4, 24bpp, (1 Cr & Cb sample per 1x1 Y samples) */
        case AV_PIX_FMT_YUV410P     :   /* planar YUV 4:1:0,  9bpp, (1 Cr & Cb sample per 4x4 Y samples) */
        case AV_PIX_FMT_YUV411P     :   /* planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples) */
            as_vohp->make_black_background = make_black_background_planar_yuv;
            as_vohp->make_frame            = make_frame_planar_yuv;
            return 0;
        case AV_PIX_FMT_YUYV422     :   /* packed YUV 4:2:2, 16bpp */
            as_vohp->make_black_background = make_black_background_packed_yuv422;
            as_vohp->make_frame            = make_frame_packed_yuv;
            return 0;
        case AV_PIX_FMT_YUV420P10LE :   /* planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian */
        case AV_PIX_FMT_YUV420P16LE :   /* planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian */
        case AV_PIX_FMT_YUV422P10LE :   /* planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian */
        case AV_PIX_FMT_YUV422P16LE :   /* planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian */
        case AV_PIX_FMT_YUV444P10LE :   /* planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian */
        case AV_PIX_FMT_YUV444P16LE :   /* planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian */
#if FFMPEG_HIGH_DEPTH_SUPPORT
        case AV_PIX_FMT_YUV420P12LE :
        case AV_PIX_FMT_YUV420P14LE :
        case AV_PIX_FMT_YUV422P12LE :
        case AV_PIX_FMT_YUV422P14LE :
        case AV_PIX_FMT_YUV444P12LE :
        case AV_PIX_FMT_YUV444P14LE :
#endif
            as_vohp->make_black_background = make_black_background_planar_yuv_interleaved;
            as_vohp->make_frame            = make_frame_planar_yuv;
            return 0;
        case AV_PIX_FMT_GRAY8       :   /* Y, 8bpp */
        case AV_PIX_FMT_GRAY12LE    :   /* Y, 12bpp */
        case AV_PIX_FMT_GRAY16LE    :   /* Y, 16bpp */
#if FFMPEG_HIGH_DEPTH_SUPPORT
        case AV_PIX_FMT_GRAY10LE    :   /* Y, 10bpp */
#endif
            as_vohp->make_black_background = make_black_background_packed_all_zero;
            as_vohp->make_frame            = make_frame_packed_yuv;
            return 0;
        case AV_PIX_FMT_BGR24       :   /* packed RGB 8:8:8, 24bpp, BGRBGR... */
        case AV_PIX_FMT_BGRA        :   /* packed BGRA 8:8:8:8, 32bpp, BGRABGRA... */
        case AV_PIX_FMT_BGR48LE     :   /* packed RGB 16:16:16, 48bpp, BGRBGR... */
            as_vohp->make_black_background = make_black_background_packed_all_zero;
            as_vohp->make_frame            = make_frame_packed_rgb;
            return 0;
        default :
            as_vohp->make_black_background = NULL;
            as_vohp->make_frame            = NULL;
            return -1;
    }
}

int make_frame
(
    lw_video_output_handler_t *vohp,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame,
    IScriptEnvironment        *env
)
{
    if( av_frame->opaque )
    {
        /* Render a video frame from the decoder directly. */
        as_video_buffer_handler_t *as_vbhp = (as_video_buffer_handler_t *)av_frame->opaque;
        as_frame = as_vbhp->as_frame_buffer;
        return 0;
    }
    /* Render a video frame through the scaler from the decoder.
     * We don't change the presentation resolution. */
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)vohp->private_handler;
    as_frame = env->NewVideoFrame( *as_vohp->vi, 32 );
    if( vohp->output_width  != av_frame->width || vohp->output_height != av_frame->height )
        as_vohp->make_black_background( as_frame, as_vohp->bitdepth_minus_8 );
    return as_vohp->make_frame( vohp, av_frame->height, av_frame, as_frame );
}

static int as_check_dr_available
(
    AVCodecContext    *ctx,
    enum AVPixelFormat pixel_format
 )
{
    if( !(ctx->codec->capabilities & AV_CODEC_CAP_DR1) )
        return 0;
    static enum AVPixelFormat dr_support_pix_fmt[] =
    {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV420P9LE,
        AV_PIX_FMT_YUV420P10LE,
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV422P9LE,
        AV_PIX_FMT_YUV422P10LE,
        AV_PIX_FMT_YUV422P16LE,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV444P9LE,
        AV_PIX_FMT_YUV444P10LE,
        AV_PIX_FMT_YUV444P16LE,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_BGRA,
#if FFMPEG_HIGH_DEPTH_SUPPORT
        AV_PIX_FMT_YUV420P12LE,
        AV_PIX_FMT_YUV420P14LE,
        AV_PIX_FMT_YUV422P12LE,
        AV_PIX_FMT_YUV422P14LE,
        AV_PIX_FMT_YUV444P12LE,
        AV_PIX_FMT_YUV444P14LE,
#endif
        AV_PIX_FMT_NONE
    };
    for( int i = 0; dr_support_pix_fmt[i] != AV_PIX_FMT_NONE; i++ )
        if( dr_support_pix_fmt[i] == pixel_format )
            return 1;
    return 0;
}

static void as_video_release_buffer_handler
(
    void    *opaque,
    uint8_t *data
)
{
    as_video_buffer_handler_t *as_vbhp = (as_video_buffer_handler_t *)opaque;
    delete as_vbhp;
}

static void as_video_unref_buffer_handler
(
    void    *opaque,
    uint8_t *data
)
{
    /* Decrement the reference-counter to the video buffer handler by 1.
     * Delete it by as_video_release_buffer_handler() if there are no reference to it i.e. the reference-counter equals zero. */
    AVBufferRef *as_buffer_ref = (AVBufferRef *)opaque;
    av_buffer_unref( &as_buffer_ref );
}

static int as_video_get_buffer
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int             flags
)
{
    lw_video_output_handler_t *lw_vohp = (lw_video_output_handler_t *)ctx->opaque;
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)lw_vohp->private_handler;
    lw_video_scaler_handler_t *vshp    = &lw_vohp->scaler;
    enum AVPixelFormat pix_fmt = ctx->pix_fmt;
    avoid_yuv_scale_conversion( &pix_fmt );
    av_frame->format = pix_fmt; /* Don't use AV_PIX_FMT_YUVJ*. */
    if( vshp->output_pixel_format != pix_fmt
     || !as_check_dr_available( ctx, pix_fmt ) )
        return avcodec_default_get_buffer2( ctx, av_frame, 0 );
    /* New AviSynth video frame buffer. */
    as_video_buffer_handler_t *as_vbhp = new as_video_buffer_handler_t;
    if( !as_vbhp )
    {
        av_frame_unref( av_frame );
        return AVERROR( ENOMEM );
    }
    av_frame->opaque = as_vbhp;
    as_vbhp->as_frame_buffer = as_vohp->env->NewVideoFrame( *as_vohp->vi, 32 );
    int aligned_width  = ctx->width << (as_vohp->bitdepth_minus_8 ? 1 : 0);
    int aligned_height = ctx->height;
    avcodec_align_dimensions2( ctx, &aligned_width, &aligned_height, av_frame->linesize );
    if( lw_vohp->output_width != aligned_width || lw_vohp->output_height != aligned_height )
        as_vohp->make_black_background( as_vbhp->as_frame_buffer, as_vohp->bitdepth_minus_8 );
    /* Create frame buffers for the decoder.
     * The callback as_video_release_buffer_handler() shall be called when no reference to the video buffer handler is present.
     * The callback as_video_unref_buffer_handler() decrements the reference-counter by 1. */
    memset( av_frame->buf,      0, sizeof(av_frame->buf) );
    memset( av_frame->data,     0, sizeof(av_frame->data) );
    memset( av_frame->linesize, 0, sizeof(av_frame->linesize) );
    AVBufferRef *as_buffer_handler = av_buffer_create( NULL, 0, as_video_release_buffer_handler, as_vbhp, 0 );
    if( !as_buffer_handler )
    {
        delete as_vbhp;
        av_frame_unref( av_frame );
        return AVERROR( ENOMEM );
    }
#define CREATE_PLANE_BUFFER( PLANE, PLANE_ID )                                                      \
    do                                                                                              \
    {                                                                                               \
        AVBufferRef *as_buffer_ref = av_buffer_ref( as_buffer_handler );                            \
        if( !as_buffer_ref )                                                                        \
        {                                                                                           \
            av_buffer_unref( &as_buffer_handler );                                                  \
            goto fail;                                                                              \
        }                                                                                           \
        av_frame->linesize[PLANE] = as_vbhp->as_frame_buffer->GetPitch( PLANE_ID );                 \
        int as_plane_size = as_vbhp->as_frame_buffer->GetHeight( PLANE_ID )                         \
                          * av_frame->linesize[PLANE];                                              \
        av_frame->buf[PLANE] = av_buffer_create( as_vbhp->as_frame_buffer->GetWritePtr( PLANE_ID ), \
                                                 as_plane_size,                                     \
                                                 as_video_unref_buffer_handler,                     \
                                                 as_buffer_ref,                                     \
                                                 0 );                                               \
        if( !av_frame->buf[PLANE] )                                                                 \
            goto fail;                                                                              \
        av_frame->data[PLANE] = av_frame->buf[PLANE]->data;                                         \
    } while( 0 )
    if( as_vohp->vi->pixel_type & VideoInfo::CS_INTERLEAVED )
        CREATE_PLANE_BUFFER( 0, );
    else
        for( int i = 0; i < 3; i++ )
        {
            static const int as_plane[3] = { PLANAR_Y, PLANAR_U, PLANAR_V };
            CREATE_PLANE_BUFFER( i, as_plane[i] );
        }
    /* Here, a variable 'as_buffer_handler' itself is not referenced by any pointer. */
    av_buffer_unref( &as_buffer_handler );
#undef CREATE_PLANE_BUFFER
    av_frame->nb_extended_buf = 0;
    av_frame->extended_data   = av_frame->data;
    return 0;
fail:
    av_frame_unref( av_frame );
    av_buffer_unref( &as_buffer_handler );
    return AVERROR( ENOMEM );
}

void as_free_video_output_handler
(
    void *private_handler
)
{
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)private_handler;
    if( !as_vohp )
        return;
    av_freep( &as_vohp->scaled.data[0] );
    lw_free( as_vohp );
}

void as_setup_video_rendering
(
    lw_video_output_handler_t *vohp,
    AVCodecContext            *ctx,
    const char                *filter_name,
    int                        direct_rendering,
    enum AVPixelFormat         output_pixel_format,
    int                        output_width,
    int                        output_height
)
{
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)vohp->private_handler;
    IScriptEnvironment        *env     = as_vohp->env;
    VideoInfo                 *vi      = as_vohp->vi;
    if( determine_colorspace_conversion( as_vohp, ctx->pix_fmt, &output_pixel_format, &vi->pixel_type ) < 0 )
        env->ThrowError( "%s: %s is not supported", filter_name, av_get_pix_fmt_name( ctx->pix_fmt ) );
    vohp->scaler.output_pixel_format = output_pixel_format;
    enum AVPixelFormat input_pixel_format = ctx->pix_fmt;
    avoid_yuv_scale_conversion( &input_pixel_format );
    direct_rendering &= as_check_dr_available( ctx, input_pixel_format );
    int (*dr_get_buffer)( struct AVCodecContext *, AVFrame *, int ) = direct_rendering ? as_video_get_buffer : NULL;
    setup_video_rendering( vohp, SWS_FAST_BILINEAR,
                           output_width, output_height, output_pixel_format,
                           ctx, dr_get_buffer );
    /* Set the dimensions of AviSynth frame buffer. */
    vi->width  = vohp->output_width;
    vi->height = vohp->output_height;
}
