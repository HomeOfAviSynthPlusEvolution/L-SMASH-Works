/*****************************************************************************
 * video_output.c
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

#include "lwinput.h"

#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "video_output.h"

#if (LIBAVUTIL_VERSION_MICRO >= 100) && (LIBSWSCALE_VERSION_MICRO >= 100)
#define FFMPEG_HIGH_DEPTH_SUPPORT 1
#else
#define FFMPEG_HIGH_DEPTH_SUPPORT 0
#endif

static output_colorspace_index determine_colorspace_conversion
(
    enum AVPixelFormat  input_pixel_format,
    enum AVPixelFormat *output_pixel_format
)
{
    DEBUG_VIDEO_MESSAGE_BOX_DESKTOP( MB_OK, "input_pixel_format = %s", (av_pix_fmt_desc_get( input_pixel_format ))->name );
    avoid_yuv_scale_conversion( &input_pixel_format );
    switch( input_pixel_format )
    {
        case AV_PIX_FMT_YUV444P :
        case AV_PIX_FMT_YUV440P :
        case AV_PIX_FMT_YUV420P9LE :
        case AV_PIX_FMT_YUV420P9BE :
        case AV_PIX_FMT_YUV422P9LE :
        case AV_PIX_FMT_YUV422P9BE :
        case AV_PIX_FMT_YUV444P9LE :
        case AV_PIX_FMT_YUV444P9BE :
        case AV_PIX_FMT_YUV420P10LE :
        case AV_PIX_FMT_YUV420P10BE :
        case AV_PIX_FMT_YUV422P10LE :
        case AV_PIX_FMT_YUV422P10BE :
        case AV_PIX_FMT_YUV444P10LE :
        case AV_PIX_FMT_YUV444P10BE :
        case AV_PIX_FMT_YUV420P16LE :
        case AV_PIX_FMT_YUV420P16BE :
        case AV_PIX_FMT_YUV422P16LE :
        case AV_PIX_FMT_YUV422P16BE :
        case AV_PIX_FMT_YUV444P16LE :
        case AV_PIX_FMT_YUV444P16BE :
        case AV_PIX_FMT_GRAY12LE :
        case AV_PIX_FMT_GRAY12BE :
        case AV_PIX_FMT_GRAY16LE :
        case AV_PIX_FMT_GRAY16BE :
        case AV_PIX_FMT_RGB48LE :
        case AV_PIX_FMT_RGB48BE :
        case AV_PIX_FMT_BGR48LE :
        case AV_PIX_FMT_BGR48BE :
        case AV_PIX_FMT_GBRP9LE :
        case AV_PIX_FMT_GBRP9BE :
        case AV_PIX_FMT_GBRP10LE :
        case AV_PIX_FMT_GBRP10BE :
        case AV_PIX_FMT_GBRP16LE :
        case AV_PIX_FMT_GBRP16BE :
#ifdef FFMPEG_HIGH_DEPTH_SUPPORT
        case AV_PIX_FMT_YUV420P12LE :
        case AV_PIX_FMT_YUV420P12BE :
        case AV_PIX_FMT_YUV422P12LE :
        case AV_PIX_FMT_YUV422P12BE :
        case AV_PIX_FMT_YUV444P12LE :
        case AV_PIX_FMT_YUV444P12BE :
        case AV_PIX_FMT_YUV420P14LE :
        case AV_PIX_FMT_YUV420P14BE :
        case AV_PIX_FMT_YUV422P14LE :
        case AV_PIX_FMT_YUV422P14BE :
        case AV_PIX_FMT_YUV444P14LE :
        case AV_PIX_FMT_YUV444P14BE :
        case AV_PIX_FMT_GRAY10LE :
        case AV_PIX_FMT_GRAY10BE :
#endif
            *output_pixel_format = AV_PIX_FMT_YUV444P16LE;  /* planar YUV 4:4:4, 48bpp little-endian -> YC48 */
            return OUTPUT_YC48;
        case AV_PIX_FMT_YUVA420P :
        case AV_PIX_FMT_YUVA422P :
        case AV_PIX_FMT_YUVA444P :
        case AV_PIX_FMT_YUVA420P9LE :
        case AV_PIX_FMT_YUVA420P9BE :
        case AV_PIX_FMT_YUVA422P9LE :
        case AV_PIX_FMT_YUVA422P9BE :
        case AV_PIX_FMT_YUVA444P9LE :
        case AV_PIX_FMT_YUVA444P9BE :
        case AV_PIX_FMT_YUVA420P10LE :
        case AV_PIX_FMT_YUVA420P10BE :
        case AV_PIX_FMT_YUVA422P10LE :
        case AV_PIX_FMT_YUVA422P10BE :
        case AV_PIX_FMT_YUVA444P10LE :
        case AV_PIX_FMT_YUVA444P10BE :
        case AV_PIX_FMT_YUVA420P16LE :
        case AV_PIX_FMT_YUVA420P16BE :
        case AV_PIX_FMT_YUVA422P16LE :
        case AV_PIX_FMT_YUVA422P16BE :
        case AV_PIX_FMT_YUVA444P16LE :
        case AV_PIX_FMT_YUVA444P16BE :
        case AV_PIX_FMT_YA8 :
        case AV_PIX_FMT_YA16LE :
        case AV_PIX_FMT_YA16BE :
        case AV_PIX_FMT_ARGB :
        case AV_PIX_FMT_RGBA :
        case AV_PIX_FMT_ABGR :
        case AV_PIX_FMT_BGRA :
        case AV_PIX_FMT_RGBA64LE :
        case AV_PIX_FMT_RGBA64BE :
        case AV_PIX_FMT_BGRA64LE :
        case AV_PIX_FMT_BGRA64BE :
        case AV_PIX_FMT_GBRAP :
        case AV_PIX_FMT_GBRAP10LE :
        case AV_PIX_FMT_GBRAP10BE :
        case AV_PIX_FMT_GBRAP12LE :
        case AV_PIX_FMT_GBRAP12BE :
        case AV_PIX_FMT_GBRAP16LE :
        case AV_PIX_FMT_GBRAP16BE :
#ifdef FFMPEG_HIGH_DEPTH_SUPPORT
        case AV_PIX_FMT_YUVA422P12LE :
        case AV_PIX_FMT_YUVA422P12BE :
        case AV_PIX_FMT_YUVA444P12LE :
        case AV_PIX_FMT_YUVA444P12BE :
#endif
            *output_pixel_format = AV_PIX_FMT_BGRA;         /* packed BGRA 8:8:8:8, 32bpp, BGRABGRA... */
            return OUTPUT_RGBA;
        case AV_PIX_FMT_RGB24 :
        case AV_PIX_FMT_BGR24 :
        case AV_PIX_FMT_BGR8 :
        case AV_PIX_FMT_BGR4 :
        case AV_PIX_FMT_BGR4_BYTE :
        case AV_PIX_FMT_RGB8 :
        case AV_PIX_FMT_RGB4 :
        case AV_PIX_FMT_RGB4_BYTE :
        case AV_PIX_FMT_RGB565LE :
        case AV_PIX_FMT_RGB565BE :
        case AV_PIX_FMT_RGB555LE :
        case AV_PIX_FMT_RGB555BE :
        case AV_PIX_FMT_BGR565LE :
        case AV_PIX_FMT_BGR565BE :
        case AV_PIX_FMT_BGR555LE :
        case AV_PIX_FMT_BGR555BE :
        case AV_PIX_FMT_RGB444LE :
        case AV_PIX_FMT_RGB444BE :
        case AV_PIX_FMT_BGR444LE :
        case AV_PIX_FMT_BGR444BE :
        case AV_PIX_FMT_GBRP :
        case AV_PIX_FMT_PAL8 :
            *output_pixel_format = AV_PIX_FMT_BGR24;        /* packed RGB 8:8:8, 24bpp, BGRBGR... */
            return OUTPUT_RGB24;
        default :
            *output_pixel_format = AV_PIX_FMT_YUYV422;      /* packed YUV 4:2:2, 16bpp */
            return OUTPUT_YUY2;
    }
}

static void au_free_video_output_handler
(
    void *private_handler
)
{
    au_video_output_handler_t *au_vohp = (au_video_output_handler_t *)private_handler;
    if( !au_vohp )
        return;
    lw_free( au_vohp->back_ground );
    av_free( au_vohp->another_chroma );
    av_frame_free( &au_vohp->yuv444p16 );
    lw_free( au_vohp );
}

int au_setup_video_rendering
(
    lw_video_output_handler_t *vohp,
    video_option_t            *opt,
    BITMAPINFOHEADER          *format,
    int                        output_width,
    int                        output_height,
    enum AVPixelFormat         input_pixel_format
)
{
    /* Set up output format. */
    au_video_output_handler_t *au_vohp = (au_video_output_handler_t *)lw_malloc_zero( sizeof(au_video_output_handler_t) );
    if( !au_vohp )
    {
        DEBUG_VIDEO_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to allocate the AviUtl video output handler." );
        return -1;
    }
    vohp->private_handler      = au_vohp;
    vohp->free_private_handler = au_free_video_output_handler;
    enum AVPixelFormat      output_pixel_format;
    output_colorspace_index index;
    if( opt->colorspace == 0 )
        index = determine_colorspace_conversion( input_pixel_format, &output_pixel_format );
    else
    {
        output_pixel_format = AV_PIX_FMT_YUV444P16LE;
        index               = OUTPUT_LW48;
    }
    setup_video_rendering( vohp, 1 << opt->scaler,
                           output_width, output_height, output_pixel_format,
                           NULL, NULL );
    static const struct
    {
        func_convert_colorspace *convert_colorspace;
        int                      pixel_size;
        output_colorspace_tag    compression;
    } colorspace_table[5] =
        {
            { to_yuy2,            YUY2_SIZE,  OUTPUT_TAG_YUY2 },
            { to_rgb24,           RGB24_SIZE, OUTPUT_TAG_RGB  },
            { to_rgba,            RGBA_SIZE,  OUTPUT_TAG_RGBA },
            { to_yuv16le_to_yc48, YC48_SIZE,  OUTPUT_TAG_YC48 },
            { to_yuv16le_to_lw48, LW48_SIZE,  OUTPUT_TAG_LW48 }
        };
    au_vohp->convert_colorspace = colorspace_table[index].convert_colorspace;
    /* BITMAPINFOHEADER */
    format->biSize        = sizeof( BITMAPINFOHEADER );
    format->biWidth       = output_width;
    format->biHeight      = output_height;
    format->biBitCount    = colorspace_table[index].pixel_size << 3;
    format->biCompression = colorspace_table[index].compression;
    /* Set up a black frame of back ground. */
    au_vohp->output_linesize   = MAKE_AVIUTL_PITCH( output_width * format->biBitCount );
    au_vohp->output_frame_size = au_vohp->output_linesize * output_height;
    au_vohp->back_ground       = au_vohp->output_frame_size > 0 ? lw_malloc_zero( au_vohp->output_frame_size ) : NULL;
    if( !au_vohp->back_ground )
    {
        DEBUG_VIDEO_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to allocate the back ground frame buffer." );
        return -1;
    }
    if( format->biCompression == OUTPUT_TAG_YC48
     || format->biCompression == OUTPUT_TAG_LW48 )
    {
        AVFrame *yuv444p16 = av_frame_alloc();
        if( !yuv444p16 )
        {
            DEBUG_VIDEO_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to av_frame_alloc for YUV444P16 convertion." );
            return -1;
        }
        au_vohp->yuv444p16 = yuv444p16;
        if( av_image_alloc( yuv444p16->data, yuv444p16->linesize, output_width, output_height, AV_PIX_FMT_YUV444P16LE, 32 ) < 0 )
        {
            DEBUG_VIDEO_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to av_image_alloc for YUV444P16 convertion." );
            return -1;
        }
    }
    if( format->biCompression == OUTPUT_TAG_YUY2 )
    {
        uint8_t *pic = au_vohp->back_ground;
        for( int i = 0; i < output_height; i++ )
        {
            for( int j = 0; j < au_vohp->output_linesize; j += YUY2_SIZE )
            {
                pic[j    ] = 0;
                pic[j + 1] = 128;
            }
            pic += au_vohp->output_linesize;
        }
    }
    else if( format->biCompression == OUTPUT_TAG_LW48 )
    {
        const PIXEL_LW48 black_pix = { 4096, 32768, 32768 };
        uint8_t *pic = au_vohp->back_ground;
        for( int i = 0; i < output_height; i++ )
        {
            PIXEL_LW48 *pix = (PIXEL_LW48 *)pic;
            for( int j = 0; j < output_width; j++ )
                *(pix++) = black_pix;
            pic += au_vohp->output_linesize;
        }
    }
    return 0;
}

int convert_colorspace
(
    lw_video_output_handler_t *vohp,
    AVFrame                   *av_frame,
    uint8_t                   *buf
)
{
    /* Convert color space. We don't change the presentation resolution. */
    au_video_output_handler_t *au_vohp = (au_video_output_handler_t *)vohp->private_handler;
    if( vohp->scaler.frame_prop_change_flags & (LW_FRAME_PROP_CHANGE_FLAG_WIDTH | LW_FRAME_PROP_CHANGE_FLAG_HEIGHT) )
        memcpy( buf, au_vohp->back_ground, au_vohp->output_frame_size );
    if( au_vohp->convert_colorspace( vohp, av_frame, buf ) < 0 )
        return 0;
    return au_vohp->output_frame_size;
}
