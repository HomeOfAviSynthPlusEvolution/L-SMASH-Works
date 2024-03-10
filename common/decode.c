/*****************************************************************************
 * decode.c / decode.cpp
 *****************************************************************************
 * Copyright (C) 2012-2016 L-SMASH Works project
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

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <libavcodec/avcodec.h>
#include <libavutil/cpu.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "decode.h"
#include "qsv.h"

static const AVCodec *select_hw_decoder
(
    const char              *codec_name,
    const int                prefer_hw_decoder,
    const AVCodecParameters *codecpar
)
{
    char hw_decoder_name[32] = { 0 };
    const size_t codec_name_length = strlen( codec_name );
    const char *wrapper = prefer_hw_decoder == 1 ? "_cuvid" : "_qsv";
    memcpy( hw_decoder_name, codec_name, codec_name_length );
    memcpy( hw_decoder_name + codec_name_length, wrapper, strlen( wrapper ) );
    const AVCodec *hw_decoder = avcodec_find_decoder_by_name( hw_decoder_name );
    if( !hw_decoder )
        return NULL;
    AVCodecContext *ctx = avcodec_alloc_context3( hw_decoder );
    if( !ctx )
        return NULL;
    if( (codecpar && avcodec_parameters_to_context( ctx, codecpar ) < 0)
     || avcodec_open2( ctx, hw_decoder, NULL ) < 0
     || avcodec_send_packet( ctx, NULL ) < 0 )
    {
        avcodec_free_context( &ctx );
        return NULL;
    }
    avcodec_free_context( &ctx );
    return hw_decoder;
}

const AVCodec *find_decoder
(
    enum AVCodecID           codec_id,
    const AVCodecParameters *codecpar,
    const char             **preferred_decoder_names,
    const int                prefer_hw_decoder
)
{
    const AVCodec *codec = avcodec_find_decoder( codec_id );
    if( !codec )
        return NULL;
    if( preferred_decoder_names
     && *preferred_decoder_names
     && *preferred_decoder_names[0] )
        for( const char **decoder_name = preferred_decoder_names; *decoder_name != NULL; decoder_name++ )
        {
            const AVCodec *preferred_decoder = avcodec_find_decoder_by_name( *decoder_name );
            if( preferred_decoder
             && preferred_decoder->id == codec->id )
            {
                codec = preferred_decoder;
                break;
            }
        }
    else if( codec->type == AVMEDIA_TYPE_VIDEO
          && prefer_hw_decoder )
    {
        const char *codec_name;
        if (!strcmp(codec->name, "mpeg1video"))
            codec_name = "mpeg1";
        else if (!strcmp(codec->name, "mpeg2video"))
            codec_name = "mpeg2";
        else if (!strcmp(codec->name, "libdav1d")
         || !strcmp(codec->name, "libaom-av1"))
            codec_name = "av1";
        else
            codec_name = codec->name;
        AVCodec *preferred_decoder;
        if( prefer_hw_decoder == 3 )
        {
            preferred_decoder = select_hw_decoder( codec_name, 1, codecpar );
            if( !preferred_decoder )
                preferred_decoder = select_hw_decoder( codec_name, 2, codecpar );
        }
        else
            preferred_decoder = select_hw_decoder( codec_name, prefer_hw_decoder, codecpar );
        if( preferred_decoder )
            codec = preferred_decoder;
    }
    return codec;
}

int open_decoder
(
    AVCodecContext         **ctx,
    const AVCodecParameters *codecpar,
    const AVCodec           *codec,
    const int                thread_count,
    const double             drc,
    const char              *ff_options
)
{
    AVCodecContext *c = avcodec_alloc_context3( codec );
    if( !c )
        return -1;
    int ret;
    if( (ret = avcodec_parameters_to_context( c, codecpar )) < 0 )
        goto fail;
    c->thread_count = thread_count;
    c->codec_id     = AV_CODEC_ID_NONE; /* AVCodecContext.codec_id is supposed to be set properly in avcodec_open2().
                                         * This avoids avcodec_open2() failure by the difference of enum AVCodecID.
                                         * For instance, when stream is encoded as AC-3,
                                         * AVCodecContext.codec_id might have been set to AV_CODEC_ID_EAC3
                                         * while AVCodec.id is set to AV_CODEC_ID_AC3. */
    if (!strcmp(codec->name, "libdav1d")
        && (ret = av_opt_set_int(c->priv_data, "max_frame_delay", 2, 0)) < 0)
        goto fail;
    else if( !strcmp( codec->name, "vp9" )
          && thread_count != 1
          && av_cpu_count() > 1 )
        c->thread_count = 2;
    if( codec->id == AV_CODEC_ID_H264
     && c->has_b_frames < 8 )
        c->has_b_frames = 8;
    if( codec->wrapper_name
     && !strcmp( codec->wrapper_name, "cuvid" ) )
        c->has_b_frames = 16; /* the maximum decoder latency for AVC and HEVC frame */
    AVDictionary* ff_d = NULL;
    if (codec->id == AV_CODEC_ID_AC3 && drc > -1)
    {
        char buf[10];
        if ((ret = snprintf(buf, sizeof(buf), "%.10g", drc)) > 0)
        {
            if ((ret = av_dict_set(&ff_d, "drc_scale", buf, 0)) < 0)
                goto fail;
        }
        else
            goto fail;
    }
    if ( ff_options && ( ret = av_dict_parse_string( &ff_d, ff_options, "=", " ", 0)) < 0)
    {
        av_dict_free(&ff_d);
        goto fail;
    }
    ret = avcodec_open2( c, codec, &ff_d );
    av_dict_free( &ff_d );
    if ( ret < 0 )
        goto fail;
    if( is_qsv_decoder( c->codec ) )
        if( (ret = do_qsv_decoder_workaround( c )) < 0 )
            goto fail;
    *ctx = c;
    return ret;
fail:
    avcodec_free_context( &c );
    return ret;
}

int find_and_open_decoder
(
    AVCodecContext         **ctx,
    const AVCodecParameters *codecpar,
    const char             **preferred_decoder_names,
    const int                prefer_hw_decoder,
    const int                thread_count,
    const double             drc,
    const char              *ff_options
)
{
    const AVCodec *codec = find_decoder( codecpar->codec_id, codecpar, preferred_decoder_names, prefer_hw_decoder );
    if( !codec )
        return -1;
    return open_decoder( ctx, codecpar, codec, thread_count, drc, ff_options );
}

/* An incomplete simulator of the old libavcodec video decoder API
 * Unlike the old, this function does not return consumed bytes of input packet on success. */
int decode_video_packet
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int            *got_frame,
    AVPacket       *pkt
)
{
    int ret;
    *got_frame = 0;
    if( pkt )
    {
        ret = avcodec_send_packet( ctx, pkt );
        if( ret < 0
         && ret != AVERROR_EOF          /* No more packets can be sent if true. */
         && ret != AVERROR( EAGAIN ) )  /* Must receive output frames before sending new packets if true. */
            return ret;
    }
    ret = avcodec_receive_frame( ctx, av_frame );
    if( ret < 0
     && ret != AVERROR( EAGAIN )    /* Must send new packets before receiving frames if true. */
     && ret != AVERROR_EOF )        /* No more frames can be drained if true. */
        return ret;
    if( ret >= 0 )
        *got_frame = 1;
    return 0;
}

/* An incomplete simulator of the old libavcodec audio decoder API
 * Unlike the old, this function always returns size of input packet on success since avcodec_send_packet() fully consumes it. */
int decode_audio_packet
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int            *got_frame,
    AVPacket       *pkt
)
{
    int ret;
    int consumed_bytes = 0;
    *got_frame = 0;
    if( pkt )
    {
        ret = avcodec_send_packet( ctx, pkt );
        if( ret < 0
         && ret != AVERROR_EOF          /* No more packets can be sent if true. */
         && ret != AVERROR( EAGAIN ) )  /* Must receive output frames before sending new packets if true. */
            return ret;
        if( ret == 0 )
            consumed_bytes = pkt->size;
    }
    ret = avcodec_receive_frame( ctx, av_frame );
    if( ret < 0
     && ret != AVERROR( EAGAIN )    /* Must send new packets before receiving frames if true. */
     && ret != AVERROR_EOF )        /* No more frames can be drained if true. */
        return ret;
    if( ret >= 0 )
        *got_frame = 1;
    return consumed_bytes;
}
