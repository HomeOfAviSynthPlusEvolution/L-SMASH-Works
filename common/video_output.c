/*****************************************************************************
 * video_output.c / video_output.cpp
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

/* This file is available under an ISC license. */

#include "cpp_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "video_output.h"

/* If YUV is treated as full range, return 1.
 * Otherwise, return 0. */
int avoid_yuv_scale_conversion( enum AVPixelFormat *pixel_format )
{
    static const struct
    {
        enum AVPixelFormat full;
        enum AVPixelFormat limited;
    } range_hack_table[]
        = {
            { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P },
            { AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV422P },
            { AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV444P },
            { AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUV440P },
            { AV_PIX_FMT_NONE,     AV_PIX_FMT_NONE    }
          };
    for( int i = 0; range_hack_table[i].full != AV_PIX_FMT_NONE; i++ )
        if( *pixel_format == range_hack_table[i].full )
        {
            *pixel_format = range_hack_table[i].limited;
            return 1;
        }
    return 0;
}

static void initialize_scaler_handler
(
    lw_video_scaler_handler_t *vshp,
    int                        flags,
    enum AVPixelFormat         output_pixel_format
)
{
    if( flags != SWS_FAST_BILINEAR )
        flags |= SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND | SWS_BITEXACT;
    vshp->scaler_flags        = flags;
    vshp->input_width         = 0;
    vshp->input_height        = 0;
    vshp->input_pixel_format  = AV_PIX_FMT_NONE;
    vshp->output_pixel_format = output_pixel_format;
    vshp->input_colorspace    = AVCOL_SPC_UNSPECIFIED;
    vshp->input_yuv_range     = AVCOL_RANGE_UNSPECIFIED;
}

void setup_video_rendering
(
    lw_video_output_handler_t *vohp,
    int                        scaler_flags,
    int                        width,
    int                        height,
    enum AVPixelFormat         output_pixel_format,
    struct AVCodecContext     *ctx,
    int (*dr_get_buffer)( struct AVCodecContext *, AVFrame *, int )
)
{
    lw_video_scaler_handler_t *vshp = &vohp->scaler;
    initialize_scaler_handler( vshp, scaler_flags, output_pixel_format );
    /* Set up direct rendering if available. */
    if( ctx && dr_get_buffer )
    {
        /* Align output width and height for direct rendering. */
        int linesize_align[AV_NUM_DATA_POINTERS];
        enum AVPixelFormat input_pixel_format = ctx->pix_fmt;
        ctx->pix_fmt = output_pixel_format;
        avcodec_align_dimensions2( ctx, &width, &height, linesize_align );
        ctx->pix_fmt = input_pixel_format;
        /* Set up custom get_buffer() for direct rendering if available. */
        ctx->get_buffer2 = dr_get_buffer;
        ctx->opaque      = vohp;
    }
    vohp->output_width  = width;
    vohp->output_height = height;
}

static struct SwsContext *update_scaler_configuration
(
    struct SwsContext *sws_ctx,
    int                flags,
    int                width,
    int                height,
    enum AVPixelFormat input_pixel_format,
    enum AVPixelFormat output_pixel_format,
    enum AVColorSpace  colorspace,
    int                yuv_range
)
{
    if( sws_ctx )
        sws_freeContext( sws_ctx );
    sws_ctx = sws_alloc_context();
    if( !sws_ctx )
        return NULL;
    av_opt_set_int( sws_ctx, "sws_flags",  flags,               0 );
    av_opt_set_int( sws_ctx, "srcw",       width,               0 );
    av_opt_set_int( sws_ctx, "srch",       height,              0 );
    av_opt_set_int( sws_ctx, "dstw",       width,               0 );
    av_opt_set_int( sws_ctx, "dsth",       height,              0 );
    av_opt_set_int( sws_ctx, "src_format", input_pixel_format,  0 );
    av_opt_set_int( sws_ctx, "dst_format", output_pixel_format, 0 );
    const int *yuv2rgb_coeffs = sws_getCoefficients( colorspace );
    sws_setColorspaceDetails( sws_ctx,
                              yuv2rgb_coeffs, yuv_range,
                              yuv2rgb_coeffs, yuv_range,
                              0, 1 << 16, 1 << 16 );
    if( sws_init_context( sws_ctx, NULL, NULL ) < 0 )
    {
        sws_freeContext( sws_ctx );
        return NULL;
    }
    return sws_ctx;
}

int update_scaler_configuration_if_needed
(
    lw_video_scaler_handler_t *vshp,
    lw_log_handler_t          *lhp,
    const AVFrame             *av_frame
)
{
    enum AVPixelFormat *input_pixel_format = (enum AVPixelFormat *)&av_frame->format;
    int yuv_range = avoid_yuv_scale_conversion( input_pixel_format );
    if( av_frame->color_range == AVCOL_RANGE_MPEG
     || av_frame->color_range == AVCOL_RANGE_JPEG )
        yuv_range = (av_frame->color_range == AVCOL_RANGE_JPEG);
    vshp->frame_prop_change_flags
        = (vshp->input_width        != av_frame->width      ? LW_FRAME_PROP_CHANGE_FLAG_WIDTH        : 0)
        | (vshp->input_height       != av_frame->height     ? LW_FRAME_PROP_CHANGE_FLAG_HEIGHT       : 0)
        | (vshp->input_pixel_format != *input_pixel_format  ? LW_FRAME_PROP_CHANGE_FLAG_PIXEL_FORMAT : 0)
        | (vshp->input_colorspace   != av_frame->colorspace ? LW_FRAME_PROP_CHANGE_FLAG_COLORSPACE   : 0)
        | (vshp->input_yuv_range    != yuv_range            ? LW_FRAME_PROP_CHANGE_FLAG_YUV_RANGE    : 0);
    if( !vshp->sws_ctx || vshp->frame_prop_change_flags )
    {
        /* Update scaler. */
        vshp->sws_ctx = update_scaler_configuration( vshp->sws_ctx, vshp->scaler_flags,
                                                     av_frame->width, av_frame->height,
                                                     *input_pixel_format, vshp->output_pixel_format,
                                                     av_frame->colorspace, yuv_range );
        if( !vshp->sws_ctx )
        {
            lw_log_show( lhp, LW_LOG_WARNING, "Failed to update video scaler configuration." );
            return -1;
        }
        vshp->input_width        = av_frame->width;
        vshp->input_height       = av_frame->height;
        vshp->input_pixel_format = *input_pixel_format;
        vshp->input_colorspace   = av_frame->colorspace;
        vshp->input_yuv_range    = yuv_range;
        return 1;
    }
    return 0;
}

void lw_cleanup_video_output_handler
(
    lw_video_output_handler_t *vohp
)
{
    if( vohp->free_private_handler )
        vohp->free_private_handler( vohp->private_handler );
    vohp->private_handler = NULL;
    lw_freep( &vohp->frame_order_list );
    for( int i = 0; i < REPEAT_CONTROL_CACHE_NUM; i++ )
        av_frame_free( &vohp->frame_cache_buffers[i] );
    if( vohp->scaler.sws_ctx )
    {
        sws_freeContext( vohp->scaler.sws_ctx );
        vohp->scaler.sws_ctx = NULL;
    }
}

int transfer_frame_data(AVFrame* dst, AVFrame* src)
{
    if (src->hw_frames_ctx)
    {
        int ret = av_hwframe_transfer_data(dst, src, 0);
        av_frame_unref(src);
        return ret;
    }
    else
    {
        av_frame_move_ref(dst, src);
        return 0;
    }
}
