/*****************************************************************************
 * libavsmash_audio.c / libavsmash_audio.cpp
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
#include <lsmash.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "audio_output.h"
#include "resample.h"
#include "libavsmash.h"
#include "libavsmash_audio.h"
#include "libavsmash_audio_internal.h"

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
libavsmash_audio_decode_handler_t *libavsmash_audio_alloc_decode_handler
(
    void
)
{
    libavsmash_audio_decode_handler_t *adhp = (libavsmash_audio_decode_handler_t *)lw_malloc_zero( sizeof(libavsmash_audio_decode_handler_t) );
    if( !adhp )
        return NULL;
    adhp->frame_buffer = av_frame_alloc();
    if( !adhp->frame_buffer )
    {
        libavsmash_audio_free_decode_handler( adhp );
        return NULL;
    }
    return adhp;
}

libavsmash_audio_output_handler_t *libavsmash_audio_alloc_output_handler
(
    void
)
{
    return (libavsmash_audio_output_handler_t *)lw_malloc_zero( sizeof(libavsmash_audio_output_handler_t) );
}

void libavsmash_audio_free_decode_handler
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp )
        return;
    av_frame_free( &adhp->frame_buffer );
    cleanup_configuration( &adhp->config );
    lw_free( adhp );
}

void libavsmash_audio_free_output_handler
(
    libavsmash_audio_output_handler_t *aohp
)
{
    if( !aohp )
        return;
    lw_cleanup_audio_output_handler( aohp );
    lw_free( aohp );
}

void libavsmash_audio_free_decode_handler_ptr
(
    libavsmash_audio_decode_handler_t **adhpp
)
{
    if( !adhpp || !*adhpp )
        return;
    libavsmash_audio_free_decode_handler( *adhpp );
    *adhpp = NULL;
}

void libavsmash_audio_free_output_handler_ptr
(
    libavsmash_audio_output_handler_t **aohpp
)
{
    if( !aohpp || !*aohpp )
        return;
    libavsmash_audio_free_output_handler( *aohpp );
    *aohpp = NULL;
}

/*****************************************************************************
 * Setters
 *****************************************************************************/
void libavsmash_audio_set_root
(
    libavsmash_audio_decode_handler_t *adhp,
    lsmash_root_t                     *root
)
{
    adhp->root = root;
}

void libavsmash_audio_set_track_id
(
    libavsmash_audio_decode_handler_t *adhp,
    uint32_t                           track_id
)
{
    adhp->track_id = track_id;
}

void libavsmash_audio_set_preferred_decoder_names
(
    libavsmash_audio_decode_handler_t *adhp,
    const char                       **preferred_decoder_names
)
{
    adhp->config.preferred_decoder_names = preferred_decoder_names;
}

/*****************************************************************************
 * Getters
 *****************************************************************************/
lsmash_root_t *libavsmash_audio_get_root
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->root : NULL;
}

uint32_t libavsmash_audio_get_track_id
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->track_id : 0;
}

AVCodecContext *libavsmash_audio_get_codec_context
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.ctx : NULL;
}

const char **libavsmash_audio_get_preferred_decoder_names
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.preferred_decoder_names : NULL;
}

int libavsmash_audio_get_error
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.error : -1;
}

uint64_t libavsmash_audio_get_best_used_channel_layout
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.prefer.channel_layout : 0;
}

enum AVSampleFormat libavsmash_audio_get_best_used_sample_format
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.prefer.sample_format : AV_SAMPLE_FMT_NONE;
}

int libavsmash_audio_get_best_used_sample_rate
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.prefer.sample_rate : 0;
}

int libavsmash_audio_get_best_used_bits_per_sample
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->config.prefer.bits_per_sample : 0;
}

lw_log_handler_t *libavsmash_audio_get_log_handler
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? &adhp->config.lh : NULL;
}

uint32_t libavsmash_audio_get_sample_count
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->frame_count : 0;
}

uint32_t libavsmash_audio_get_media_timescale
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->media_timescale : 0;
}

uint64_t libavsmash_audio_get_media_duration
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->media_duration : 0;
}

uint64_t libavsmash_audio_get_min_cts
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return adhp ? adhp->min_cts : UINT64_MAX;
}

/*****************************************************************************
 * Fetchers
 *****************************************************************************/
static uint32_t libavsmash_audio_fetch_sample_count
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp )
        return 0;
    adhp->frame_count = lsmash_get_sample_count_in_media_timeline( adhp->root, adhp->track_id );
    return adhp->frame_count;
}

static uint32_t libavsmash_audio_fetch_media_timescale
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp )
        return 0;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    if( lsmash_get_media_parameters( adhp->root, adhp->track_id, &media_param ) < 0 )
        return 0;
    adhp->media_timescale = media_param.timescale;
    return adhp->media_timescale;
}

static uint64_t libavsmash_audio_fetch_media_duration
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp )
        return 0;
    adhp->media_duration = lsmash_get_media_duration_from_media_timeline( adhp->root, adhp->track_id );
    return adhp->media_duration;
}

/* This function assume that no audio frame reorderings in composition timeline. */
static uint64_t libavsmash_audio_fetch_min_cts
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp || lsmash_get_cts_from_media_timeline( adhp->root, adhp->track_id, 1, &adhp->min_cts ) < 0 )
        return UINT64_MAX;
    return adhp->min_cts;
}

/*****************************************************************************
 * Others
 *****************************************************************************/
int libavsmash_audio_get_track
(
    libavsmash_audio_decode_handler_t *adhp,
    uint32_t                           track_number
)
{
    lw_log_handler_t *lhp = libavsmash_audio_get_log_handler( adhp );
    uint32_t track_id =
        libavsmash_get_track_by_media_type
        (
            libavsmash_audio_get_root( adhp ),
            ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK,
            track_number,
            lhp
        );
    if( track_id == 0 )
        return -1;
    libavsmash_audio_set_track_id( adhp, track_id );
    (void)libavsmash_audio_fetch_sample_count   ( adhp );
    (void)libavsmash_audio_fetch_media_duration ( adhp );
    (void)libavsmash_audio_fetch_media_timescale( adhp );
    (void)libavsmash_audio_fetch_min_cts        ( adhp );
    return 0;
}

int libavsmash_audio_initialize_decoder_configuration
(
    libavsmash_audio_decode_handler_t *adhp,
    AVFormatContext                   *format_ctx,
    int                                threads
)
{
    char error_string[128] = { 0 };
    if( libavsmash_audio_get_summaries( adhp ) < 0 )
        return -1;
    /* libavformat */
    uint32_t type = AVMEDIA_TYPE_AUDIO;
    uint32_t i;
    for( i = 0; i < format_ctx->nb_streams && format_ctx->streams[i]->codecpar->codec_type != type; i++ );
    if( i == format_ctx->nb_streams )
    {
        strcpy( error_string, "Failed to find stream by libavformat.\n" );
        goto fail;
    }
    /* libavcodec */
    AVCodecParameters *codecpar = format_ctx->streams[i]->codecpar;
    if( libavsmash_find_and_open_decoder( &adhp->config, codecpar, threads ) < 0 )
    {
        strcpy( error_string, "Failed to find and open the audio decoder.\n" );
        goto fail;
    }
    return initialize_decoder_configuration( adhp->root, adhp->track_id, &adhp->config );
fail:;
    lw_log_handler_t *lhp = libavsmash_audio_get_log_handler( adhp );
    lw_log_show( lhp, LW_LOG_FATAL, "%s", error_string );
    return -1;
}

int libavsmash_audio_get_summaries
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    return get_summaries( adhp->root, adhp->track_id, &adhp->config );
}

void libavsmash_audio_force_seek
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    /* Force seek before the next reading. */
    adhp->next_pcm_sample_number = adhp->pcm_sample_count + 1;
}

void libavsmash_audio_clear_error
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    adhp->config.error = 0;
}

void libavsmash_audio_close_codec_context
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp || !adhp->config.ctx )
        return;
    avcodec_free_context( &adhp->config.ctx );
}

void libavsmash_audio_apply_delay
(
    libavsmash_audio_decode_handler_t *adhp,
    int64_t                            delay
)
{
    if( !adhp )
        return;
    adhp->pcm_sample_count += delay;
}

void libavsmash_audio_set_implicit_preroll
(
    libavsmash_audio_decode_handler_t *adhp
)
{
    if( !adhp )
        return;
    adhp->implicit_preroll = 1;
}

static inline uint64_t count_sequence_output_pcm_samples
(
    uint64_t sequence_pcm_count,
    int      current_sample_rate,
    int      output_sample_rate
)
{
    uint64_t resampled_sample_count;
    if( output_sample_rate == current_sample_rate )
        resampled_sample_count = sequence_pcm_count;
    else
        resampled_sample_count = av_rescale_rnd( sequence_pcm_count, output_sample_rate, current_sample_rate, AV_ROUND_UP );
    return resampled_sample_count;
}

static int get_frame_length
(
    libavsmash_audio_decode_handler_t *adhp,
    uint32_t                           frame_number,
    uint64_t                          *frame_length,
    extended_summary_t               **esp
)
{
    lsmash_sample_t sample;
    if( lsmash_get_sample_info_from_media_timeline( adhp->root, adhp->track_id, frame_number, &sample ) < 0 )
        return -1;
    *esp = &adhp->config.entries[ sample.index - 1 ].extended;
    extended_summary_t *es = *esp;
    if( es->frame_length == 0 )
    {
        /* variable frame length
         * Guess the frame length from sample duration. */
        uint32_t frame_length_32;
        if( lsmash_get_sample_delta_from_media_timeline( adhp->root, adhp->track_id, frame_number, &frame_length_32 ) < 0 )
            return -1;
        int64_t temp_frame_length = av_rescale( frame_length_32, es->sample_rate, adhp->media_timescale );
        if( temp_frame_length < 0 )
            return -1;
        *frame_length = (uint64_t)temp_frame_length;
    }
    else
        /* constant frame length */
        *frame_length = (uint64_t)es->frame_length;
    return 0;
}

/* Count the number of whole output PCM samples. */
uint64_t libavsmash_audio_count_overall_pcm_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    int                                output_sample_rate,
    uint64_t                           start_time
)
{
    codec_configuration_t *config = &adhp->config;
    extended_summary_t    *es     = NULL;
    int      current_sample_rate       = 0;
    uint64_t current_frame_length      = 0;
    uint64_t sequence_pcm_count        = 0;
    uint64_t prior_sequences_pcm_count = 0;
    uint64_t overall_pcm_count         = 0;
    /* Count the number of output PCM audio samples in each sequence. */
    for( uint32_t i = 1; i <= adhp->frame_count; i++ )
    {
        uint64_t frame_length;
        if( get_frame_length( adhp, i, &frame_length, &es ) < 0 )
            continue;
        if( (current_sample_rate != es->sample_rate && es->sample_rate > 0)
         || current_frame_length != frame_length )
        {
            /* Encountered a new sequence. */
            if( current_sample_rate > 0 )
            {
                prior_sequences_pcm_count += sequence_pcm_count;
                /* Add the number of output PCM audio samples in the previous sequence. */
                overall_pcm_count += count_sequence_output_pcm_samples( sequence_pcm_count,
                                                                        current_sample_rate,
                                                                        output_sample_rate );
                sequence_pcm_count = 0;
            }
            current_sample_rate  = es->sample_rate > 0 ? es->sample_rate : config->ctx->sample_rate;
            current_frame_length = frame_length;
        }
        sequence_pcm_count += frame_length;
    }
    if( !es || (sequence_pcm_count == 0 && overall_pcm_count == 0) )
    {
        adhp->pcm_sample_count = 0;
        return 0;
    }
    current_sample_rate = es->sample_rate > 0 ? es->sample_rate : config->ctx->sample_rate;
    overall_pcm_count += count_sequence_output_pcm_samples( sequence_pcm_count,
                                                            current_sample_rate,
                                                            output_sample_rate );
    overall_pcm_count -= av_rescale( start_time, output_sample_rate, adhp->media_timescale );
    adhp->pcm_sample_count = overall_pcm_count;
    return overall_pcm_count;
}

/* Get pre-roll to make output samples assured to be correct, and update the target frame number if needed.
 * Some audio CODEC requires pre-roll for correct composition. */
static uint64_t get_preroll_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    uint64_t                           skip_decoded_samples,
    uint32_t                          *frame_number
)
{
    lsmash_sample_property_t prop;
    if( lsmash_get_sample_property_from_media_timeline( adhp->root, adhp->track_id, *frame_number, &prop ) < 0 )
        return 0;
    if( prop.pre_roll.distance == 0 )
    {
        if( skip_decoded_samples == 0 || !adhp->implicit_preroll )
            return 0;
        /* Estimate pre-roll distance. */
        for( uint32_t i = 1; i <= adhp->frame_count || skip_decoded_samples; i++ )
        {
            extended_summary_t *dummy = NULL;
            uint64_t frame_length;
            if( get_frame_length( adhp, i, &frame_length, &dummy ) < 0 )
                break;
            if( skip_decoded_samples < frame_length )
                skip_decoded_samples = 0;
            else
                skip_decoded_samples -= frame_length;
            ++ prop.pre_roll.distance;
        }
    }
    uint64_t preroll_samples = 0;
    for( uint32_t i = 0; i < prop.pre_roll.distance; i++ )
    {
        if( *frame_number > 1 )
            --(*frame_number);
        else
            break;
        extended_summary_t *dummy = NULL;
        uint64_t frame_length;
        if( get_frame_length( adhp, *frame_number, &frame_length, &dummy ) < 0 )
            break;
        preroll_samples += frame_length;
    }
    return preroll_samples;
}

static int find_start_audio_frame
(
    libavsmash_audio_decode_handler_t *adhp,
    int                                output_sample_rate,
    uint64_t                           skip_decoded_samples,    /* at output sampling rate */
    uint64_t                           start_frame_pos,         /* at output sampling rate */
    uint64_t                          *start_offset             /* at codec sampling rate since trimming by this before sending resampler */
)
{
    uint32_t frame_number                    = 1;
    uint64_t current_frame_pos               = 0;
    uint64_t next_frame_pos                  = 0;
    int      current_sample_rate             = 0;
    uint64_t current_frame_length            = 0;
    uint64_t decoded_pcm_sample_count        = 0;   /* the number of accumulated PCM samples before resampling per sequence */
    uint64_t resampled_sample_count          = 0;   /* the number of accumulated PCM samples after resampling per sequence */
    uint64_t prior_sequences_resampled_count = 0;   /* the number of accumulated PCM samples of all prior sequences */
    do
    {
        current_frame_pos = next_frame_pos;
        extended_summary_t *es = NULL;
        uint64_t frame_length;
        if( get_frame_length( adhp, frame_number, &frame_length, &es ) < 0 )
        {
            ++frame_number;
            continue;
        }
        if( (current_sample_rate != es->sample_rate && es->sample_rate > 0)
         || current_frame_length != frame_length )
        {
            /* Encountered a new sequence. */
            prior_sequences_resampled_count += resampled_sample_count;
            decoded_pcm_sample_count = 0;
            current_sample_rate  = es->sample_rate > 0 ? es->sample_rate : adhp->config.ctx->sample_rate;
            current_frame_length = frame_length;
        }
        decoded_pcm_sample_count += frame_length;
        resampled_sample_count = count_sequence_output_pcm_samples( decoded_pcm_sample_count,
                                                                    current_sample_rate,
                                                                    output_sample_rate );
        next_frame_pos = prior_sequences_resampled_count + resampled_sample_count;
        if( start_frame_pos < next_frame_pos )
            break;
        ++frame_number;
    } while( frame_number <= adhp->frame_count );
    *start_offset  = start_frame_pos - current_frame_pos;
    *start_offset  = av_rescale_rnd( *start_offset, current_sample_rate, output_sample_rate, AV_ROUND_UP );
    *start_offset += get_preroll_samples( adhp, av_rescale( skip_decoded_samples, current_sample_rate, output_sample_rate ), &frame_number );
    return frame_number;
}

uint64_t libavsmash_audio_get_pcm_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    libavsmash_audio_output_handler_t *aohp,
    void                              *buf,
    int64_t                            start,
    int64_t                            wanted_length
)
{
    codec_configuration_t *config = &adhp->config;
    if( config->error )
        return 0;
    uint32_t               frame_number;
    uint64_t               output_length = 0;
    enum audio_output_flag output_flags;
    aohp->request_length = wanted_length;
    if( start > 0 && start == adhp->next_pcm_sample_number )
    {
        frame_number   = adhp->last_frame_number;
        output_flags   = AUDIO_OUTPUT_NO_FLAGS;
        output_length += output_pcm_samples_from_buffer( aohp, adhp->frame_buffer, (uint8_t **)&buf, &output_flags );
        if( output_flags & AUDIO_OUTPUT_ENOUGH )
            goto audio_out;
        if( adhp->packet.size <= 0 )
            ++frame_number;
        aohp->output_sample_offset = 0;
    }
    else
    {
        /* Seek audio stream. */
        if( flush_resampler_buffers( aohp->swr_ctx ) < 0 )
        {
            config->error = 1;
            lw_log_show( &config->lh, LW_LOG_FATAL,
                         "Failed to flush resampler buffers.\n"
                         "It is recommended you reopen the file." );
            return 0;
        }
        libavsmash_flush_buffers( config );
        if( config->error )
            return 0;
        adhp->next_pcm_sample_number = 0;
        adhp->last_frame_number      = 0;
        uint64_t start_frame_pos;
        if( start >= 0 )
            start_frame_pos = start;
        else
        {
            uint64_t silence_length = -start;
            put_silence_audio_samples( (int)(silence_length * aohp->output_block_align), aohp->output_bits_per_sample == 8, (uint8_t **)&buf );
            output_length        += silence_length;
            aohp->request_length -= silence_length;
            start_frame_pos = 0;
        }
        start_frame_pos += aohp->skip_decoded_samples;
        frame_number = find_start_audio_frame( adhp, aohp->output_sample_rate, aohp->skip_decoded_samples, start_frame_pos, &aohp->output_sample_offset );
    }
    do
    {
        AVPacket *pkt = &adhp->packet;
        if( frame_number > adhp->frame_count )
        {
            if( config->delay_count || !(output_flags & AUDIO_OUTPUT_ENOUGH) )
            {
                /* Null packet */
                av_init_packet( pkt );
                pkt->data = NULL;
                pkt->size = 0;
                if( config->delay_count )
                    config->delay_count -= 1;
            }
            else
                goto audio_out;
        }
        else if( pkt->size <= 0 )
            /* Getting an audio packet must be after flushing all remaining samples in resampler's FIFO buffer. */
            while( get_sample( adhp->root, adhp->track_id, frame_number, config, pkt ) == 2 )
                if( config->update_pending )
                    /* Update the decoder configuration. */
                    update_configuration( adhp->root, adhp->track_id, config );
        /* Decode and output from an audio packet. */
        output_flags   = AUDIO_OUTPUT_NO_FLAGS;
        output_length += output_pcm_samples_from_packet( aohp, config->ctx, pkt, adhp->frame_buffer, (uint8_t **)&buf, &output_flags );
        if( output_flags & AUDIO_DECODER_DELAY )
            ++ config->delay_count;
        if( output_flags & AUDIO_RECONFIG_FAILURE )
        {
            config->error = 1;
            lw_log_show( &config->lh, LW_LOG_FATAL,
                         "Failed to reconfigure resampler.\n"
                         "It is recommended you reopen the file." );
            goto audio_out;
        }
        if( output_flags & AUDIO_OUTPUT_ENOUGH )
            goto audio_out;
        if( output_flags & (AUDIO_DECODER_ERROR | AUDIO_DECODER_RECEIVED_PACKET) )
            ++frame_number;
    } while( 1 );
audio_out:
    adhp->next_pcm_sample_number = start + output_length;
    adhp->last_frame_number      = frame_number;
    return output_length;
}
