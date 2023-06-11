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

#ifdef _MSC_VER
#include <string.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

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
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/mastering_display_metadata.h>
}

#include "../common/lwsimd.h"

#include "video_output.h"

static inline __m128i _MM_PACKUS_EPI32( const __m128i &low, const __m128i &high )
{
    const __m128i val_32 = _mm_set1_epi32( 0x8000 );
    const __m128i val_16 = _mm_set1_epi16( 0x8000 );
    const __m128i low1   = _mm_sub_epi32( low, val_32 );
    const __m128i high1  = _mm_sub_epi32( high, val_32 );
    return _mm_add_epi16( _mm_packs_epi32( low1, high1 ), val_16 );
}

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

static void make_black_background_planar_yuva
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_Y ), 0x00,                   frame->GetPitch( PLANAR_Y ) * frame->GetHeight( PLANAR_Y ) );
    memset( frame->GetWritePtr( PLANAR_U ), 0x80<<bitdepth_minus_8, frame->GetPitch( PLANAR_U ) * frame->GetHeight( PLANAR_U ) );
    memset( frame->GetWritePtr( PLANAR_V ), 0x80<<bitdepth_minus_8, frame->GetPitch( PLANAR_V ) * frame->GetHeight( PLANAR_V ) );
    memset( frame->GetWritePtr( PLANAR_A ), 0xff,                   frame->GetPitch( PLANAR_A ) * frame->GetHeight( PLANAR_A ) );
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

static void make_black_background_planar_yuva_interleaved
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_Y ), 0x00, frame->GetPitch( PLANAR_Y ) * frame->GetHeight( PLANAR_Y ) );
    memset( frame->GetWritePtr( PLANAR_A ), 0xff, frame->GetPitch( PLANAR_A ) * frame->GetHeight( PLANAR_A ) );
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

static void make_black_background_planar_rgb
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_G ), 0x00, frame->GetPitch( PLANAR_G ) * frame->GetHeight( PLANAR_G ) );
    memset( frame->GetWritePtr( PLANAR_B ), 0x00, frame->GetPitch( PLANAR_B ) * frame->GetHeight( PLANAR_B ) );
    memset( frame->GetWritePtr( PLANAR_R ), 0x00, frame->GetPitch( PLANAR_R ) * frame->GetHeight( PLANAR_R ) );
}

static void make_black_background_planar_rgba
(
    PVideoFrame &frame,
    int          bitdepth_minus_8
)
{
    memset( frame->GetWritePtr( PLANAR_G ), 0x00, frame->GetPitch( PLANAR_G ) * frame->GetHeight( PLANAR_G ) );
    memset( frame->GetWritePtr( PLANAR_B ), 0x00, frame->GetPitch( PLANAR_B ) * frame->GetHeight( PLANAR_B ) );
    memset( frame->GetWritePtr( PLANAR_R ), 0x00, frame->GetPitch( PLANAR_R ) * frame->GetHeight( PLANAR_R ) );
    memset( frame->GetWritePtr( PLANAR_A ), 0xff, frame->GetPitch( PLANAR_A ) * frame->GetHeight( PLANAR_A ) );
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

static int make_frame_planar_yuv
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { NULL } };
    as_picture.data    [0] = as_frame->GetWritePtr( PLANAR_Y );
    as_picture.data    [1] = as_frame->GetWritePtr( PLANAR_U );
    as_picture.data    [2] = as_frame->GetWritePtr( PLANAR_V );
    as_picture.linesize[0] = as_frame->GetPitch   ( PLANAR_Y );
    as_picture.linesize[1] = as_frame->GetPitch   ( PLANAR_U );
    as_picture.linesize[2] = as_frame->GetPitch   ( PLANAR_V );
    if( vohp->scaler.input_pixel_format == AV_PIX_FMT_P010LE && vohp->scaler.output_pixel_format == AV_PIX_FMT_YUV420P10LE )
    {
        const int width_y       = as_frame->GetRowSize( PLANAR_Y ) / sizeof( uint16_t );
        const int width_uv      = as_frame->GetRowSize( PLANAR_U ) / sizeof( uint16_t );
        const int height_y      = as_frame->GetHeight( PLANAR_Y );
        const int height_uv     = as_frame->GetHeight( PLANAR_U );
        const int src_pitch_y   = av_frame->linesize[0] / sizeof( uint16_t );
        const int src_pitch_uv  = av_frame->linesize[1] / sizeof( uint16_t );
        const int dst_pitch_y   = as_picture.linesize[0] / sizeof( uint16_t );
        const int dst_pitch_uv  = as_picture.linesize[1] / sizeof( uint16_t );
        uint16_t *srcp_y        = (uint16_t *)av_frame->data[0];
        uint16_t *srcp_uv       = (uint16_t *)av_frame->data[1];
        uint16_t *dstp_y        = (uint16_t *)as_picture.data[0];
        uint16_t *dstp_u        = (uint16_t *)as_picture.data[1];
        uint16_t *dstp_v        = (uint16_t *)as_picture.data[2];

        for( int y = 0; y < height_y; y++ )
        {
            for( int x = 0; x < width_y; x += 8 )
            {
                __m128i yy = _mm_load_si128( (const __m128i *)(srcp_y + x) );
                yy         = _mm_srli_epi16( yy, 6 );
                _mm_stream_si128( (__m128i *)(dstp_y + x), yy );
            }
            srcp_y += src_pitch_y;
            dstp_y += dst_pitch_y;
        }

        const __m128i mask = _mm_set1_epi32(0x0000FFFF);
        for( int y = 0; y < height_uv; y++ )
        {
            for( int x = 0; x < width_uv; x += 8 )
            {
                __m128i uv_low  = _mm_load_si128( (__m128i *)((uint32_t *)srcp_uv + x + 0) );
                __m128i uv_high = _mm_load_si128( (__m128i *)((uint32_t *)srcp_uv + x + 4) );

                __m128i u_low  = _mm_and_si128( uv_low, mask );
                __m128i u_high = _mm_and_si128( uv_high, mask );
                __m128i u      = _MM_PACKUS_EPI32( u_low, u_high );
                u              = _mm_srli_epi16( u, 6 );
                _mm_stream_si128( (__m128i *)(dstp_u + x), u );

                __m128i v_low  = _mm_srli_epi32( uv_low, 16 );
                __m128i v_high = _mm_srli_epi32( uv_high, 16 );
                __m128i v      = _MM_PACKUS_EPI32( v_low, v_high );
                v              = _mm_srli_epi16( v, 6 );
                _mm_stream_si128( (__m128i *)(dstp_v + x), v );
            }
            srcp_uv += src_pitch_uv;
            dstp_u  += dst_pitch_uv;
            dstp_v  += dst_pitch_uv;
        }

        return height_y;
    }
    else
        return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

static int make_frame_planar_yuva
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { NULL } };
    as_picture.data    [0] = as_frame->GetWritePtr( PLANAR_Y );
    as_picture.data    [1] = as_frame->GetWritePtr( PLANAR_U );
    as_picture.data    [2] = as_frame->GetWritePtr( PLANAR_V );
    as_picture.data    [3] = as_frame->GetWritePtr( PLANAR_A );
    as_picture.linesize[0] = as_frame->GetPitch   ( PLANAR_Y );
    as_picture.linesize[1] = as_frame->GetPitch   ( PLANAR_U );
    as_picture.linesize[2] = as_frame->GetPitch   ( PLANAR_V );
    as_picture.linesize[3] = as_frame->GetPitch   ( PLANAR_A );
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
    as_picture_t as_picture = { { NULL } };
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
    as_picture_t as_picture = { { NULL } };
    as_picture.data    [0] = as_frame->GetWritePtr() + as_frame->GetPitch() * (as_frame->GetHeight() - 1);
    as_picture.linesize[0] = -as_frame->GetPitch();
    return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

static int make_frame_planar_rgb
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { NULL } };
    as_picture.data    [0] = as_frame->GetWritePtr( PLANAR_G );
    as_picture.data    [1] = as_frame->GetWritePtr( PLANAR_B );
    as_picture.data    [2] = as_frame->GetWritePtr( PLANAR_R );
    as_picture.linesize[0] = as_frame->GetPitch   ( PLANAR_G );
    as_picture.linesize[1] = as_frame->GetPitch   ( PLANAR_B );
    as_picture.linesize[2] = as_frame->GetPitch   ( PLANAR_R );
    return convert_av_pixel_format( vohp->scaler.sws_ctx, height, av_frame, &as_picture );
}

static int make_frame_planar_rgba
(
    lw_video_output_handler_t *vohp,
    int                        height,
    AVFrame                   *av_frame,
    PVideoFrame               &as_frame
)
{
    as_picture_t as_picture = { { NULL } };
    as_picture.data    [0] = as_frame->GetWritePtr( PLANAR_G );
    as_picture.data    [1] = as_frame->GetWritePtr( PLANAR_B );
    as_picture.data    [2] = as_frame->GetWritePtr( PLANAR_R );
    as_picture.data    [3] = as_frame->GetWritePtr( PLANAR_A );
    as_picture.linesize[0] = as_frame->GetPitch   ( PLANAR_G );
    as_picture.linesize[1] = as_frame->GetPitch   ( PLANAR_B );
    as_picture.linesize[2] = as_frame->GetPitch   ( PLANAR_R );
    as_picture.linesize[3] = as_frame->GetPitch   ( PLANAR_A );
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
            { "YUV420P8",   AV_PIX_FMT_YUV420P      },
            { "YUV422P8",   AV_PIX_FMT_YUV422P      },
            { "YUV444P8",   AV_PIX_FMT_YUV444P      },
            { "YUV410P8",   AV_PIX_FMT_YUV410P      },
            { "YUV411P8",   AV_PIX_FMT_YUV411P      },
            { "YUV420P9",   AV_PIX_FMT_YUV420P9LE   },
            { "YUV422P9",   AV_PIX_FMT_YUV422P9LE   },
            { "YUV444P9",   AV_PIX_FMT_YUV444P9LE   },
            { "YUV420P10",  AV_PIX_FMT_YUV420P10LE  },
            { "YUV422P10",  AV_PIX_FMT_YUV422P10LE  },
            { "YUV444P10",  AV_PIX_FMT_YUV444P10LE  },
            { "YUV420P12",  AV_PIX_FMT_YUV420P12LE  },
            { "YUV422P12",  AV_PIX_FMT_YUV422P12LE  },
            { "YUV444P12",  AV_PIX_FMT_YUV444P12LE  },
            { "YUV420P14",  AV_PIX_FMT_YUV420P14LE  },
            { "YUV422P14",  AV_PIX_FMT_YUV422P14LE  },
            { "YUV444P14",  AV_PIX_FMT_YUV444P14LE  },
            { "YUV420P16",  AV_PIX_FMT_YUV420P16LE  },
            { "YUV422P16",  AV_PIX_FMT_YUV422P16LE  },
            { "YUV444P16",  AV_PIX_FMT_YUV444P16LE  },
            { "YUVA420P8",  AV_PIX_FMT_YUVA420P     },
            { "YUVA422P8",  AV_PIX_FMT_YUVA422P     },
            { "YUVA444P8",  AV_PIX_FMT_YUVA444P     },
            { "YUVA420P10", AV_PIX_FMT_YUVA420P10LE },
            { "YUVA422P10", AV_PIX_FMT_YUVA422P10LE },
            { "YUVA444P10", AV_PIX_FMT_YUVA444P10LE },
            { "YUVA422P12", AV_PIX_FMT_YUVA422P12LE },
            { "YUVA444P12", AV_PIX_FMT_YUVA444P12LE },
            { "YUVA420P16", AV_PIX_FMT_YUVA420P16LE },
            { "YUVA422P16", AV_PIX_FMT_YUVA422P16LE },
            { "YUVA444P16", AV_PIX_FMT_YUVA444P16LE },
            { "YUY2",       AV_PIX_FMT_YUYV422      },
            { "Y8",         AV_PIX_FMT_GRAY8        },
            { "Y10",        AV_PIX_FMT_GRAY10LE     },
            { "Y12",        AV_PIX_FMT_GRAY12LE     },
            { "Y14",        AV_PIX_FMT_GRAY14LE     },
            { "Y16",        AV_PIX_FMT_GRAY16LE     },
            { "RGB24",      AV_PIX_FMT_BGR24        },
            { "RGB32",      AV_PIX_FMT_BGRA         },
            { "RGB48",      AV_PIX_FMT_BGR48LE      },
            { "RGB64",      AV_PIX_FMT_BGRA64LE     },
            { "GBRP8",      AV_PIX_FMT_GBRP         },
            { "GBRP10",     AV_PIX_FMT_GBRP10LE     },
            { "GBRP12",     AV_PIX_FMT_GBRP12LE     },
            { "GBRP14",     AV_PIX_FMT_GBRP14LE     },
            { "GBRP16",     AV_PIX_FMT_GBRP16LE     },
            { "GBRAP8",     AV_PIX_FMT_GBRAP        },
            { "GBRAP10",    AV_PIX_FMT_GBRAP10LE    },
            { "GBRAP12",    AV_PIX_FMT_GBRAP12LE    },
            { "GBRAP16",    AV_PIX_FMT_GBRAP16LE    },
            { "XYZ12LE",    AV_PIX_FMT_XYZ12LE      },
            { NULL,         AV_PIX_FMT_NONE         }
        };
    for( int i = 0; format_table[i].format_name; i++ )
        if( strcasecmp( format_name, format_table[i].format_name ) == 0 )
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
            { AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV420P,      VideoInfo::CS_I420,       0, 1, 1 },
            { AV_PIX_FMT_NV12,         AV_PIX_FMT_YUV420P,      VideoInfo::CS_I420,       0, 1, 1 },
            { AV_PIX_FMT_NV21,         AV_PIX_FMT_YUV420P,      VideoInfo::CS_I420,       0, 1, 1 },
            { AV_PIX_FMT_YUV420P9LE,   AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_YUV420P9BE,   AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_YUV420P10LE,  AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_YUV420P10BE,  AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_P010LE,       AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_P010BE,       AV_PIX_FMT_YUV420P10LE,  VideoInfo::CS_YUV420P10,  2, 1, 1 },
            { AV_PIX_FMT_YUV420P12LE,  AV_PIX_FMT_YUV420P12LE,  VideoInfo::CS_YUV420P12,  4, 1, 1 },
            { AV_PIX_FMT_YUV420P12BE,  AV_PIX_FMT_YUV420P12LE,  VideoInfo::CS_YUV420P12,  4, 1, 1 },
            { AV_PIX_FMT_YUV420P14LE,  AV_PIX_FMT_YUV420P14LE,  VideoInfo::CS_YUV420P14,  6, 1, 1 },
            { AV_PIX_FMT_YUV420P14BE,  AV_PIX_FMT_YUV420P14LE,  VideoInfo::CS_YUV420P14,  6, 1, 1 },
            { AV_PIX_FMT_YUV420P16LE,  AV_PIX_FMT_YUV420P16LE,  VideoInfo::CS_YUV420P16,  8, 1, 1 },
            { AV_PIX_FMT_YUV420P16BE,  AV_PIX_FMT_YUV420P16LE,  VideoInfo::CS_YUV420P16,  8, 1, 1 },
            { AV_PIX_FMT_P016LE,       AV_PIX_FMT_YUV420P16LE,  VideoInfo::CS_YUV420P16,  8, 1, 1 },
            { AV_PIX_FMT_P016BE,       AV_PIX_FMT_YUV420P16LE,  VideoInfo::CS_YUV420P16,  8, 1, 1 },
            { AV_PIX_FMT_YUYV422,      AV_PIX_FMT_YUYV422,      VideoInfo::CS_YUY2,       0, 1, 0 },
            { AV_PIX_FMT_UYVY422,      AV_PIX_FMT_YUYV422,      VideoInfo::CS_YUY2,       0, 1, 0 },
            { AV_PIX_FMT_YVYU422,      AV_PIX_FMT_YUYV422,      VideoInfo::CS_YUY2,       0, 1, 0 },
            { AV_PIX_FMT_YUV422P,      AV_PIX_FMT_YUV422P,      VideoInfo::CS_YV16,       0, 1, 0 },
            { AV_PIX_FMT_NV16,         AV_PIX_FMT_YUV422P,      VideoInfo::CS_YV16,       0, 1, 0 },
            { AV_PIX_FMT_YUV422P9LE,   AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_YUV422P9BE,   AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_YUV422P10LE,  AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_YUV422P10BE,  AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_NV20LE,       AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_NV20BE,       AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_Y210LE,       AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_Y210BE,       AV_PIX_FMT_YUV422P10LE,  VideoInfo::CS_YUV422P10,  2, 1, 0 },
            { AV_PIX_FMT_YUV422P12LE,  AV_PIX_FMT_YUV422P12LE,  VideoInfo::CS_YUV422P12,  4, 1, 0 },
            { AV_PIX_FMT_YUV422P12BE,  AV_PIX_FMT_YUV422P12LE,  VideoInfo::CS_YUV422P12,  4, 1, 0 },
            { AV_PIX_FMT_YUV422P14LE,  AV_PIX_FMT_YUV422P14LE,  VideoInfo::CS_YUV422P14,  6, 1, 0 },
            { AV_PIX_FMT_YUV422P14BE,  AV_PIX_FMT_YUV422P14LE,  VideoInfo::CS_YUV422P14,  6, 1, 0 },
            { AV_PIX_FMT_YUV422P16LE,  AV_PIX_FMT_YUV422P16LE,  VideoInfo::CS_YUV422P16,  8, 1, 0 },
            { AV_PIX_FMT_YUV422P16BE,  AV_PIX_FMT_YUV422P16LE,  VideoInfo::CS_YUV422P16,  8, 1, 0 },
            { AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV444P,      VideoInfo::CS_YV24,       0, 0, 0 },
            { AV_PIX_FMT_NV24,         AV_PIX_FMT_YUV444P,      VideoInfo::CS_YV24,       0, 0, 0 },
            { AV_PIX_FMT_NV42,         AV_PIX_FMT_YUV444P,      VideoInfo::CS_YV24,       0, 0, 0 },
            { AV_PIX_FMT_YUV444P9LE,   AV_PIX_FMT_YUV444P10LE,  VideoInfo::CS_YUV444P10,  2, 0, 0 },
            { AV_PIX_FMT_YUV444P9BE,   AV_PIX_FMT_YUV444P10LE,  VideoInfo::CS_YUV444P10,  2, 0, 0 },
            { AV_PIX_FMT_YUV444P10LE,  AV_PIX_FMT_YUV444P10LE,  VideoInfo::CS_YUV444P10,  2, 0, 0 },
            { AV_PIX_FMT_YUV444P10BE,  AV_PIX_FMT_YUV444P10LE,  VideoInfo::CS_YUV444P10,  2, 0, 0 },
            { AV_PIX_FMT_YUV444P12LE,  AV_PIX_FMT_YUV444P12LE,  VideoInfo::CS_YUV444P12,  4, 0, 0 },
            { AV_PIX_FMT_YUV444P12BE,  AV_PIX_FMT_YUV444P12LE,  VideoInfo::CS_YUV444P12,  4, 0, 0 },
            { AV_PIX_FMT_YUV444P14LE,  AV_PIX_FMT_YUV444P14LE,  VideoInfo::CS_YUV444P14,  6, 0, 0 },
            { AV_PIX_FMT_YUV444P14BE,  AV_PIX_FMT_YUV444P14LE,  VideoInfo::CS_YUV444P14,  6, 0, 0 },
            { AV_PIX_FMT_YUV444P16LE,  AV_PIX_FMT_YUV444P16LE,  VideoInfo::CS_YUV444P16,  8, 0, 0 },
            { AV_PIX_FMT_YUV444P16BE,  AV_PIX_FMT_YUV444P16LE,  VideoInfo::CS_YUV444P16,  8, 0, 0 },
            { AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV410P,      VideoInfo::CS_YUV9,       0, 2, 2 },
            { AV_PIX_FMT_YUV411P,      AV_PIX_FMT_YUV411P,      VideoInfo::CS_YV411,      0, 2, 0 },
            { AV_PIX_FMT_UYYVYY411,    AV_PIX_FMT_YUV411P,      VideoInfo::CS_YV411,      0, 2, 0 },
            { AV_PIX_FMT_YUVA420P,     AV_PIX_FMT_YUVA420P,     VideoInfo::CS_YUVA420,    0, 1, 1 },
            { AV_PIX_FMT_YUVA420P9LE,  AV_PIX_FMT_YUVA420P10LE, VideoInfo::CS_YUVA420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUVA420P9BE,  AV_PIX_FMT_YUVA420P10LE, VideoInfo::CS_YUVA420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUVA420P10LE, AV_PIX_FMT_YUVA420P10LE, VideoInfo::CS_YUVA420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUVA420P10BE, AV_PIX_FMT_YUVA420P10LE, VideoInfo::CS_YUVA420P10, 2, 1, 1 },
            { AV_PIX_FMT_YUVA420P16LE, AV_PIX_FMT_YUVA420P16LE, VideoInfo::CS_YUVA420P16, 8, 1, 1 },
            { AV_PIX_FMT_YUVA420P16BE, AV_PIX_FMT_YUVA420P16LE, VideoInfo::CS_YUVA420P16, 8, 1, 1 },
            { AV_PIX_FMT_YUVA422P,     AV_PIX_FMT_YUVA422P,     VideoInfo::CS_YUVA422,    0, 1, 0 },
            { AV_PIX_FMT_YUVA422P9LE,  AV_PIX_FMT_YUVA422P10LE, VideoInfo::CS_YUVA422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUVA422P9BE,  AV_PIX_FMT_YUVA422P10LE, VideoInfo::CS_YUVA422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUVA422P10LE, AV_PIX_FMT_YUVA422P10LE, VideoInfo::CS_YUVA422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUVA422P10BE, AV_PIX_FMT_YUVA422P10LE, VideoInfo::CS_YUVA422P10, 2, 1, 0 },
            { AV_PIX_FMT_YUVA422P12LE, AV_PIX_FMT_YUVA422P12LE, VideoInfo::CS_YUVA422P12, 4, 1, 0 },
            { AV_PIX_FMT_YUVA422P12BE, AV_PIX_FMT_YUVA422P12LE, VideoInfo::CS_YUVA422P12, 4, 1, 0 },
            { AV_PIX_FMT_YUVA422P16LE, AV_PIX_FMT_YUVA422P16LE, VideoInfo::CS_YUVA422P16, 8, 1, 0 },
            { AV_PIX_FMT_YUVA422P16BE, AV_PIX_FMT_YUVA422P16LE, VideoInfo::CS_YUVA422P16, 8, 1, 0 },
            { AV_PIX_FMT_YUVA444P,     AV_PIX_FMT_YUVA444P,     VideoInfo::CS_YUVA444,    0, 0, 0 },
            { AV_PIX_FMT_YUVA444P9LE,  AV_PIX_FMT_YUVA444P10LE, VideoInfo::CS_YUVA444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUVA444P9BE,  AV_PIX_FMT_YUVA444P10LE, VideoInfo::CS_YUVA444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUVA444P10LE, AV_PIX_FMT_YUVA444P10LE, VideoInfo::CS_YUVA444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUVA444P10BE, AV_PIX_FMT_YUVA444P10LE, VideoInfo::CS_YUVA444P10, 2, 0, 0 },
            { AV_PIX_FMT_YUVA444P12LE, AV_PIX_FMT_YUVA444P12LE, VideoInfo::CS_YUVA444P12, 4, 0, 0 },
            { AV_PIX_FMT_YUVA444P12BE, AV_PIX_FMT_YUVA444P12LE, VideoInfo::CS_YUVA444P12, 4, 0, 0 },
            { AV_PIX_FMT_YUVA444P16LE, AV_PIX_FMT_YUVA444P16LE, VideoInfo::CS_YUVA444P16, 8, 0, 0 },
            { AV_PIX_FMT_YUVA444P16BE, AV_PIX_FMT_YUVA444P16LE, VideoInfo::CS_YUVA444P16, 8, 0, 0 },
            { AV_PIX_FMT_GRAY8,        AV_PIX_FMT_GRAY8,        VideoInfo::CS_Y8,         0, 0, 0 },
            { AV_PIX_FMT_GRAY9LE,      AV_PIX_FMT_GRAY10LE,     VideoInfo::CS_Y10,        2, 0, 0 },
            { AV_PIX_FMT_GRAY9BE,      AV_PIX_FMT_GRAY10LE,     VideoInfo::CS_Y10,        2, 0, 0 },
            { AV_PIX_FMT_GRAY10LE,     AV_PIX_FMT_GRAY10LE,     VideoInfo::CS_Y10,        2, 0, 0 },
            { AV_PIX_FMT_GRAY10BE,     AV_PIX_FMT_GRAY10LE,     VideoInfo::CS_Y10,        2, 0, 0 },
            { AV_PIX_FMT_GRAY12LE,     AV_PIX_FMT_GRAY12LE,     VideoInfo::CS_Y12,        4, 0, 0 },
            { AV_PIX_FMT_GRAY12BE,     AV_PIX_FMT_GRAY12LE,     VideoInfo::CS_Y12,        4, 0, 0 },
            { AV_PIX_FMT_GRAY14LE,     AV_PIX_FMT_GRAY14LE,     VideoInfo::CS_Y14,        6, 0, 0 },
            { AV_PIX_FMT_GRAY14BE,     AV_PIX_FMT_GRAY14LE,     VideoInfo::CS_Y14,        6, 0, 0 },
            { AV_PIX_FMT_GRAY16LE,     AV_PIX_FMT_GRAY16LE,     VideoInfo::CS_Y16,        8, 0, 0 },
            { AV_PIX_FMT_GRAY16BE,     AV_PIX_FMT_GRAY16LE,     VideoInfo::CS_Y16,        8, 0, 0 },
            { AV_PIX_FMT_RGB24,        AV_PIX_FMT_BGR24,        VideoInfo::CS_BGR24,      0, 0, 0 },
            { AV_PIX_FMT_BGR24,        AV_PIX_FMT_BGR24,        VideoInfo::CS_BGR24,      0, 0, 0 },
            { AV_PIX_FMT_ARGB,         AV_PIX_FMT_BGRA,         VideoInfo::CS_BGR32,      0, 0, 0 },
            { AV_PIX_FMT_RGBA,         AV_PIX_FMT_BGRA,         VideoInfo::CS_BGR32,      0, 0, 0 },
            { AV_PIX_FMT_ABGR,         AV_PIX_FMT_BGRA,         VideoInfo::CS_BGR32,      0, 0, 0 },
            { AV_PIX_FMT_BGRA,         AV_PIX_FMT_BGRA,         VideoInfo::CS_BGR32,      0, 0, 0 },
            { AV_PIX_FMT_RGB48LE,      AV_PIX_FMT_BGR48LE,      VideoInfo::CS_BGR48,      8, 0, 0 },
            { AV_PIX_FMT_RGB48BE,      AV_PIX_FMT_BGR48LE,      VideoInfo::CS_BGR48,      8, 0, 0 },
            { AV_PIX_FMT_BGR48LE,      AV_PIX_FMT_BGR48LE,      VideoInfo::CS_BGR48,      8, 0, 0 },
            { AV_PIX_FMT_BGR48BE,      AV_PIX_FMT_BGR48LE,      VideoInfo::CS_BGR48,      8, 0, 0 },
            { AV_PIX_FMT_XYZ12LE,      AV_PIX_FMT_XYZ12LE,      VideoInfo::CS_BGR48,      8, 0, 0 },
            { AV_PIX_FMT_RGBA64LE,     AV_PIX_FMT_BGRA64LE,     VideoInfo::CS_BGR64,      8, 0, 0 },
            { AV_PIX_FMT_RGBA64BE,     AV_PIX_FMT_BGRA64LE,     VideoInfo::CS_BGR64,      8, 0, 0 },
            { AV_PIX_FMT_BGRA64LE,     AV_PIX_FMT_BGRA64LE,     VideoInfo::CS_BGR64,      8, 0, 0 },
            { AV_PIX_FMT_BGRA64BE,     AV_PIX_FMT_BGRA64LE,     VideoInfo::CS_BGR64,      8, 0, 0 },
            { AV_PIX_FMT_GBRP,         AV_PIX_FMT_GBRP,         VideoInfo::CS_RGBP,       0, 0, 0 },
            { AV_PIX_FMT_GBRP9LE,      AV_PIX_FMT_GBRP10LE,     VideoInfo::CS_RGBP10,     2, 0, 0 },
            { AV_PIX_FMT_GBRP9BE,      AV_PIX_FMT_GBRP10LE,     VideoInfo::CS_RGBP10,     2, 0, 0 },
            { AV_PIX_FMT_GBRP10LE,     AV_PIX_FMT_GBRP10LE,     VideoInfo::CS_RGBP10,     2, 0, 0 },
            { AV_PIX_FMT_GBRP10BE,     AV_PIX_FMT_GBRP10LE,     VideoInfo::CS_RGBP10,     2, 0, 0 },
            { AV_PIX_FMT_GBRP12LE,     AV_PIX_FMT_GBRP12LE,     VideoInfo::CS_RGBP12,     4, 0, 0 },
            { AV_PIX_FMT_GBRP12BE,     AV_PIX_FMT_GBRP12LE,     VideoInfo::CS_RGBP12,     4, 0, 0 },
            { AV_PIX_FMT_GBRP14LE,     AV_PIX_FMT_GBRP14LE,     VideoInfo::CS_RGBP14,     6, 0, 0 },
            { AV_PIX_FMT_GBRP14BE,     AV_PIX_FMT_GBRP14LE,     VideoInfo::CS_RGBP14,     6, 0, 0 },
            { AV_PIX_FMT_GBRP16LE,     AV_PIX_FMT_GBRP16LE,     VideoInfo::CS_RGBP16,     8, 0, 0 },
            { AV_PIX_FMT_GBRP16BE,     AV_PIX_FMT_GBRP16LE,     VideoInfo::CS_RGBP16,     8, 0, 0 },
            { AV_PIX_FMT_GBRAP,        AV_PIX_FMT_GBRAP,        VideoInfo::CS_RGBAP,      0, 0, 0 },
            { AV_PIX_FMT_GBRAP10LE,    AV_PIX_FMT_GBRAP10LE,    VideoInfo::CS_RGBAP10,    2, 0, 0 },
            { AV_PIX_FMT_GBRAP10BE,    AV_PIX_FMT_GBRAP10LE,    VideoInfo::CS_RGBAP10,    2, 0, 0 },
            { AV_PIX_FMT_GBRAP12LE,    AV_PIX_FMT_GBRAP12LE,    VideoInfo::CS_RGBAP12,    4, 0, 0 },
            { AV_PIX_FMT_GBRAP12BE,    AV_PIX_FMT_GBRAP12LE,    VideoInfo::CS_RGBAP12,    4, 0, 0 },
            { AV_PIX_FMT_GBRAP16LE,    AV_PIX_FMT_GBRAP16LE,    VideoInfo::CS_RGBAP16,    8, 0, 0 },
            { AV_PIX_FMT_GBRAP16BE,    AV_PIX_FMT_GBRAP16LE,    VideoInfo::CS_RGBAP16,    8, 0, 0 },            
            { AV_PIX_FMT_NONE,         AV_PIX_FMT_NONE,         VideoInfo::CS_UNKNOWN,    0, 0, 0 }            
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
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUV410P:
        case AV_PIX_FMT_YUV411P:
            as_vohp->make_black_background = make_black_background_planar_yuv;
            as_vohp->make_frame            = make_frame_planar_yuv;
            return 0;
        case AV_PIX_FMT_YUV420P10LE:
        case AV_PIX_FMT_YUV420P12LE:
        case AV_PIX_FMT_YUV420P14LE:
        case AV_PIX_FMT_YUV420P16LE:
        case AV_PIX_FMT_YUV422P10LE:
        case AV_PIX_FMT_YUV422P12LE:
        case AV_PIX_FMT_YUV422P14LE:
        case AV_PIX_FMT_YUV422P16LE:
        case AV_PIX_FMT_YUV444P10LE:
        case AV_PIX_FMT_YUV444P12LE:
        case AV_PIX_FMT_YUV444P14LE:
        case AV_PIX_FMT_YUV444P16LE:
            as_vohp->make_black_background = make_black_background_planar_yuv_interleaved;
            as_vohp->make_frame            = make_frame_planar_yuv;
            return 0;
        case AV_PIX_FMT_YUYV422:
            as_vohp->make_black_background = make_black_background_packed_yuv422;
            as_vohp->make_frame            = make_frame_packed_yuv;
            return 0;
        case AV_PIX_FMT_YUVA420P:
        case AV_PIX_FMT_YUVA422P:
        case AV_PIX_FMT_YUVA444P:
            as_vohp->make_black_background = make_black_background_planar_yuva;
            as_vohp->make_frame            = make_frame_planar_yuva;
            return 0;
        case AV_PIX_FMT_YUVA420P10LE:
        case AV_PIX_FMT_YUVA420P16LE:
        case AV_PIX_FMT_YUVA422P10LE:
        case AV_PIX_FMT_YUVA422P12LE:
        case AV_PIX_FMT_YUVA422P16LE:
        case AV_PIX_FMT_YUVA444P10LE:
        case AV_PIX_FMT_YUVA444P12LE:
        case AV_PIX_FMT_YUVA444P16LE:
            as_vohp->make_black_background = make_black_background_planar_yuva_interleaved;
            as_vohp->make_frame            = make_frame_planar_yuva;
            return 0;
        case AV_PIX_FMT_GRAY8:
        case AV_PIX_FMT_GRAY10LE:
        case AV_PIX_FMT_GRAY12LE:
        case AV_PIX_FMT_GRAY14LE:
        case AV_PIX_FMT_GRAY16LE:
            as_vohp->make_black_background = make_black_background_packed_all_zero;
            as_vohp->make_frame            = make_frame_packed_yuv;
            return 0;
        case AV_PIX_FMT_BGR24:
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_BGR48LE:
        case AV_PIX_FMT_BGRA64LE:
        case AV_PIX_FMT_XYZ12LE:
            as_vohp->make_black_background = make_black_background_packed_all_zero;
            as_vohp->make_frame            = make_frame_packed_rgb;
            return 0;
        case AV_PIX_FMT_GBRP:
        case AV_PIX_FMT_GBRP10LE:
        case AV_PIX_FMT_GBRP12LE:
        case AV_PIX_FMT_GBRP14LE:
        case AV_PIX_FMT_GBRP16LE:
            as_vohp->make_black_background = make_black_background_planar_rgb;
            as_vohp->make_frame            = make_frame_planar_rgb;
            return 0;
        case AV_PIX_FMT_GBRAP:
        case AV_PIX_FMT_GBRAP10LE:
        case AV_PIX_FMT_GBRAP12LE:
        case AV_PIX_FMT_GBRAP16LE:
            as_vohp->make_black_background = make_black_background_planar_rgba;
            as_vohp->make_frame            = make_frame_planar_rgba;
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
        AV_PIX_FMT_YUV420P12LE,
        AV_PIX_FMT_YUV420P14LE,
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV422P9LE,
        AV_PIX_FMT_YUV422P10LE,
        AV_PIX_FMT_YUV422P12LE,
        AV_PIX_FMT_YUV422P14LE,
        AV_PIX_FMT_YUV422P16LE,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV444P9LE,
        AV_PIX_FMT_YUV444P10LE,
        AV_PIX_FMT_YUV444P12LE,
        AV_PIX_FMT_YUV444P14LE,
        AV_PIX_FMT_YUV444P16LE,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_BGRA,
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

void avs_set_frame_properties
(
    AVFrame* av_frame,
    AVStream* stream,
    int64_t duration_num,
    int64_t duration_den,
    bool rgb,
    PVideoFrame& avs_frame,
    int top,
    int bottom,
    IScriptEnvironment* env
    
)
{
    AVSMap* props = env->getFramePropsRW(avs_frame);
    /* Sample aspect ratio */
    env->propSetInt(props, "_SARNum", av_frame->sample_aspect_ratio.num, 0);
    env->propSetInt(props, "_SARDen", av_frame->sample_aspect_ratio.den, 0);
    /* Sample duration */
    env->propSetInt(props, "_DurationNum", duration_num, 0);
    env->propSetInt(props, "_DurationDen", duration_den, 0);
    /* Color format
     * The decoded color format may not match with the output. Set proper properties when
     * no YUV->RGB conversion is there. */
    if (!rgb)
    {
        if (av_frame->color_range != AVCOL_RANGE_UNSPECIFIED)
            env->propSetInt(props, "_ColorRange", av_frame->color_range == AVCOL_RANGE_MPEG, 0);
        env->propSetInt(props, "_Primaries", av_frame->color_primaries, 0);
        env->propSetInt(props, "_Transfer", av_frame->color_trc, 0);
        env->propSetInt(props, "_Matrix", av_frame->colorspace, 0);
        if (av_frame->chroma_location > 0)
            env->propSetInt(props, "_ChromaLocation", av_frame->chroma_location - 1, 0);
    }
    /* Picture type */
    char pict_type = av_get_picture_type_char(av_frame->pict_type);
    env->propSetData(props, "_PictType", &pict_type, 1, 0);
    /* BFF or TFF */
    int field_based = 0;
    if ( av_frame->interlaced_frame )
        field_based = av_frame->top_field_first ? 2 : 1;
    env->propSetInt(props, "_FieldBased", field_based, 0);
    if ( top > -1 )
    {
        env->propSetInt(props, "_EncodedFrameTop", top, 0);
        env->propSetInt(props, "_EncodedFrameBottom", bottom, 0);
    }
    /* Mastering display color volume */
    int frame_has_primaries = 0, frame_has_luminance = 0;
    const AVFrameSideData* mastering_display_side_data = av_frame_get_side_data(av_frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (mastering_display_side_data)
    {
        const AVMasteringDisplayMetadata* mastering_display = (const AVMasteringDisplayMetadata*)mastering_display_side_data->data;
        if ((frame_has_primaries = mastering_display->has_primaries))
        {
            double display_primaries_x[3], display_primaries_y[3];
            for (int i = 0; i < 3; ++i)
            {
                display_primaries_x[i] = av_q2d(mastering_display->display_primaries[i][0]);
                display_primaries_y[i] = av_q2d(mastering_display->display_primaries[i][1]);
            }
            env->propSetFloatArray(props, "MasteringDisplayPrimariesX", display_primaries_x, 3);
            env->propSetFloatArray(props, "MasteringDisplayPrimariesY", display_primaries_y, 3);
            env->propSetFloat(props, "MasteringDisplayWhitePointX", av_q2d(mastering_display->white_point[0]), 0);
            env->propSetFloat(props, "MasteringDisplayWhitePointY", av_q2d(mastering_display->white_point[1]), 0);
        }
        if ((frame_has_luminance = mastering_display->has_luminance))
        {
            env->propSetFloat(props, "MasteringDisplayMinLuminance", av_q2d(mastering_display->min_luminance), 0);
            env->propSetFloat(props, "MasteringDisplayMaxLuminance", av_q2d(mastering_display->max_luminance), 0);
        }
    }
    if (stream && (!frame_has_primaries || !frame_has_luminance))
    {
        for (int i = 0; i < stream->nb_side_data; ++i)
        {
            if (stream->side_data[i].type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA)
            {
                const AVMasteringDisplayMetadata* mastering_display = (const AVMasteringDisplayMetadata*)stream->side_data[i].data;
                if (mastering_display->has_primaries && !frame_has_primaries)
                {
                    double display_primaries_x[3], display_primaries_y[3];
                    for (int i = 0; i < 3; ++i)
                    {
                        display_primaries_x[i] = av_q2d(mastering_display->display_primaries[i][0]);
                        display_primaries_y[i] = av_q2d(mastering_display->display_primaries[i][1]);
                    }
                    env->propSetFloatArray(props, "MasteringDisplayPrimariesX", display_primaries_x, 3);
                    env->propSetFloatArray(props, "MasteringDisplayPrimariesY", display_primaries_y, 3);
                    env->propSetFloat(props, "MasteringDisplayWhitePointX", av_q2d(mastering_display->white_point[0]), 0);
                    env->propSetFloat(props, "MasteringDisplayWhitePointY", av_q2d(mastering_display->white_point[1]), 0);
                }
                if (mastering_display->has_luminance && !frame_has_luminance)
                {
                    env->propSetFloat(props, "MasteringDisplayMinLuminance", av_q2d(mastering_display->min_luminance), 0);
                    env->propSetFloat(props, "MasteringDisplayMaxLuminance", av_q2d(mastering_display->max_luminance), 0);
                }
                break;
            }
        }
    }
    int frame_has_light_level = 0;
    const AVFrameSideData* content_light_side_data = av_frame_get_side_data(av_frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (content_light_side_data)
    {
        const AVContentLightMetadata* content_light = (const AVContentLightMetadata*)content_light_side_data->data;
        if ((frame_has_light_level = content_light->MaxCLL || content_light->MaxFALL))
        {
            env->propSetInt(props, "ContentLightLevelMax", content_light->MaxCLL, 0);
            env->propSetInt(props, "ContentLightLevelAverage", content_light->MaxFALL, 0);
        }
    }
    if (stream && !frame_has_light_level)
    {
        for (int i = 0; i < stream->nb_side_data; ++i)
        {
            if (stream->side_data[i].type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL)
            {
                const AVContentLightMetadata* content_light = (const AVContentLightMetadata*)stream->side_data[i].data;
                if (content_light->MaxCLL || content_light->MaxFALL)
                {
                    env->propSetInt(props, "ContentLightLevelMax", content_light->MaxCLL, 0);
                    env->propSetInt(props, "ContentLightLevelAverage", content_light->MaxFALL, 0);
                }
                break;
            }
        }
    }
    const AVFrameSideData* rpu_side_data = av_frame_get_side_data(av_frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
    if (rpu_side_data && rpu_side_data->size > 0)
        env->propSetData(props, "DolbyVisionRPU", reinterpret_cast<const char*>(rpu_side_data->data), rpu_side_data->size, 0);
}
