/*****************************************************************************
 * qsv.c / qsv.cpp
 *****************************************************************************
 * Copyright (C) 2015 L-SMASH Works project
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

#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/mem.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "decode.h"

int is_qsv_decoder
(
    const AVCodec *codec
)
{
    if( codec && codec->pix_fmts )
        for( const enum AVPixelFormat *pix_fmt = codec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++ )
            if( *pix_fmt == AV_PIX_FMT_QSV )
                return 1;
    return 0;
}

/* Workarounds for Intel QuickSync Video Decoder through libavcodec */
int do_qsv_decoder_workaround
(
    AVCodecContext *ctx
)
{
    ctx->has_b_frames = 16; /* the maximum decoder latency for AVC and HEVC frame */
    if( ctx->codec_id != AV_CODEC_ID_H264 )
        return 0;
    /* Feed an initializer packet to the decoder since libavcodec does not append parameter sets to access units
     * containing no IDR NAL units. Here we append parameter sets before the fake IDR NAL unit. Without this,
     * MFXVideoDECODE_DecodeHeader will return MFX_ERR_MORE_DATA. */
    static const uint8_t fake_idr[] = { 0x00, 0x00, 0x00, 0x01, 0x65 }; /* valid for both start-code and size-field prefixes */
    int ret = -1;
    AVPacket initializer = { 0 };
    if( ctx->extradata[0] == 1 )
    {
        /* Set up the bitstream filter. */
        AVBSFContext            *bsf_ctx = NULL;
        const AVBitStreamFilter *bsf     = av_bsf_get_by_name( "h264_mp4toannexb" );
        if( !bsf || (ret = av_bsf_alloc( bsf, &bsf_ctx )) < 0 )
            goto bsf_fail;
        AVCodecParameters *codecpar = bsf_ctx->par_in;
        if( (ret = avcodec_parameters_from_context( codecpar, ctx )) < 0 )
            goto bsf_fail;
        codecpar->extradata[4] |= 0x03; /* Force 4 byte length size fields. */
        if( (ret = av_bsf_init( bsf_ctx )) < 0 )
            goto bsf_fail;
        /* Convert into AnnexB Byte Stream Format. */
        uint8_t data[sizeof(fake_idr)];
        memcpy( data, fake_idr, sizeof(fake_idr) );
        initializer.data = data;
        initializer.size = sizeof(fake_idr);
        AVPacket *in_pkt = &initializer;
        while( 1 )
        {
            if( (ret = av_bsf_send_packet( bsf_ctx, in_pkt )) < 0 )
                goto bsf_fail;
            ret = av_bsf_receive_packet( bsf_ctx, &initializer );
            if( ret == AVERROR( EAGAIN ) || (in_pkt && ret == AVERROR_EOF) )
                in_pkt = NULL;  /* Send a null packet. */
            else if( ret < 0 )
                goto bsf_fail;
            else if( ret == 0 )
                break;
        }
bsf_fail:
        /* Tear down the bistream filter. */
        av_bsf_free( &bsf_ctx );
        if( ret < 0 )
            goto fail;
    }
    else
    {
        if( (ret = av_new_packet( &initializer, ctx->extradata_size + sizeof(fake_idr) )) < 0 )
            return ret;
        memcpy( initializer.data, ctx->extradata, ctx->extradata_size );
        memcpy( initializer.data + ctx->extradata_size, fake_idr, sizeof(fake_idr) );
    }
    /* Initialize the decoder. */
    AVFrame *picture = av_frame_alloc();
    if( picture )
    {
        int got_picture;    /* unused */
        ret = decode_video_packet( ctx, picture, &got_picture, &initializer );
        av_frame_free( &picture );
    }
fail:
    av_packet_unref( &initializer );
    return ret;
}
