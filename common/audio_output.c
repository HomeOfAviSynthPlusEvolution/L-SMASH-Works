/*****************************************************************************
 * audio_output.c / audio_output.cpp
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
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/mem.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "audio_output.h"
#include "resample.h"
#include "decode.h"

static int consume_decoded_audio_samples
(
    lw_audio_output_handler_t *aohp,
    AVFrame                   *frame,
    int                        input_sample_count,
    int                        wanted_sample_count,
    uint8_t                  **out_data,
    int                        sample_offset
)
{
    /* Input */
    uint8_t *in_data[32];
    int decoded_data_offset = sample_offset * aohp->input_block_align;
    for( int i = 0; i < aohp->input_planes; i++ )
        in_data[i] = frame->extended_data[i] + decoded_data_offset;
    audio_samples_t in;
    in.channel_layout = frame->ch_layout.u.mask;
    in.sample_count   = input_sample_count;
    in.sample_format  = (enum AVSampleFormat)frame->format;
    in.data           = in_data;
    /* Output */
    uint8_t *resampled_buffer = NULL;
    if( aohp->s24_output )
    {
        int out_linesize = get_linesize(aohp->output_channel_layout.nb_channels, wanted_sample_count, aohp->output_sample_format);
        if( !aohp->resampled_buffer || out_linesize > aohp->resampled_buffer_size )
        {
            uint8_t *temp = (uint8_t *)av_realloc( aohp->resampled_buffer, out_linesize );
            if( !temp )
                return 0;
            aohp->resampled_buffer_size = out_linesize;
            aohp->resampled_buffer      = temp;
        }
        resampled_buffer = aohp->resampled_buffer;
    }
    audio_samples_t out;
    out.channel_layout = aohp->output_channel_layout.u.mask;
    out.sample_count   = wanted_sample_count;
    out.sample_format  = aohp->output_sample_format;
    out.data           = resampled_buffer ? &resampled_buffer : out_data;
    /* Resample */
    int resampled_size = resample_audio( aohp->swr_ctx, &out, &in );
    if( resampled_buffer && resampled_size > 0 )
        resampled_size = resample_s32_to_s24( out_data, aohp->resampled_buffer, resampled_size );
    return resampled_size > 0 ? resampled_size / aohp->output_block_align : 0;
}

uint64_t output_pcm_samples_from_buffer
(
    lw_audio_output_handler_t *aohp,
    AVFrame                   *frame_buffer,
    uint8_t                  **output_buffer,
    enum audio_output_flag    *output_flags
)
{
    uint64_t output_length = 0;
    if( frame_buffer->extended_data
     && frame_buffer->extended_data[0] )
    {
        /* Flush remaing audio samples. */
        int resampled_length = consume_decoded_audio_samples( aohp, frame_buffer,
                                                              0, (int)aohp->request_length,
                                                              output_buffer, 0 );
        output_length        += resampled_length;
        aohp->request_length -= resampled_length;
        if( aohp->request_length <= 0 )
            *output_flags |= AUDIO_OUTPUT_ENOUGH;
    }
    return output_length;
}

uint64_t output_pcm_samples_from_packet
(
    lw_audio_output_handler_t *aohp,
    AVCodecContext            *ctx,
    AVPacket                  *pkt,
    AVFrame                   *frame_buffer,
    uint8_t                  **output_buffer,
    enum audio_output_flag    *output_flags
)
{
    uint64_t output_length = 0;
    int      output_audio  = 0;
    do
    {
        int decode_complete;
        int consumed_data_length = decode_audio_packet( ctx, frame_buffer, &decode_complete, pkt );
        if( consumed_data_length < 0 )
        {
            /* Force to request the next sample. */
            *output_flags |= AUDIO_DECODER_ERROR;
            pkt->size = 0;
            break;
        }
        if( pkt->data )
        {
            if( consumed_data_length > 0 )
            {
                pkt->size -= consumed_data_length;
                pkt->data += consumed_data_length;
                *output_flags |= AUDIO_DECODER_RECEIVED_PACKET;
            }
        }
        else if( !decode_complete )
        {
            /* No more PCM audio samples in this stream. */
            *output_flags |= AUDIO_OUTPUT_ENOUGH;
            break;
        }
        output_audio |= decode_complete ? 1 : 0;
        if( decode_complete
         && frame_buffer->extended_data
         && frame_buffer->extended_data[0] )
        {
            /* Check channel layout, sample rate and sample format of decoded audio samples. */
            if (frame_buffer->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
                av_channel_layout_default(&frame_buffer->ch_layout, ctx->ch_layout.nb_channels);
            enum AVSampleFormat input_sample_format = (enum AVSampleFormat)frame_buffer->format;
            if( aohp->input_channel_layout.u.mask != frame_buffer->ch_layout.u.mask
             || aohp->input_sample_rate    != frame_buffer->sample_rate
             || aohp->input_sample_format  != input_sample_format )
            {
                /* Detected a change of channel layout, sample rate or sample format.
                 * Reconfigure audio resampler. */
                if( update_resampler_configuration( aohp->swr_ctx,
                                                   &aohp->output_channel_layout,
                                                    aohp->output_sample_rate,
                                                    aohp->output_sample_format,
                                                    &frame_buffer->ch_layout,
                                                    frame_buffer->sample_rate,
                                                    input_sample_format,
                                                    &aohp->input_planes,
                                                    &aohp->input_block_align ) < 0 )
                {
                    *output_flags |= AUDIO_RECONFIG_FAILURE;
                    break;
                }
                av_channel_layout_copy(&aohp->input_channel_layout, &frame_buffer->ch_layout);
                aohp->input_sample_rate    = frame_buffer->sample_rate;
                aohp->input_sample_format  = input_sample_format;
            }
            /* Process decoded audio samples. */
            int decoded_length = frame_buffer->nb_samples;
            if( decoded_length > aohp->output_sample_offset )
            {
                /* Send decoded audio data to resampler and get desired resampled audio as you want as much as possible. */
                int useful_length = (int)(decoded_length - aohp->output_sample_offset);
                int resampled_length = consume_decoded_audio_samples( aohp, frame_buffer,
                                                                      useful_length, (int)aohp->request_length,
                                                                      output_buffer, (int)aohp->output_sample_offset );
                output_length        += resampled_length;
                aohp->request_length -= resampled_length;
                aohp->output_sample_offset = 0;
                if( aohp->request_length <= 0 )
                {
                    *output_flags |= AUDIO_OUTPUT_ENOUGH;
                    break;
                }
            }
            else
                aohp->output_sample_offset -= decoded_length;
        }
    } while( pkt->size > 0 );
    if( !output_audio && pkt->data )
        /* Count audio frame delay only if feeding non-NULL packet. */
        *output_flags |= AUDIO_DECODER_DELAY;
    return output_length;
}

void lw_cleanup_audio_output_handler
(
    lw_audio_output_handler_t *aohp
)
{
    av_channel_layout_uninit(&aohp->output_channel_layout);
    av_channel_layout_uninit(&aohp->input_channel_layout);
    if( aohp->resampled_buffer )
        av_freep( &aohp->resampled_buffer );
    if( aohp->swr_ctx )
        swr_free( &aohp->swr_ctx );
}
