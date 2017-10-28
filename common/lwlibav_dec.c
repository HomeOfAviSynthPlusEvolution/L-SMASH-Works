/*****************************************************************************
 * lwlibav_dec.c / lwindex.cpp
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
#include <libavformat/avformat.h>   /* Demuxer */
#include <libavcodec/avcodec.h>     /* Decoder */
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "lwlibav_dec.h"
#include "qsv.h"
#include "decode.h"

/* Close and open the new decoder to flush buffers in the decoder even if the decoder implements avcodec_flush_buffers().
 * It seems this brings about more stable composition when seeking.
 * Note that this function could reallocate AVCodecContext. */
void lwlibav_flush_buffers
(
    lwlibav_decode_handler_t *dhp
)
{
    const AVCodecParameters *codecpar     = dhp->format->streams[ dhp->stream_index ]->codecpar;
    const AVCodec           *codec        = dhp->ctx->codec;
    void                    *app_specific = dhp->ctx->opaque;
    AVCodecContext *ctx = NULL;
    if( open_decoder( &ctx, codecpar, codec, dhp->ctx->thread_count, dhp->ctx->refcounted_frames ) < 0 )
    {
        avcodec_flush_buffers( dhp->ctx );
        dhp->error = 1;
        lw_log_show( &dhp->lh, LW_LOG_FATAL,
                     "Failed to flush buffers by a reliable way.\n"
                     "It is recommended you reopen the file." );
    }
    else
    {
        dhp->ctx->opaque = NULL;
        avcodec_free_context( &dhp->ctx );
        dhp->ctx = ctx;
        dhp->ctx->opaque = app_specific;
    }
    dhp->exh.delay_count = 0;
}

void lwlibav_update_configuration
(
    lwlibav_decode_handler_t *dhp,
    uint32_t                  frame_number,
    int                       extradata_index,
    int64_t                   rap_pos
)
{
    lwlibav_extradata_handler_t *exhp = &dhp->exh;
    if( exhp->entry_count == 0 || extradata_index < 0 )
    {
        /* No need to update the extradata. */
        exhp->current_index = extradata_index;
        lwlibav_flush_buffers( dhp );
        return;
    }
    char error_string[96] = { 0 };
    AVCodecParameters *codecpar          = dhp->format->streams[ dhp->stream_index ]->codecpar;
    void              *app_specific      = dhp->ctx->opaque;
    const int          thread_count      = dhp->ctx->thread_count;
    const int          refcounted_frames = dhp->ctx->refcounted_frames;
    /* Close the decoder here. */
    dhp->ctx->opaque = NULL;
    avcodec_free_context( &dhp->ctx );
    /* Find an appropriate decoder. */
    const lwlibav_extradata_t *entry = &exhp->entries[extradata_index];
    const AVCodec *codec = find_decoder( entry->codec_id, dhp->preferred_decoder_names );
    if( !codec )
    {
        strcpy( error_string, "Failed to find the decoder.\n" );
        goto fail;
    }
    /* Set up decoder basic settings. */
    if( codecpar->codec_type == AVMEDIA_TYPE_VIDEO )
        set_video_basic_settings( dhp, codec, frame_number );
    else
        set_audio_basic_settings( dhp, codec, frame_number );
    /* Update extradata. */
    av_freep( &codecpar->extradata );
    codecpar->extradata_size = 0;
    if( entry->extradata_size > 0 )
    {
        codecpar->extradata = (uint8_t *)av_malloc( entry->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE );
        if( !codecpar->extradata )
        {
            strcpy( error_string, "Failed to allocate extradata.\n" );
            goto fail;
        }
        codecpar->extradata_size = entry->extradata_size;
        memcpy( codecpar->extradata, entry->extradata, codecpar->extradata_size );
        memset( codecpar->extradata + codecpar->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE );
    }
    /* This is needed by some CODECs such as UtVideo and raw video. */
    codecpar->codec_tag = entry->codec_tag;
    /* Open an appropriate decoder.
     * Here, we force single threaded decoding since some decoder doesn't do its proper initialization with multi-threaded decoding. */
    if( open_decoder( &dhp->ctx, codecpar, codec, 1, refcounted_frames ) < 0 )
    {
        strcpy( error_string, "Failed to open decoder.\n" );
        goto fail;
    }
    exhp->current_index = extradata_index;
    exhp->delay_count   = 0;
    /* Set up decoder basic settings by actual decoding. */
    if( dhp->ctx->codec_type == AVMEDIA_TYPE_VIDEO
      ? try_decode_video_frame( dhp, frame_number, rap_pos, error_string ) < 0
      : try_decode_audio_frame( dhp, frame_number, error_string ) < 0 )
        goto fail;
    /* Reopen/flush with the requested number of threads. */
    dhp->ctx->thread_count = thread_count;
    int width  = dhp->ctx->width;
    int height = dhp->ctx->height;
    lwlibav_flush_buffers( dhp );   /* Note that dhp->ctx could change here. */
    dhp->ctx->get_buffer2 = exhp->get_buffer ? exhp->get_buffer : avcodec_default_get_buffer2;
    dhp->ctx->opaque      = app_specific;
    /* avcodec_open2() may have changed resolution unexpectedly. */
    dhp->ctx->width       = width;
    dhp->ctx->height      = height;
    return;
fail:
    exhp->delay_count = 0;
    dhp->error = 1;
    lw_log_show( &dhp->lh, LW_LOG_FATAL,
                 "%sIt is recommended you reopen the file.", error_string );
}

int lwlibav_get_av_frame
(
    AVFormatContext *format_ctx,
    int              stream_index,
    uint32_t         frame_number,
    AVPacket        *pkt
)
{
    av_packet_unref( pkt );
    av_init_packet( pkt );
    /* Get a packet as the requested frame physically. */
    while( read_av_frame( format_ctx, pkt ) >= 0 )
    {
        if( pkt->stream_index != stream_index )
        {
            av_packet_unref( pkt );
            continue;
        }
        /* libavformat seek results might be inaccurate.
         * So, you might get a returnable packet exceeding frame_count and, if present, return it. */
        return 0;
    }
    /* Return a null packet. */
    pkt->data = NULL;
    pkt->size = 0;
    return 1;
}
