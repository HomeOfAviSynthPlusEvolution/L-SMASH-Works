/*****************************************************************************
 * libavsmash_video.c / libavsmash_video.cpp
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

#include <inttypes.h>
#include <float.h>

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <lsmash.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "video_output.h"
#include "libavsmash.h"
#include "libavsmash_video.h"
#include "libavsmash_video_internal.h"
#include "decode.h"

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
libavsmash_video_decode_handler_t *libavsmash_video_alloc_decode_handler
(
    void
)
{
    libavsmash_video_decode_handler_t *vdhp = (libavsmash_video_decode_handler_t *)lw_malloc_zero( sizeof(libavsmash_video_decode_handler_t) );
    if( !vdhp )
        return NULL;
    vdhp->frame_buffer = av_frame_alloc();
    if( !vdhp->frame_buffer )
    {
        libavsmash_video_free_decode_handler( vdhp );
        return NULL;
    }
    return vdhp;
}

libavsmash_video_output_handler_t *libavsmash_video_alloc_output_handler
(
    void
)
{
    return (libavsmash_video_output_handler_t *)lw_malloc_zero( sizeof(libavsmash_video_output_handler_t) );
}

void libavsmash_video_free_decode_handler
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    if( !vdhp )
        return;
    lw_freep( &vdhp->keyframe_list );
    lw_freep( &vdhp->order_converter );
    av_frame_free( &vdhp->frame_buffer );
    av_frame_free( &vdhp->first_valid_frame );
    cleanup_configuration( &vdhp->config );
    lw_free( vdhp );
}

void libavsmash_video_free_output_handler
(
    libavsmash_video_output_handler_t *vohp
)
{
    if( !vohp )
        return;
    lw_cleanup_video_output_handler( vohp );
    lw_free( vohp );
}

void libavsmash_video_free_decode_handler_ptr
(
    libavsmash_video_decode_handler_t **vdhpp
)
{
    if( !vdhpp || !*vdhpp )
        return;
    libavsmash_video_free_decode_handler( *vdhpp );
    *vdhpp = NULL;
}

void libavsmash_video_free_output_handler_ptr
(
    libavsmash_video_output_handler_t **vohpp
)
{
    if( !vohpp || !*vohpp )
        return;
    libavsmash_video_free_output_handler( *vohpp );
    *vohpp = NULL;
}

/*****************************************************************************
 * Setters
 *****************************************************************************/
void libavsmash_video_set_root
(
    libavsmash_video_decode_handler_t *vdhp,
    lsmash_root_t                     *root
)
{
    vdhp->root = root;
}

void libavsmash_video_set_track_id
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           track_id
)
{
    vdhp->track_id = track_id;
}

void libavsmash_video_set_forward_seek_threshold
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           forward_seek_threshold
)
{
    vdhp->forward_seek_threshold = forward_seek_threshold;
}

void libavsmash_video_set_seek_mode
(
    libavsmash_video_decode_handler_t *vdhp,
    int                                seek_mode
)
{
    vdhp->seek_mode = seek_mode;
}

void libavsmash_video_set_preferred_decoder_names
(
    libavsmash_video_decode_handler_t *vdhp,
    const char                       **preferred_decoder_names
)
{
    vdhp->config.preferred_decoder_names = preferred_decoder_names;
}

void libavsmash_video_set_prefer_hw_decoder
(
    libavsmash_video_decode_handler_t *vdhp,
    int                                prefer_hw_decoder
)
{
    vdhp->config.prefer_hw_decoder = prefer_hw_decoder;
}

void libavsmash_video_set_log_handler
(
    libavsmash_video_decode_handler_t *vdhp,
    lw_log_handler_t                  *lh
)
{
    vdhp->config.lh = *lh;
}

void libavsmash_video_set_get_buffer_func
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    vdhp->config.get_buffer = vdhp->config.ctx->get_buffer2;
}

/*****************************************************************************
 * Getters
 *****************************************************************************/
lsmash_root_t *libavsmash_video_get_root
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->root : NULL;
}

uint32_t libavsmash_video_get_track_id
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->track_id : 0;
}

uint32_t libavsmash_video_get_forward_seek_threshold
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->forward_seek_threshold : 0;
}

int libavsmash_video_get_seek_mode
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->seek_mode : -1;
}

const char **libavsmash_video_get_preferred_decoder_names
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->config.preferred_decoder_names : NULL;
}

int libavsmash_video_get_error
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->config.error : -1;
}

lw_log_handler_t *libavsmash_video_get_log_handler
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? &vdhp->config.lh : NULL;
}

AVCodecContext *libavsmash_video_get_codec_context
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->config.ctx : NULL;
}

int libavsmash_video_get_max_width
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->config.prefer.width : 0;
}

int libavsmash_video_get_max_height
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->config.prefer.height : 0;
}

AVFrame *libavsmash_video_get_frame_buffer
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->frame_buffer : NULL;
}

uint32_t libavsmash_video_get_sample_count
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->sample_count : 0;
}

uint32_t libavsmash_video_get_media_timescale
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->media_timescale : 0;
}

uint64_t libavsmash_video_get_media_duration
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->media_duration : 0;
}

uint64_t libavsmash_video_get_min_cts
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->min_cts : 0;
}

/*****************************************************************************
 * Fetchers
 *****************************************************************************/
static uint32_t libavsmash_video_fetch_sample_count
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    if( !vdhp )
        return 0;
    vdhp->sample_count = lsmash_get_sample_count_in_media_timeline( vdhp->root, vdhp->track_id );
    return vdhp->sample_count;
}

static uint32_t libavsmash_video_fetch_media_timescale
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    if( !vdhp )
        return 0;
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    if( lsmash_get_media_parameters( vdhp->root, vdhp->track_id, &media_param ) < 0 )
        return 0;
    vdhp->media_timescale = media_param.timescale;
    return vdhp->media_timescale;
}

static uint64_t libavsmash_video_fetch_media_duration
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    if( !vdhp )
        return 0;
    vdhp->media_duration = lsmash_get_media_duration_from_media_timeline( vdhp->root, vdhp->track_id );
    return vdhp->media_duration;
}

/*****************************************************************************
 * Others
 *****************************************************************************/
int libavsmash_video_get_track
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           track_number
)
{
    lw_log_handler_t *lhp = libavsmash_video_get_log_handler( vdhp );
    uint32_t track_id =
        libavsmash_get_track_by_media_type
        (
            libavsmash_video_get_root( vdhp ),
            ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK,
            track_number,
            lhp
        );
    if( track_id == 0 )
        return -1;
    libavsmash_video_set_track_id( vdhp, track_id );
    (void)libavsmash_video_fetch_sample_count   ( vdhp );
    (void)libavsmash_video_fetch_media_duration ( vdhp );
    (void)libavsmash_video_fetch_media_timescale( vdhp );
    return 0;
}

int libavsmash_video_initialize_decoder_configuration
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFormatContext                   *format_ctx,
    int                                threads
)
{
    char error_string[128] = { 0 };
    if( libavsmash_video_get_summaries( vdhp ) < 0 )
        return -1;
    /* libavformat */
    uint32_t type = AVMEDIA_TYPE_VIDEO;
    uint32_t i;
    for( i = 0; i < format_ctx->nb_streams && format_ctx->streams[i]->codecpar->codec_type != type; i++ );
    if( i == format_ctx->nb_streams )
    {
        strcpy( error_string, "Failed to find stream by libavformat.\n" );
        goto fail;
    }
    /* libavcodec */
    AVCodecParameters *codecpar = format_ctx->streams[i]->codecpar;
    if( libavsmash_find_and_open_decoder( &vdhp->config, codecpar, threads ) < 0 )
    {
        strcpy( error_string, "Failed to find and open the video decoder.\n" );
        goto fail;
    }
    return initialize_decoder_configuration( vdhp->root, vdhp->track_id, &vdhp->config );
fail:;
    lw_log_handler_t *lhp = libavsmash_video_get_log_handler( vdhp );
    lw_log_show( lhp, LW_LOG_FATAL, "%s", error_string );
    return -1;
}

int libavsmash_video_get_summaries
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    return get_summaries( vdhp->root, vdhp->track_id, &vdhp->config );
}

void libavsmash_video_force_seek
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    /* Force seek before the next reading. */
    vdhp->last_sample_number = vdhp->sample_count + 1;
}

static inline uint32_t get_decoding_sample_number
(
    order_converter_t *order_converter,
    uint32_t           composition_sample_number
)
{
    return order_converter
         ? order_converter[composition_sample_number].composition_to_decoding
         : composition_sample_number;
}

uint32_t libavsmash_video_get_coded_sample_number
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           composition_sample_number
)
{
    return get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
}

int libavsmash_video_get_cts
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           coded_sample_number,
    uint64_t                          *cts
)
{
    return lsmash_get_cts_from_media_timeline( vdhp->root, vdhp->track_id, coded_sample_number, cts );
}

int libavsmash_video_get_sample_duration
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           coded_sample_number,
    uint32_t                          *sample_duration
)
{
    return lsmash_get_sample_delta_from_media_timeline( vdhp->root, vdhp->track_id, coded_sample_number, sample_duration );
}

void libavsmash_video_clear_error
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    vdhp->config.error = 0;
}

void libavsmash_video_close_codec_context
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    if( !vdhp || !vdhp->config.ctx )
        return;
    avcodec_free_context( &vdhp->config.ctx );
}

int libavsmash_video_setup_timestamp_info
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    int64_t                           *framerate_num,
    int64_t                           *framerate_den
)
{
    int err = -1;
    uint64_t media_timescale = lsmash_get_media_timescale( vdhp->root, vdhp->track_id );
    uint64_t media_duration  = lsmash_get_media_duration_from_media_timeline( vdhp->root, vdhp->track_id );
    if( media_duration == 0 )
        media_duration = INT32_MAX;
    if( vdhp->sample_count == 1 )
    {
        /* Calculate average framerate. */
        reduce_fraction( &media_timescale, &media_duration );
        *framerate_num = (int64_t)media_timescale;
        *framerate_den = (int64_t)media_duration;
        err = 0;
        goto setup_finish;
    }
    lw_log_handler_t *lhp = &vdhp->config.lh;
    lsmash_media_ts_list_t ts_list;
    if( lsmash_get_media_timestamps( vdhp->root, vdhp->track_id, &ts_list ) < 0 )
    {
        lw_log_show( lhp, LW_LOG_ERROR, "Failed to get timestamps." );
        goto setup_finish;
    }
    if( ts_list.sample_count != vdhp->sample_count )
    {
        lw_log_show( lhp, LW_LOG_ERROR, "Failed to count number of video samples." );
        goto setup_finish;
    }
    uint32_t composition_sample_delay;
    if( lsmash_get_max_sample_delay( &ts_list, &composition_sample_delay ) < 0 )
    {
        lsmash_delete_media_timestamps( &ts_list );
        lw_log_show( lhp, LW_LOG_ERROR, "Failed to get composition delay." );
        goto setup_finish;
    }
    if( composition_sample_delay )
    {
        /* Consider composition order for keyframe detection.
         * Note: sample number for L-SMASH is 1-origin. */
        vdhp->order_converter = (order_converter_t *)lw_malloc_zero( (ts_list.sample_count + 1) * sizeof(order_converter_t) );
        if( !vdhp->order_converter )
        {
            lsmash_delete_media_timestamps( &ts_list );
            lw_log_show( lhp, LW_LOG_ERROR, "Failed to allocate memory." );
            goto setup_finish;
        }
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            ts_list.timestamp[i].dts = i + 1;
        lsmash_sort_timestamps_composition_order( &ts_list );
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            vdhp->order_converter[i + 1].composition_to_decoding = (uint32_t)ts_list.timestamp[i].dts;
    }
    /* Calculate average framerate. */
    uint64_t largest_cts          = ts_list.timestamp[0].cts;
    uint64_t second_largest_cts   = 0;
    uint64_t first_duration       = ts_list.timestamp[1].cts - ts_list.timestamp[0].cts;
    uint64_t composition_timebase = first_duration;
    int      strict_cfr           = 1;
    for( uint32_t i = 1; i < ts_list.sample_count; i++ )
    {
        uint64_t duration = ts_list.timestamp[i].cts - ts_list.timestamp[i - 1].cts;
        if( duration == 0 )
        {
            lsmash_delete_media_timestamps( &ts_list );
            lw_log_show( lhp, LW_LOG_WARNING, "Detected CTS duplication at frame %" PRIu32, i );
            err = 0;
            goto setup_finish;
        }
        if( strict_cfr && duration != first_duration )
            strict_cfr = 0;
        composition_timebase = get_gcd( composition_timebase, duration );
        second_largest_cts   = largest_cts;
        largest_cts          = ts_list.timestamp[i].cts;
    }
    uint64_t reduce = reduce_fraction( &media_timescale, &composition_timebase );
    uint64_t composition_duration = ((largest_cts - ts_list.timestamp[0].cts) + (largest_cts - second_largest_cts)) / reduce;
    lsmash_delete_media_timestamps( &ts_list );
    double avg_frame_rate = (vdhp->sample_count * ((double)media_timescale / composition_duration));
    if( strict_cfr || !lw_try_rational_framerate( avg_frame_rate, framerate_num, framerate_den, composition_timebase ) )
    {
        uint64_t num = (uint64_t)(avg_frame_rate * composition_timebase + 0.5);
        uint64_t den = composition_timebase;
        if( num && den )
            reduce_fraction( &num, &den );
        else
        {
            num = 1;
            den = 1;
        }
        *framerate_num = (int64_t)num;
        *framerate_den = (int64_t)den;
    }
    err = 0;
setup_finish:;
    if( vohp->vfr2cfr )
    {
        /* Override average framerate by specified output constant framerate. */
        *framerate_num = (int64_t)vohp->cfr_num;
        *framerate_den = (int64_t)vohp->cfr_den;
        vohp->frame_count = ((double)vohp->cfr_num / vohp->cfr_den)
                          * ((double)media_duration / media_timescale)
                          + 0.5;
    }
    else
        vohp->frame_count = libavsmash_video_get_sample_count( vdhp );
    uint32_t min_cts_sample_number = get_decoding_sample_number( vdhp->order_converter, 1 );
    vdhp->config.error = lsmash_get_cts_from_media_timeline( vdhp->root, vdhp->track_id, min_cts_sample_number, &vdhp->min_cts );
    return err;
}

static int decode_video_sample
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFrame                           *picture,
    int                               *got_picture,
    uint32_t                           sample_number
)
{
    codec_configuration_t *config = &vdhp->config;
    AVPacket pkt = { 0 };
    int ret = get_sample( vdhp->root, vdhp->track_id, sample_number, config, &pkt );
    if( ret )
        return ret;
    if( pkt.flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
    {
        pkt.flags = AV_PKT_FLAG_KEY;
        vdhp->last_rap_number = sample_number;
    }
    else
        pkt.flags = 0;
    av_frame_unref( picture );
    uint64_t cts = pkt.pts;
    ret = decode_video_packet( config->ctx, picture, got_picture, &pkt );
    picture->pts = cts;
    if( ret < 0 )
    {
        lw_log_show( &config->lh, LW_LOG_WARNING, "Failed to decode a video frame." );
        return -1;
    }
    return 0;
}

static int find_random_accessible_point
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           composition_sample_number,
    uint32_t                           decoding_sample_number,
    uint32_t                          *rap_number
)
{
    if( decoding_sample_number == 0 )
        decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
    lsmash_random_access_flag ra_flags;
    uint32_t distance;  /* distance from the closest random accessible point to the previous. */
    uint32_t number_of_leadings;
    if( lsmash_get_closest_random_accessible_point_detail_from_media_timeline( vdhp->root, vdhp->track_id,
                                                                               decoding_sample_number, rap_number,
                                                                               &ra_flags, &number_of_leadings, &distance ) < 0 )
        *rap_number = 1;
    int roll_recovery = !!(ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_GDR);
    int is_leading    = number_of_leadings && (decoding_sample_number - *rap_number <= number_of_leadings);
    if( (roll_recovery || is_leading) && *rap_number > distance )
        *rap_number -= distance;
    /* Check whether random accessible point has the same decoder configuration or not. */
    decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
    do
    {
        lsmash_sample_t sample;
        lsmash_sample_t rap_sample;
        if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, decoding_sample_number, &sample ) < 0
         || lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, *rap_number, &rap_sample ) < 0 )
        {
            /* Fatal error. */
            *rap_number = vdhp->last_rap_number;
            return 0;
        }
        if( sample.index == rap_sample.index )
            break;
        uint32_t sample_index = sample.index;
        for( uint32_t i = decoding_sample_number - 1; i; i-- )
        {
            if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, i, &sample ) < 0 )
            {
                /* Fatal error. */
                *rap_number = vdhp->last_rap_number;
                return 0;
            }
            if( sample.index != sample_index )
            {
                if( distance )
                {
                    *rap_number += distance;
                    distance = 0;
                    continue;
                }
                else
                    *rap_number = i + 1;
            }
        }
        break;
    } while( 1 );
    return roll_recovery;
}

/* This function returns the number of the next sample. */
static uint32_t seek_video
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFrame                           *picture,
    uint32_t                           composition_sample_number,
    uint32_t                           rap_number,
    int                                error_ignorance
)
{
    /* Prepare to decode from random accessible sample. */
    codec_configuration_t *config = &vdhp->config;
    if( config->update_pending )
        /* Update the decoder configuration. */
        update_configuration( vdhp->root, vdhp->track_id, config );
    else
        libavsmash_flush_buffers( config );
    if( config->error )
        return 0;
    int got_picture;
    int output_ready = 0;
    uint64_t rap_cts = 0;
    uint32_t i;
    uint32_t decoder_delay = get_decoder_delay( config->ctx );
    uint32_t goal = composition_sample_number + decoder_delay;
    for( i = rap_number; i < goal; i++ )
    {
        if( config->index == config->queue.index )
            config->delay_count = MIN( decoder_delay, i - rap_number );
        int ret = decode_video_sample( vdhp, picture, &got_picture, i );
        if( got_picture )
        {
            output_ready = 1;
            if( decoder_delay > config->delay_count )
            {
                /* Shorten the distance to the goal if we got a frame earlier than expected. */
                uint32_t new_decoder_delay = config->delay_count;
                goal -= decoder_delay - new_decoder_delay;
                decoder_delay = new_decoder_delay;
            }
        }
        else if( output_ready )
        {
            /* More input samples are required to output and the goal become more distant. */
            ++decoder_delay;
            ++goal;
        }
        /* Some decoders return -1 when feeding a leading sample.
         * We don't consider as an error if the return value -1 is caused by a leading sample since it's not fatal at all. */
        if( i == vdhp->last_rap_number )
            rap_cts = picture->pts;
        if( ret == -1 && (uint64_t)picture->pts >= rap_cts && !error_ignorance )
        {
            lw_log_show( &config->lh, LW_LOG_WARNING, "Failed to decode a video frame." );
            return 0;
        }
        else if( ret >= 1 )
            /* No decoding occurs. */
            break;
    }
    if( config->index == config->queue.index )
        config->delay_count = MIN( decoder_delay, i - rap_number );
    return i;
}

static int get_picture
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFrame                           *picture,
    uint32_t                           current,
    uint32_t                           goal
)
{
    codec_configuration_t *config = &vdhp->config;
    int got_picture = (current > goal);
    while( current <= goal )
    {
        int ret = decode_video_sample( vdhp, picture, &got_picture, current );
        if( ret == -1 )
            return -1;
        else if( ret == 1 )
            /* Sample doesn't exist. */
            break;
        ++current;
        if( config->update_pending )
            /* A new decoder configuration is needed. Anyway, stop getting picture. */
            break;
        if( !got_picture )
        {
            /* Fundamental seek operations after the decoder initialization is already done, but
             * more input samples are required to output and the goal become more distant. */
            ++ config->delay_count;
            ++ goal;
        }
    }
    /* Flush the last frames. */
    if( current > vdhp->sample_count && get_decoder_delay( config->ctx ) )
        while( current <= goal )
        {
            AVPacket pkt = { 0 };
            av_init_packet( &pkt );
            pkt.data = NULL;
            pkt.size = 0;
            av_frame_unref( picture );
            if( decode_video_packet( config->ctx, picture, &got_picture, &pkt ) < 0 )
            {
                lw_log_show( &config->lh, LW_LOG_WARNING, "Failed to decode and flush a video frame." );
                return -1;
            }
            ++current;
        }
    return got_picture ? 0 : -1;
}

static int get_requested_picture
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFrame                           *picture,
    uint32_t                           sample_number
)
{
#define MAX_ERROR_COUNT 3       /* arbitrary */
    codec_configuration_t *config = &vdhp->config;
    uint32_t config_index;
    if( sample_number < vdhp->first_valid_frame_number || vdhp->sample_count == 1 )
    {
        /* Get the index of the decoder configuration. */
        lsmash_sample_t sample;
        uint32_t decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, vdhp->first_valid_frame_number );
        if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, decoding_sample_number, &sample ) < 0 )
            goto video_fail;
        config_index = sample.index;
        /* Copy the first valid video frame data. */
        av_frame_unref( picture );
        if( av_frame_ref( picture, vdhp->first_valid_frame ) < 0 )
            goto video_fail;
        /* Force seeking at the next access for valid video frame. */
        vdhp->last_sample_number = vdhp->sample_count + 1;
        goto return_frame;
    }
    uint32_t start_number;  /* number of sample, for normal decoding, where decoding starts excluding decoding delay */
    uint32_t rap_number;    /* number of sample, for seeking, where decoding starts excluding decoding delay */
    int seek_mode = vdhp->seek_mode;
    int roll_recovery = 0;
    if( sample_number > vdhp->last_sample_number
     && sample_number <= vdhp->last_sample_number + vdhp->forward_seek_threshold )
    {
        start_number = vdhp->last_sample_number + 1 + config->delay_count;
        rap_number   = vdhp->last_rap_number;
    }
    else
    {
        roll_recovery = find_random_accessible_point( vdhp, sample_number, 0, &rap_number );
        if( rap_number == vdhp->last_rap_number && sample_number > vdhp->last_sample_number )
        {
            roll_recovery = 0;
            start_number  = vdhp->last_sample_number + 1 + config->delay_count;
        }
        else
        {
            /* Require starting to decode from random accessible sample. */
            vdhp->last_rap_number = rap_number;
            start_number = seek_video( vdhp, picture, sample_number, rap_number, roll_recovery || seek_mode != SEEK_MODE_NORMAL );
        }
    }
    /* Get the desired picture. */
    int error_count = 0;
    while( start_number == 0    /* Failed to seek. */
     || config->update_pending  /* Need to update the decoder configuration to decode pictures. */
     || get_picture( vdhp, picture, start_number, sample_number + config->delay_count ) < 0 )
    {
        if( config->update_pending )
        {
            roll_recovery = find_random_accessible_point( vdhp, sample_number, 0, &rap_number );
            vdhp->last_rap_number = rap_number;
        }
        else
        {
            /* Failed to get the desired picture. */
            if( config->error || seek_mode == SEEK_MODE_AGGRESSIVE )
                goto video_fail;
            if( ++error_count > MAX_ERROR_COUNT || rap_number <= 1 )
            {
                if( seek_mode == SEEK_MODE_UNSAFE )
                    goto video_fail;
                /* Retry to decode from the same random accessible sample with error ignorance. */
                seek_mode = SEEK_MODE_AGGRESSIVE;
            }
            else
            {
                /* Retry to decode from more past random accessible sample. */
                roll_recovery = find_random_accessible_point( vdhp, sample_number, rap_number - 1, &rap_number );
                if( vdhp->last_rap_number == rap_number )
                    goto video_fail;
                vdhp->last_rap_number = rap_number;
            }
        }
        start_number = seek_video( vdhp, picture, sample_number, rap_number, roll_recovery || seek_mode != SEEK_MODE_NORMAL );
    }
    vdhp->last_sample_number = sample_number;
    config_index = config->index;
return_frame:;
    /* Don't exceed the maximum presentation size specified for each sequence. */
    extended_summary_t *extended = &config->entries[ config_index - 1 ].extended;
    if( config->ctx->width > extended->width )
        config->ctx->width = extended->width;
    if( config->ctx->height > extended->height )
        config->ctx->height = extended->height;
    return 0;
video_fail:
    /* fatal error of decoding */
    lw_log_show( &config->lh, LW_LOG_WARNING, "Couldn't read video frame." );
    return -1;
#undef MAX_ERROR_COUNT
}

static uint32_t libavsmash_vfr2cfr
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    uint32_t                           sample_number
)
{
    /* Convert VFR to CFR. */
    double target_pts  = (double)((uint64_t)(sample_number - 1) * vohp->cfr_den) / vohp->cfr_num;
    double current_pts = DBL_MAX;
    lsmash_sample_t sample;
    if( vdhp->last_sample_number <= vdhp->sample_count )
    {
        uint32_t last_decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, vdhp->last_sample_number );
        if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, last_decoding_sample_number, &sample ) < 0 )
            return 0;
        current_pts = (double)(sample.cts - vdhp->min_cts) / vdhp->media_timescale;
        if( target_pts == current_pts )
            return vdhp->last_sample_number;
    }
    uint32_t composition_sample_number = vdhp->last_sample_number;
    double   prev_pts = current_pts;
    if( target_pts < current_pts )
    {
        for( composition_sample_number--;
             composition_sample_number;
             composition_sample_number-- )
        {
            uint32_t decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
            if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, decoding_sample_number, &sample ) < 0 )
                return 0;
            current_pts = (double)(sample.cts - vdhp->min_cts) / vdhp->media_timescale;
            prev_pts = current_pts;
            if( current_pts <= target_pts )
                break;
        }
        if( composition_sample_number == 0 )
            return 0;
    }
    double next_target_pts = (double)((uint64_t)sample_number * vohp->cfr_den) / vohp->cfr_num;
    for( composition_sample_number++;
         composition_sample_number <= vdhp->sample_count;
         composition_sample_number++ )
    {
        uint32_t decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
        if( lsmash_get_sample_info_from_media_timeline( vdhp->root, vdhp->track_id, decoding_sample_number, &sample ) < 0 )
            return 0;
        current_pts = (double)(sample.cts - vdhp->min_cts) / vdhp->media_timescale;
        if( current_pts >= target_pts )
        {
            uint32_t prev_composition_sample_number = composition_sample_number - 1;
            if( current_pts > next_target_pts )
                /* Between the current target and the next target, there are no input samples.
                 * Therefore, output the previous sample. This is absolutely correct. */
                sample_number = prev_composition_sample_number;
            else
            {
                if( current_pts > (next_target_pts + target_pts) / 2 )
                    /* The current sample is far from the current target and should be a candidate for the next target. */
                    sample_number = prev_composition_sample_number;
                else
                {
                    /* Choose the nearest one. */
                    if( current_pts - target_pts >= target_pts - prev_pts )
                        sample_number = prev_composition_sample_number;
                    else
                        sample_number = composition_sample_number;
                }
            }
            break;
        }
        prev_pts = current_pts;
    }
    if( composition_sample_number > vdhp->sample_count )
        sample_number = vdhp->sample_count;
    return sample_number;
}

/* Return 0 if successful.
 * Return 1 if the same frame was requested at the last call.
 * Return a negative value otherwise. */
int libavsmash_video_get_frame
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    uint32_t                           sample_number
)
{
    if( vohp->vfr2cfr )
    {
        sample_number = libavsmash_vfr2cfr( vdhp, vohp, sample_number );
        if( sample_number == 0 )
            return -1;
    }
    if( sample_number == vdhp->last_sample_number )
        return 1;
    int ret;
    if( (ret = get_requested_picture( vdhp, vdhp->frame_buffer, sample_number )) < 0
     || (ret = update_scaler_configuration_if_needed( &vohp->scaler, &vdhp->config.lh, vdhp->frame_buffer )) < 0 )
        return ret;
    return 0;
}

int libavsmash_video_find_first_valid_frame
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    codec_configuration_t *config = &vdhp->config;
    for( uint32_t i = 1; i <= vdhp->sample_count + get_decoder_delay( config->ctx ); i++ )
    {
        AVPacket pkt = { 0 };
        get_sample( vdhp->root, vdhp->track_id, i, config, &pkt );
        av_frame_unref( vdhp->frame_buffer );
        int got_picture;
        if( decode_video_packet( config->ctx, vdhp->frame_buffer, &got_picture, &pkt ) >= 0 && got_picture )
        {
            vdhp->first_valid_frame_number = i - MIN( get_decoder_delay( config->ctx ), config->delay_count );
            if( vdhp->first_valid_frame_number > 1 || vdhp->sample_count == 1 )
            {
                vdhp->first_valid_frame = av_frame_clone( vdhp->frame_buffer );
                if( !vdhp->first_valid_frame )
                    return -1;
                av_frame_unref( vdhp->frame_buffer );
            }
            break;
        }
        else if( pkt.data )
            ++ config->delay_count;
    }
    return 0;
}

int libavsmash_video_create_keyframe_list
(
    libavsmash_video_decode_handler_t *vdhp
)
{
    vdhp->keyframe_list = (uint8_t *)lw_malloc_zero( (vdhp->sample_count + 1) * sizeof(uint8_t) );
    if( !vdhp->keyframe_list )
        return -1;
    for( uint32_t composition_sample_number = 1; composition_sample_number <= vdhp->sample_count; composition_sample_number++ )
    {
        uint32_t decoding_sample_number = get_decoding_sample_number( vdhp->order_converter, composition_sample_number );
        uint32_t rap_number;
        if( lsmash_get_closest_random_accessible_point_from_media_timeline( vdhp->root,
                                                                            vdhp->track_id,
                                                                            decoding_sample_number, &rap_number ) < 0 )
            continue;
        if( decoding_sample_number == rap_number )
            vdhp->keyframe_list[composition_sample_number] = 1;
    }
    return 0;
}

int libavsmash_video_is_keyframe
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    uint32_t                           sample_number
)
{
    if( vohp->vfr2cfr )
        sample_number = libavsmash_vfr2cfr( vdhp, vohp, sample_number );
    return vdhp->keyframe_list[sample_number];
}
