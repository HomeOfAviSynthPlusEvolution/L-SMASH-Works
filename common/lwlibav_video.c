/*****************************************************************************
 * lwlibav_video.c / lwlibav_video.cpp
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

#include <float.h>

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <libavformat/avformat.h>   /* Demuxer */
#include <libavcodec/avcodec.h>     /* Decoder */
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "video_output.h"
#include "lwlibav_dec.h"
#include "lwlibav_video.h"
#include "lwlibav_video_internal.h"
#include "decode.h"

#define SEEK_MODE_NORMAL     0
#define SEEK_MODE_UNSAFE     1
#define SEEK_MODE_AGGRESSIVE 2

#if LIBAVCODEC_VERSION_MICRO < 100
#define avcodec_find_best_pix_fmt_of_list( _0, _1, _2, _3 ) avcodec_find_best_pix_fmt2( (enum AVPixelFormat *)(_0), _1, _2, _3 )
#endif

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
lwlibav_video_decode_handler_t *lwlibav_video_alloc_decode_handler
(
    void
)
{
    lwlibav_video_decode_handler_t *vdhp = (lwlibav_video_decode_handler_t *)lw_malloc_zero( sizeof(lwlibav_video_decode_handler_t) );
    if( !vdhp )
        return NULL;
    vdhp->frame_buffer = av_frame_alloc();
    if( !vdhp->frame_buffer )
    {
        lwlibav_video_free_decode_handler( vdhp );
        return NULL;
    }
    return vdhp;
}

lwlibav_video_output_handler_t *lwlibav_video_alloc_output_handler
(
    void
)
{
    return (lwlibav_video_output_handler_t *)lw_malloc_zero( sizeof(lwlibav_video_output_handler_t) );
}

void lwlibav_video_free_decode_handler
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    if( !vdhp )
        return;
    lwlibav_extradata_handler_t *exhp = &vdhp->exh;
    if( exhp->entries )
    {
        for( int i = 0; i < exhp->entry_count; i++ )
            if( exhp->entries[i].extradata )
                av_free( exhp->entries[i].extradata );
        lw_free( exhp->entries );
    }
    av_packet_unref( &vdhp->packet );
    lw_free( vdhp->frame_list );
    lw_free( vdhp->order_converter );
    lw_free( vdhp->keyframe_list );
    av_free( vdhp->index_entries );
    av_frame_free( &vdhp->frame_buffer );
    av_frame_free( &vdhp->first_valid_frame );
    av_frame_free( &vdhp->movable_frame_buffer );
    avcodec_free_context( &vdhp->ctx );
    if( vdhp->format )
        lavf_close_file( &vdhp->format );
    lw_free( vdhp );
}

void lwlibav_video_free_output_handler
(
    lwlibav_video_output_handler_t *vohp
)
{
    if( !vohp )
        return;
    lw_cleanup_video_output_handler( vohp );
    lw_free( vohp );
}

void lwlibav_video_free_decode_handler_ptr
(
    lwlibav_video_decode_handler_t **vdhpp
)
{
    if( !vdhpp || !*vdhpp )
        return;
    lwlibav_video_free_decode_handler( *vdhpp );
    *vdhpp = NULL;
}

void lwlibav_video_free_output_handler_ptr
(
    lwlibav_video_output_handler_t **vohpp
)
{
    if( !vohpp || !*vohpp )
        return;
    lwlibav_video_free_output_handler( *vohpp );
    *vohpp = NULL;
}

/*****************************************************************************
 * Setters
 *****************************************************************************/
void lwlibav_video_set_forward_seek_threshold
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        forward_seek_threshold
)
{
    vdhp->forward_seek_threshold = forward_seek_threshold;
}

void lwlibav_video_set_seek_mode
(
    lwlibav_video_decode_handler_t *vdhp,
    int                             seek_mode
)
{
    vdhp->seek_mode = seek_mode;
}

void lwlibav_video_set_preferred_decoder_names
(
    lwlibav_video_decode_handler_t *vdhp,
    const char                    **preferred_decoder_names
)
{
    vdhp->preferred_decoder_names = preferred_decoder_names;
}

void lwlibav_video_set_prefer_hw_decoder
(
    lwlibav_video_decode_handler_t *vdhp,
    int                             prefer_hw_decoder
)
{
    vdhp->prefer_hw_decoder = prefer_hw_decoder;
}

void lwlibav_video_set_log_handler
(
    lwlibav_video_decode_handler_t *vdhp,
    lw_log_handler_t               *lh
)
{
    vdhp->lh = *lh;
}

void lwlibav_video_set_get_buffer_func
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    vdhp->exh.get_buffer = vdhp->ctx->get_buffer2;
}

/*****************************************************************************
 * Getters
 *****************************************************************************/
const char **lwlibav_video_get_preferred_decoder_names
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->preferred_decoder_names : NULL;
}

int lwlibav_video_get_error
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->error : -1;
}

lw_log_handler_t *lwlibav_video_get_log_handler
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? &vdhp->lh : NULL;
}

AVCodecContext *lwlibav_video_get_codec_context
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->ctx : NULL;
}

int lwlibav_video_get_max_width
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->max_width : 0;
}

int lwlibav_video_get_max_height
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->max_height : 0;
}

AVFrame *lwlibav_video_get_frame_buffer
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    return vdhp ? vdhp->frame_buffer : NULL;
}

/*****************************************************************************
 * Others
 *****************************************************************************/
void lwlibav_video_force_seek
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    /* Force seek before the next reading. */
    vdhp->last_frame_number = vdhp->frame_count + 1;
}

int lwlibav_video_get_desired_track
(
    const char                     *file_path,
    lwlibav_video_decode_handler_t *vdhp,
    int                             threads
)
{
    AVCodecContext *ctx = NULL;
    if( vdhp->stream_index < 0
     || vdhp->frame_count == 0
     || lavf_open_file( &vdhp->format, file_path, &vdhp->lh ) < 0
     || find_and_open_decoder( &ctx, vdhp->format->streams[ vdhp->stream_index ]->codecpar,
                               vdhp->preferred_decoder_names, vdhp->prefer_hw_decoder, threads ) < 0 )
    {
        av_freep( &vdhp->index_entries );
        lw_freep( &vdhp->frame_list );
        lw_freep( &vdhp->order_converter );
        lw_freep( &vdhp->keyframe_list );
        if( vdhp->format )
            lavf_close_file( &vdhp->format );
        return -1;
    }
    vdhp->ctx = ctx;
    return 0;
}

void lwlibav_video_setup_timestamp_info
(
    lwlibav_file_handler_t         *lwhp,
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    int64_t                        *framerate_num,
    int64_t                        *framerate_den,
    int                             apply_repeat_flag
)
{
    AVStream *stream = vdhp->format->streams[ vdhp->stream_index ];
    if( vohp->vfr2cfr )
    {
        *framerate_num = (int64_t)vohp->cfr_num;
        *framerate_den = (int64_t)vohp->cfr_den;
        return;
    }
    if( vdhp->frame_count == 1
     || lwhp->raw_demuxer
     || vdhp->actual_time_base.num == 0
     || vdhp->actual_time_base.den == 0
     || ((lwhp->format_flags & AVFMT_TS_DISCONT) && !(vdhp->lw_seek_flags & SEEK_DTS_BASED))
     || !(vdhp->lw_seek_flags & (SEEK_DTS_BASED | SEEK_PTS_BASED | SEEK_PTS_GENERATED))
     || (apply_repeat_flag && ((stream->avg_frame_rate.num && stream->avg_frame_rate.den) || (stream->r_frame_rate.num && stream->r_frame_rate.den))) )
        goto use_lavf_frame_rate;
    uint64_t stream_timebase  = vdhp->actual_time_base.num;
    uint64_t stream_timescale = vdhp->actual_time_base.den;
    uint64_t reduce = reduce_fraction( &stream_timescale, &stream_timebase );
    uint64_t stream_duration = (vdhp->stream_duration * vdhp->time_base.num) / reduce;
    double stream_framerate = (vohp->frame_count - (vohp->repeat_correction_ts ? 1 : 0))
                            * ((double)stream_timescale / stream_duration);
    if( vdhp->strict_cfr || !lw_try_rational_framerate( stream_framerate, framerate_num, framerate_den, stream_timebase ) )
    {
        if( stream_timebase > INT_MAX || (uint64_t)(stream_framerate * stream_timebase + 0.5) > INT_MAX )
            goto use_lavf_frame_rate;
        uint64_t num = (uint64_t)(stream_framerate * stream_timebase + 0.5);
        uint64_t den = stream_timebase;
        if( num && den )
            reduce_fraction( &num, &den );
        else if( stream->avg_frame_rate.num == 0
              || stream->avg_frame_rate.den == 0 )
        {
            num = 1;
            den = 1;
        }
        else
            goto use_lavf_frame_rate;
        *framerate_num = (int64_t)num;
        *framerate_den = (int64_t)den;
    }
    return;
use_lavf_frame_rate:
    if( !(stream->avg_frame_rate.num && stream->avg_frame_rate.den)
     || (stream->avg_frame_rate.den != 1 && stream->avg_frame_rate.den != 1001 && (stream->r_frame_rate.den == 1 || stream->r_frame_rate.den == 1001)) )
    {
        *framerate_num = (int64_t)stream->r_frame_rate.num;
        *framerate_den = (int64_t)stream->r_frame_rate.den;
    }
    else
    {
        *framerate_num = (int64_t)stream->avg_frame_rate.num;
        *framerate_den = (int64_t)stream->avg_frame_rate.den;
    }
    return;
}

void lwlibav_video_set_initial_input_format
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    vdhp->ctx->width      = vdhp->initial_width;
    vdhp->ctx->height     = vdhp->initial_height;
    vdhp->ctx->pix_fmt    = vdhp->initial_pix_fmt;
    vdhp->ctx->colorspace = vdhp->initial_colorspace;
}

/* Set the indentifier in output order to identify output picture.
 * Currently, we do not use timestamps directly provided by the libavformat for seeking and use
 * both DTSs and PTSs for picture number correction at the input to the decoder, so use both DTS
 * as identifiers at the output. */
static inline void set_output_order_id
(
    lwlibav_video_decode_handler_t *vdhp,
    AVPacket                       *pkt,
    uint32_t                        coded_picture_number
)
{
    if( vdhp->order_converter && coded_picture_number <= vdhp->frame_count )
    {
        /* Picture reorderings are present. */
        pkt->pts = vdhp->order_converter[coded_picture_number].decoding_to_presentation;
        pkt->dts = coded_picture_number;
    }
    else if( !(vdhp->lw_seek_flags & SEEK_DTS_BASED) )
    {
        /* Duplicated or invalid DTSs are present, or no reliable timestamps to identify output pictures. */
        pkt->pts = AV_NOPTS_VALUE;
        pkt->dts = AV_NOPTS_VALUE;
    }
    else
    {
        /* No picture reorderings are present. */
        pkt->pts = coded_picture_number;
        pkt->dts = coded_picture_number;
    }
}

/* Get the indentifier in output order to identify output picture. */
static inline int64_t get_output_order_id
(
    AVFrame *decoded_frame_buffer
)
{
    return decoded_frame_buffer->pts;
}

static uint32_t correct_current_frame_number
(
    lwlibav_video_decode_handler_t *vdhp,
    AVPacket                       *pkt,
    uint32_t                        i,      /* picture_number */
    uint32_t                        goal
)
{
#define MATCH_DTS( j ) (info[j].dts == pkt->dts)
#define MATCH_POS( j ) ((vdhp->lw_seek_flags & SEEK_POS_CORRECTION) && info[j].file_offset == pkt->pos)
    order_converter_t  *oc   = vdhp->order_converter;
    video_frame_info_t *info = vdhp->frame_list;
    uint32_t p = oc ? oc[i].decoding_to_presentation : i;
    if( pkt->dts == AV_NOPTS_VALUE || MATCH_DTS( p ) || MATCH_POS( p ) )
        return i;
    if( pkt->dts > info[p].dts )
    {
        /* too forward */
        uint32_t limit = MIN( goal, vdhp->frame_count );
        if( oc )
            while( !MATCH_DTS( oc[++i].decoding_to_presentation )
                && !MATCH_POS( oc[  i].decoding_to_presentation )
                && i <= limit );
        else
            while( !MATCH_DTS( ++i )
                && !MATCH_POS(   i )
                && i <= limit );
        if( i > limit )
            return 0;
    }
    else
    {
        /* too backward */
        if( oc )
            while( !MATCH_DTS( oc[--i].decoding_to_presentation )
                && !MATCH_POS( oc[  i].decoding_to_presentation )
                && i );
        else
            while( !MATCH_DTS( --i )
                && !MATCH_POS(   i )
                && i );
        if( i == 0 )
            return 0;
    }
    return i;
#undef MATCH_DTS
#undef MATCH_POS
}

static int decode_video_picture
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,
    int                            *got_picture,
    int64_t                        *pkt_pts,
    uint32_t                       *current,
    uint32_t                        goal,
    uint32_t                        rap_number
)
{
    /* Get a packet containing a frame. */
    uint32_t picture_number = *current;
    AVPacket *pkt = &vdhp->packet;
    int ret = lwlibav_get_av_frame( vdhp->format, vdhp->stream_index, picture_number, pkt );
    if( ret > 0 )
        return ret;
    /* Correct the current picture number in order to match DTS since libavformat might have sought wrong position. */
    uint32_t correction_distance = 0;
    if( picture_number == rap_number && (vdhp->lw_seek_flags & SEEK_DTS_BASED) )
    {
        picture_number = correct_current_frame_number( vdhp, pkt, picture_number, goal );
        if( picture_number == 0
         || picture_number > rap_number )
            return -2;
        if( *current > picture_number )
            /* It seems we got a more backward frame rather than what we requested. */
            correction_distance = *current - picture_number;
        *current = picture_number;
    }
    if( pkt->flags & AV_PKT_FLAG_KEY )
        vdhp->last_rap_number = picture_number;
    /* Avoid decoding frames until the seek correction caused by too backward is done. */
    while( correction_distance )
    {
        ret = lwlibav_get_av_frame( vdhp->format, vdhp->stream_index, ++picture_number, pkt );
        if( ret > 0 )
            return ret;
        if( pkt->flags & AV_PKT_FLAG_KEY )
            vdhp->last_rap_number = picture_number;
        *current = picture_number;
        --correction_distance;
    }
    /* Decode a frame in a packet. */
    AVFrame *mov_frame = vdhp->movable_frame_buffer;
    av_frame_unref( mov_frame );
    set_output_order_id( vdhp, pkt, picture_number );
    ret = decode_video_packet( vdhp->ctx, mov_frame, got_picture, pkt );
    vdhp->last_fed_picture_number = picture_number;
    /* We can't get the requested frame by feeding a picture if that picture is field coded.
     * This branch avoids putting empty data on the frame buffer. */
    if( *got_picture )
    {
        av_frame_unref( frame );
        av_frame_move_ref( frame, mov_frame );
        vdhp->last_dec_frame = frame;
    }
    *pkt_pts = pkt->pts;
    if( ret < 0 )
    {
        lw_log_show( &vdhp->lh, LW_LOG_ERROR, "Failed to decode a video frame." );
        return -1;
    }
    return 0;
}

static void find_random_accessible_point
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        presentation_picture_number,
    uint32_t                        decoding_picture_number,
    uint32_t                       *rap_number
)
{
    int is_leading = !!(vdhp->frame_list[presentation_picture_number].flags & LW_VFRAME_FLAG_LEADING);
    if( decoding_picture_number == 0 )
        decoding_picture_number = vdhp->frame_list[presentation_picture_number].sample_number;
    *rap_number = decoding_picture_number;
    while( *rap_number )
    {
        if( vdhp->keyframe_list[ *rap_number ] )
        {
            if( !is_leading )
                break;
            /* Shall be decoded from more past random access point. */
            is_leading = 0;
        }
        --(*rap_number);
    }
    if( *rap_number == 0 )
        *rap_number = 1;
}

static int64_t get_random_accessible_point_position
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        rap_number
)
{
    uint32_t presentation_rap_number = vdhp->order_converter
                                     ? vdhp->order_converter[rap_number].decoding_to_presentation
                                     : rap_number;
    return (vdhp->lw_seek_flags & SEEK_POS_BASED) ? vdhp->frame_list[presentation_rap_number].file_offset
         : (vdhp->lw_seek_flags & SEEK_PTS_BASED) ? vdhp->frame_list[presentation_rap_number].pts
         : (vdhp->lw_seek_flags & SEEK_DTS_BASED) ? vdhp->frame_list[presentation_rap_number].dts
         :                                          vdhp->frame_list[presentation_rap_number].sample_number;
}

static inline uint32_t is_half_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        output_picture_number
)
{
    return (output_picture_number <= vdhp->frame_count
         && vdhp->frame_list[output_picture_number].repeat_pict == 0);
}

static void correct_output_delay
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                       *goal_fed_picture_number,
    uint32_t                        reliable_picture_number,
    uint32_t                        estimated_picture_number
)
{
    if( reliable_picture_number != estimated_picture_number )
    {
        /* If positive, we got a frame earlier than expected.
         * Otherwise, we got a frame later than expected and it's an error. */
        int64_t diff = reliable_picture_number - estimated_picture_number;
        vdhp->exh.delay_count    = (uint32_t)(vdhp->exh.delay_count    - diff);
        *goal_fed_picture_number = (uint32_t)(*goal_fed_picture_number - diff);
    }
}

static uint32_t seek_video
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,
    uint32_t                        presentation_picture_number,
    uint32_t                        rap_number,
    int64_t                         rap_pos,
    int                             error_ignorance
)
{
    /* Prepare to decode from random accessible picture. */
    lwlibav_extradata_handler_t *exhp = &vdhp->exh;
    int extradata_index = vdhp->frame_list[rap_number].extradata_index;
    if( extradata_index != exhp->current_index )
        /* Update the decoder configuration. */
        lwlibav_update_configuration( (lwlibav_decode_handler_t *)vdhp, rap_number, extradata_index, rap_pos );
    else
        lwlibav_flush_buffers( (lwlibav_decode_handler_t *)vdhp );
    if( vdhp->error )
        return 0;
    if( av_seek_frame( vdhp->format, vdhp->stream_index, rap_pos, vdhp->av_seek_flags ) < 0 )
        av_seek_frame( vdhp->format, vdhp->stream_index, rap_pos, vdhp->av_seek_flags | AVSEEK_FLAG_ANY );
    int      got_picture  = 0;
    int      output_ready = 0;
    int64_t  rap_pts = AV_NOPTS_VALUE;
    uint32_t current;
    uint32_t decoder_delay = get_decoder_delay( vdhp->ctx );
    uint32_t thread_delay  = decoder_delay - vdhp->ctx->has_b_frames;
    uint32_t goal = presentation_picture_number + decoder_delay;
    exhp->delay_count     = 0;
    vdhp->last_half_frame = 0;
    for( current = rap_number; current <= goal; current++ )
    {
        int64_t pkt_pts;
        int ret = decode_video_picture( vdhp, frame, &got_picture, &pkt_pts, &current, goal, rap_number );
        if( ret == -2 )
            return 0;
        else if( ret >= 1 )
            /* No decoding occurs. */
            break;
        if( got_picture )
        {
            exhp->delay_count = MIN( decoder_delay, current - rap_number );
            uint32_t picture_number;
            int64_t output_id = get_output_order_id( frame );
            if( output_id != AV_NOPTS_VALUE )
            {
                picture_number = (uint32_t)output_id;
                uint32_t estimated_picture_number = current - exhp->delay_count;
                correct_output_delay( vdhp, &goal, picture_number, estimated_picture_number );
                if( picture_number == presentation_picture_number )
                {
                    /* Got the desired output frame now.
                     * Shorten the decoder delay if we got a frame earlier than expected. */;
                    vdhp->last_half_frame = is_half_frame( vdhp, picture_number );
                    return current + 1;
                }
                decoder_delay = exhp->delay_count;
            }
            else
            {
                picture_number = current - exhp->delay_count;
                if( decoder_delay > exhp->delay_count )
                {
                    /* Shorten the distance to the goal if we got a frame earlier than expected. */
                    uint32_t new_decoder_delay = exhp->delay_count;
                    goal -= decoder_delay - new_decoder_delay;
                    decoder_delay = new_decoder_delay;
                }
            }
            vdhp->last_half_frame = is_half_frame( vdhp, picture_number );
            output_ready = 1;
        }
        /* Handle decoder delay derived from PAFF field coded pictures. */
        else if( current <= vdhp->frame_count
              && current >= rap_number + decoder_delay
              && vdhp->frame_list[current].repeat_pict == 0 )
        {
            /* No output frame since the second field coded picture of the next frame is not decoded yet. */
            if( decoder_delay - thread_delay < 2 * vdhp->ctx->has_b_frames + 1UL )
            {
                uint32_t new_decoder_delay = thread_delay + 2 * vdhp->ctx->has_b_frames + 1UL;
                goal += new_decoder_delay - decoder_delay;
                decoder_delay = new_decoder_delay;
            }
        }
        else if( output_ready )
        {
            /* More input pictures are required to output and the goal become more distant. */
            ++decoder_delay;
            ++goal;
            uint32_t picture_number = current - exhp->delay_count;
            vdhp->last_half_frame = is_half_frame( vdhp, picture_number );
        }
        /* Some decoders return -1 when feeding a leading pictures.
         * We don't consider as an error if the return value -1 is caused by a leading picture since it's not fatal at all. */
        if( current == vdhp->last_rap_number && pkt_pts != AV_NOPTS_VALUE )
            rap_pts = pkt_pts;
        if( ret == -1 && (pkt_pts == AV_NOPTS_VALUE || pkt_pts >= rap_pts) && !error_ignorance )
        {
            lw_log_show( &vdhp->lh, LW_LOG_ERROR, "Failed to decode a video frame." );
            return 0;
        }
    }
    exhp->delay_count = MIN( decoder_delay, current - rap_number );
    if( current <= rap_number )
        vdhp->last_half_frame = 0;
    return current;
}

static inline int copy_last_req_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame
)
{
    /* Copy the last decoded and requested frame data. */
    assert( vdhp->last_req_frame );
    if( frame == vdhp->last_req_frame )
        return 0;
    av_frame_unref( frame );
    return av_frame_ref( frame, vdhp->last_req_frame );
}

/* Answer whether field of picture in the frame buffer is the first or the second. */
static inline int field_number_of_picture_in_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,
    uint32_t                        output_picture_number
)
{
    if( frame->top_field_first )
        return vdhp->frame_list[output_picture_number].field_info == LW_FIELD_INFO_TOP    ? 1
             : vdhp->frame_list[output_picture_number].field_info == LW_FIELD_INFO_BOTTOM ? 2
             :                                                                              0;
    else
        return vdhp->frame_list[output_picture_number].field_info == LW_FIELD_INFO_TOP    ? 2
             : vdhp->frame_list[output_picture_number].field_info == LW_FIELD_INFO_BOTTOM ? 1
             :                                                                              0;
}

static int is_picture_stored_in_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,
    uint32_t                        picture_number
)
{
    if( !frame )
        return -1;
    int64_t output_id = get_output_order_id( frame );
    if( output_id != AV_NOPTS_VALUE )
    {
        uint32_t reliable_picture_number = (uint32_t)output_id;
        if( picture_number == reliable_picture_number )
            return 1;
        else if( is_half_frame( vdhp, reliable_picture_number ) )
        {
            int field_number = field_number_of_picture_in_frame( vdhp, frame, reliable_picture_number );
            if( (field_number == 1 && picture_number == reliable_picture_number + 1)
             || (field_number == 2 && picture_number == reliable_picture_number - 1) )
                return 1;
        }
        return 0;
    }
    return -1;
}

static int get_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,  /* requesting frame buffer */
    uint32_t                        current,
    uint32_t                        requested_picture_number,
    uint32_t                        rap_number
)
{
#define REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER 0
    uint32_t goal = requested_picture_number + vdhp->exh.delay_count;
    int got_picture = (current > goal);
    if( got_picture )
    {
        /* The last decoded and output frame is the requested frame. */
        if( requested_picture_number == vdhp->last_frame_number )
            return copy_last_req_frame( vdhp, frame );
        else
            return REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER;
    }
    while( current <= goal )
    {
        int64_t pkt_pts;    /* unused */
        int ret = decode_video_picture( vdhp, frame, &got_picture, &pkt_pts, &current, goal, rap_number );
        if( ret < 0 )
            return -1;
        else if( ret == 1 )
            /* No more frames. */
            break;
        uint32_t estimated_picture_number = current - vdhp->exh.delay_count;
        if( got_picture )
        {
            /* The decoder output a frame. */
            int64_t output_id = get_output_order_id( frame );
            if( output_id != AV_NOPTS_VALUE )
            {
                uint32_t picture_number = (uint32_t)output_id;
                vdhp->last_half_frame = is_half_frame( vdhp, picture_number );
                correct_output_delay( vdhp, &goal, picture_number, estimated_picture_number );
                if( picture_number == requested_picture_number )
                    /* Got the requested output frame. */
                    return 0;
                else if( vdhp->last_half_frame && (picture_number == requested_picture_number + 1)
                      && field_number_of_picture_in_frame( vdhp, frame, picture_number ) == 2 )
                    /* Got the requested output frame but the output timestamp is from one of the second displayed field. */
                    return 0;
                else if( picture_number > requested_picture_number )
                    return -1;
            }
            else
                vdhp->last_half_frame = is_half_frame( vdhp, estimated_picture_number );
        }
        else
        {
            /* The decoder did not output a frame. */
            int more_input = 0;
            vdhp->last_half_frame = is_half_frame( vdhp, estimated_picture_number );
            if( vdhp->last_half_frame )
            {
                /* field coded picture */
                if( current == goal )
                {
                    /* The last output frame by the decoder might contain the requested picture. */
                    int ret = is_picture_stored_in_frame( vdhp, vdhp->last_dec_frame, estimated_picture_number );
                    if( ret == 1 )
                        goto return_last_frame;
                    else if( ret == -1 && (estimated_picture_number == vdhp->last_frame_number + 1) )
                        return copy_last_req_frame( vdhp, frame );
                    more_input = 1;
                }
            }
            else
                /* frame coded picture */
                more_input = 1;
            if( more_input )
            {
                /* Fundamental seek operations after the decoder initialization is already done, but
                 * more input pictures are required to output and the goal becomes more distant. */
                ++ vdhp->exh.delay_count;
                ++ goal;
            }
        }
        ++current;
    }
    /* Flush the last frames. */
    if( current > vdhp->frame_count && vdhp->exh.delay_count )
        while( current <= goal )
        {
            uint32_t estimated_picture_number = current - vdhp->exh.delay_count;
            uint32_t last_half_frame = is_half_frame( vdhp, estimated_picture_number );
            int ret = is_picture_stored_in_frame( vdhp, vdhp->last_dec_frame, estimated_picture_number );
            if( ret == 1 )
            {
                if( current == goal )
                {
                    vdhp->last_half_frame = last_half_frame;
                    vdhp->last_fed_picture_number = current;
                    goto return_last_frame;
                }
            }
            else if( ret == -1 && last_half_frame )
            {
                int field_number = field_number_of_picture_in_frame( vdhp, vdhp->last_dec_frame, estimated_picture_number );
                if( (field_number == 1 && estimated_picture_number == vdhp->last_frame_number + 1)
                 || (field_number == 2 && estimated_picture_number == vdhp->last_frame_number - 1) )
                {
                    /* Apparently, the requested picture is in the last output frame by the decoder. */
                    vdhp->last_half_frame = 1;
                    vdhp->last_fed_picture_number = current;
                    if( current == goal )
                    {
                        /* The last decoded and output frame is the requested frame. */
                        if( estimated_picture_number == vdhp->last_frame_number + 1 )
                            return copy_last_req_frame( vdhp, frame );
                        else
                            return REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER;
                    }
                    ++current;
                    continue;
                }
            }
            AVPacket pkt = { 0 };
            av_frame_unref( frame );
            if( decode_video_packet( vdhp->ctx, frame, &got_picture, &pkt ) < 0 )
            {
                lw_log_show( &vdhp->lh, LW_LOG_ERROR, "Failed to decode and flush a video frame." );
                return -1;
            }
            vdhp->last_fed_picture_number = current;
            if( !got_picture )
                break;
            uint32_t picture_number;
            int64_t  output_id = get_output_order_id( frame );
            if( output_id != AV_NOPTS_VALUE )
            {
                picture_number = (uint32_t)output_id;
                vdhp->last_half_frame = is_half_frame( vdhp, picture_number );
                correct_output_delay( vdhp, &goal, picture_number, estimated_picture_number );
            }
            else
            {
                picture_number        = estimated_picture_number;
                vdhp->last_half_frame = last_half_frame;
            }
            current += (vdhp->frame_list[picture_number].flags & LW_VFRAME_FLAG_COUNTERPART_MISSING) ? 2 : 1;
        }
    return got_picture ? REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER : -1;
return_last_frame:
    if( vdhp->last_dec_frame == vdhp->last_req_frame )
        return copy_last_req_frame( vdhp, frame );
    else
        return REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER;
#undef REQUESTED_FRAME_IS_ALREADY_ON_OUTPUT_FRAME_BUFFER
}

static inline uint32_t get_last_half_offset
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    if( !vdhp->last_half_frame )
        return 0;
    int field_number = field_number_of_picture_in_frame( vdhp, vdhp->last_req_frame, vdhp->last_frame_number );
    return field_number == 1 ? 1
         : field_number == 2 ? -1
         :                     0;
}

static int get_requested_picture
(
    lwlibav_video_decode_handler_t *vdhp,
    AVFrame                        *frame,
    uint32_t                        picture_number
)
{
#define MAX_ERROR_COUNT 3   /* arbitrary */
    if( picture_number > vdhp->frame_count )
        picture_number = vdhp->frame_count;
    uint32_t extradata_index;
    uint32_t last_half_offset = get_last_half_offset( vdhp );
    if( picture_number == vdhp->last_frame_number
     || picture_number == vdhp->last_frame_number + last_half_offset )
    {
        /* The last frame is the requested frame. */
        if( copy_last_req_frame( vdhp, frame ) < 0 )
            goto video_fail;
        extradata_index = vdhp->frame_list[picture_number].extradata_index;
        goto return_frame;
    }
    if( picture_number < vdhp->first_valid_frame_number || vdhp->frame_count == 1 )
    {
        /* Copy the first valid video frame data. */
        av_frame_unref( frame );
        if( av_frame_ref( frame, vdhp->first_valid_frame ) < 0 )
            goto video_fail;
        /* Force seeking at the next access for valid video frame. */
        vdhp->last_frame_number = vdhp->frame_count + 1;
        /* Return the first valid video frame. */
        extradata_index = vdhp->frame_list[ vdhp->first_valid_frame_number ].extradata_index;
        goto return_frame;
    }
    uint32_t start_number;  /* number of picture, for normal decoding, where decoding starts excluding decoding delay */
    uint32_t rap_number;    /* number of picture, for seeking, where decoding starts excluding decoding delay */
    uint32_t last_frame_number = vdhp->last_frame_number + last_half_offset;
    int      seek_mode         = vdhp->seek_mode;
    int64_t  rap_pos           = INT64_MIN;
    if( picture_number > last_frame_number
     && picture_number <= last_frame_number + vdhp->forward_seek_threshold )
    {
        start_number = vdhp->last_fed_picture_number + 1;
        rap_number   = vdhp->last_rap_number;
    }
    else
    {
        find_random_accessible_point( vdhp, picture_number, 0, &rap_number );
        if( rap_number == vdhp->last_rap_number && picture_number > last_frame_number )
            start_number = vdhp->last_fed_picture_number + 1;
        else
        {
            /* Require starting to decode from random accessible picture. */
            rap_pos = get_random_accessible_point_position( vdhp, rap_number );
            vdhp->last_rap_number = rap_number;
            start_number = seek_video( vdhp, frame, picture_number, rap_number, rap_pos, seek_mode != SEEK_MODE_NORMAL );
        }
    }
    /* Get frame containing the requested picture. */
    int error_count = 0;
    while( start_number == 0
        || get_frame( vdhp, frame, start_number, picture_number, rap_number ) < 0 )
    {
        /* Failed to get requested picture. */
        if( vdhp->error || seek_mode == SEEK_MODE_AGGRESSIVE )
            goto video_fail;
        if( ++error_count > MAX_ERROR_COUNT || rap_number <= 1 )
        {
            if( seek_mode == SEEK_MODE_UNSAFE )
                goto video_fail;
            /* Retry to decode from the same random accessible picture with error ignorance. */
            seek_mode = SEEK_MODE_AGGRESSIVE;
        }
        else
        {
            /* Retry to decode from more past random accessible picture. */
            find_random_accessible_point( vdhp, picture_number, rap_number - 1, &rap_number );
            rap_pos = get_random_accessible_point_position( vdhp, rap_number );
            vdhp->last_rap_number = rap_number;
        }
        start_number = seek_video( vdhp, frame, picture_number, rap_number, rap_pos, seek_mode != SEEK_MODE_NORMAL );
    }
    vdhp->last_frame_number = picture_number;
    extradata_index = vdhp->frame_list[picture_number].extradata_index;
return_frame:;
    vdhp->last_req_frame = frame;
    /* Don't exceed the maximum presentation size specified for each sequence. */
    lwlibav_extradata_t *entry = &vdhp->exh.entries[extradata_index];
    if( vdhp->ctx->width > entry->width )
        vdhp->ctx->width = entry->width;
    if( vdhp->ctx->height > entry->height )
        vdhp->ctx->height = entry->height;
    /* Set the actual PTS here. */
    frame->pts = vdhp->frame_list[picture_number].pts;
    return 0;
video_fail:
    /* fatal error of decoding */
    lw_log_show( &vdhp->lh, LW_LOG_ERROR, "Couldn't get the requested video frame." );
    return -1;
#undef MAX_ERROR_COUNT
}

static inline int check_frame_buffer_identical
(
    AVFrame *a,
    AVFrame *b
)
{
    return !memcmp( a->data,     b->data,     sizeof(a->data) )
        && !memcmp( a->linesize, b->linesize, sizeof(a->linesize) );
}

static inline int copy_frame
(
    lw_log_handler_t *lhp,
    AVFrame          *dst,
    AVFrame          *src
)
{
    av_frame_unref( dst );
    if( av_frame_ref( dst, src ) < 0 )
    {
        lw_log_show( lhp, LW_LOG_ERROR, "Failed to reference a video frame.\n" );
        return -1;
    }
    return 0;
}

static inline int copy_field
(
    lw_log_handler_t *lhp,
    AVFrame          *dst,
    AVFrame          *src,
    int               line_offset,
    int               top_field_first
)
{
    /* Check if the destination is writable. */
    if( av_frame_is_writable( dst ) == 0 )
    {
        /* The destination is NOT writable, so allocate new buffers and copy the data. */
        av_frame_unref( dst );
        if( av_frame_ref( dst, src ) < 0 )
        {
            lw_log_show( lhp, LW_LOG_ERROR, "Failed to reference a video frame.\n" );
            return -1;
        }
        if( av_frame_make_writable( dst ) < 0 )
        {
            lw_log_show( lhp, LW_LOG_ERROR, "Failed to make a video frame writable.\n" );
            return -1;
        }
        /* For direct rendering, the destination can not know
         * whether the value at the address held by the opaque pointer is valid or not.
         * Anyway, the opaque pointer for direct rendering shall be set to NULL. */
        dst->opaque = NULL;
    }
    else
    {
        /* The destination is writable. Copy field data from the source. */
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get( (enum AVPixelFormat)dst->format );
        int number_of_planes = av_pix_fmt_count_planes( (enum AVPixelFormat)dst->format );
        int height           = MIN( dst->height, src->height );
        for( int i = 0; i < number_of_planes; i++ )
        {
            int r_shift = 1 + ((i == 1 || i == 2) ? desc->log2_chroma_h : 0);
            int field_height = (height >> r_shift) + (line_offset == 0 && (height & 1) ? 1 : 0);
            av_image_copy_plane( dst->data[i] + dst->linesize[i] * line_offset, 2 * dst->linesize[i],
                                 src->data[i] + src->linesize[i] * line_offset, 2 * src->linesize[i],
                                 MIN( dst->linesize[i], src->linesize[i] ),
                                 field_height );
        }
    }
    /* Treat this frame as interlaced. */
    dst->interlaced_frame = 1;
    dst->top_field_first  = top_field_first;
    return 0;
}

static int lwlibav_repeat_control
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
)
{
    /* Get picture to applied the repeat control. */
    uint32_t t = vohp->frame_order_list[frame_number].top;
    uint32_t b = vohp->frame_order_list[frame_number].bottom;
    uint32_t first_field_number  = MIN( t, b );
    uint32_t second_field_number = MAX( t, b );
    /* Check repeat targets and cache datas. */
    enum
    {
        REPEAT_CONTROL_COPIED_FROM_CACHE   = 0x00,
        REPEAT_CONTROL_DECODE_TOP_FIELD    = 0x01,
        REPEAT_CONTROL_DECODE_BOTTOM_FIELD = 0x02,
        REPEAT_CONTROL_DECODE_BOTH_FIELDS  = 0x03,  /* REPEAT_CONTROL_DECODE_TOP_FIELD | REPEAT_CONTROL_DECODE_BOTTOM_FIELD */
        REPEAT_CONTROL_DECODE_ONE_FRAME    = 0x04
    };
    int repeat_control;
    int top_field_first = -1;
    if( first_field_number == second_field_number )
    {
        repeat_control = REPEAT_CONTROL_DECODE_ONE_FRAME;
        if( first_field_number == vohp->frame_cache_numbers[0] )
            return copy_frame( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[0] );
        if( first_field_number == vohp->frame_cache_numbers[1] )
            return copy_frame( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[1] );
        if( first_field_number != vohp->frame_order_list[frame_number - 1].top
         && first_field_number != vohp->frame_order_list[frame_number - 1].bottom
         && first_field_number != vohp->frame_order_list[frame_number + 1].top
         && first_field_number != vohp->frame_order_list[frame_number + 1].bottom )
        {
            if( get_requested_picture( vdhp, vdhp->frame_buffer, first_field_number ) < 0 )
                return -1;
            /* Treat this frame as interlaced. */
            vdhp->frame_buffer->interlaced_frame = 1;
            return 0;
        }
    }
    else
    {
        repeat_control = REPEAT_CONTROL_DECODE_BOTH_FIELDS;
        top_field_first = t < b;
        for( int i = 0; i < REPEAT_CONTROL_CACHE_NUM; i++ )
        {
            if( t == vohp->frame_cache_numbers[i] )
            {
                if( copy_field( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[i], 0, top_field_first ) < 0 )
                    return -1;
                repeat_control &= ~REPEAT_CONTROL_DECODE_TOP_FIELD;
            }
            if( b == vohp->frame_cache_numbers[i] )
            {
                if( copy_field( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[i], 1, top_field_first ) < 0 )
                    return -1;
                repeat_control &= ~REPEAT_CONTROL_DECODE_BOTTOM_FIELD;
            }
        }
        if( repeat_control == REPEAT_CONTROL_COPIED_FROM_CACHE )
            return 0;
    }
    /* Decode target frames and copy to output buffer. */
    if( repeat_control == REPEAT_CONTROL_DECODE_BOTH_FIELDS )
    {
        /* Decode 2 frames, and copy each a top and bottom fields. */
        if( get_requested_picture( vdhp, vohp->frame_cache_buffers[0], first_field_number ) < 0 )
            return -1;
        vohp->frame_cache_numbers[0] = first_field_number;
        if( get_requested_picture( vdhp, vohp->frame_cache_buffers[1], second_field_number ) < 0 )
            return -1;
        vohp->frame_cache_numbers[1] = second_field_number;
        if( check_frame_buffer_identical( vohp->frame_cache_buffers[0], vohp->frame_cache_buffers[1] ) )
            return copy_frame( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[0] );
        if( copy_field( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[0], t > b ? 1 : 0, top_field_first ) < 0
         || copy_field( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[1], t < b ? 1 : 0, top_field_first ) < 0 )
            return -1;
        return 0;
    }
    else
    {
        /* Decode 1 frame, and copy 1 frame or 1 field. */
        int decode_number = repeat_control == REPEAT_CONTROL_DECODE_ONE_FRAME ? first_field_number
                          : repeat_control == REPEAT_CONTROL_DECODE_TOP_FIELD ? t : b;
        int idx = vohp->frame_cache_numbers[0] > vohp->frame_cache_numbers[1] ? 1 : 0;
        if( get_requested_picture( vdhp, vohp->frame_cache_buffers[idx], decode_number ) < 0 )
            return -1;
        vohp->frame_cache_numbers[idx] = decode_number;
        if( repeat_control == REPEAT_CONTROL_DECODE_ONE_FRAME )
            return copy_frame( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[idx] );
        else
            return copy_field( &vdhp->lh, vdhp->frame_buffer, vohp->frame_cache_buffers[idx],
                               repeat_control == REPEAT_CONTROL_DECODE_TOP_FIELD ? 0 : 1, top_field_first );
    }
}

static int64_t lwlibav_get_ts
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        frame_number
)
{
    return (vdhp->lw_seek_flags & (SEEK_PTS_GENERATED | SEEK_PTS_BASED)) ? vdhp->frame_list[frame_number].pts
         : (vdhp->lw_seek_flags & SEEK_DTS_BASED)                        ? vdhp->frame_list[frame_number].dts
         :                                                                 AV_NOPTS_VALUE;
}

static uint32_t lwlibav_vfr2cfr
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
)
{
    /* Convert VFR to CFR. */
    double target_ts  = (double)((uint64_t)(frame_number - 1) * vohp->cfr_den) / vohp->cfr_num;
    double current_ts = DBL_MAX;
    AVRational time_base = vdhp->format->streams[ vdhp->stream_index ]->time_base;
    int64_t ts = lwlibav_get_ts( vdhp, vdhp->last_ts_frame_number );
    if( ts != AV_NOPTS_VALUE )
    {
        current_ts = ((double)(ts - vdhp->min_ts) * time_base.num) / time_base.den;
        if( target_ts == current_ts )
            return vdhp->last_ts_frame_number;
    }
    uint32_t composition_frame_number = vdhp->last_ts_frame_number;
    double   prev_ts = current_ts;
    if( target_ts < current_ts )
    {
        for( composition_frame_number--;
             composition_frame_number;
             composition_frame_number-- )
        {
            ts = lwlibav_get_ts( vdhp, composition_frame_number );
            if( ts != AV_NOPTS_VALUE )
            {
                current_ts = ((double)(ts - vdhp->min_ts) * time_base.num) / time_base.den;
                prev_ts = current_ts;
                if( current_ts <= target_ts )
                    break;
            }
        }
        if( composition_frame_number == 0 )
            return 0;
    }
    double next_target_ts = (double)((uint64_t)frame_number * vohp->cfr_den) / vohp->cfr_num;
    for( composition_frame_number++;
         composition_frame_number <= vdhp->frame_count;
         composition_frame_number++ )
    {
        ts = lwlibav_get_ts( vdhp, composition_frame_number );
        if( ts != AV_NOPTS_VALUE )
        {
            current_ts = ((double)(ts - vdhp->min_ts) * time_base.num) / time_base.den;
            if( current_ts >= target_ts )
            {
                uint32_t prev_composition_frame_number = composition_frame_number;
                while( lwlibav_get_ts( vdhp, --prev_composition_frame_number ) == AV_NOPTS_VALUE );
                if( prev_composition_frame_number == 0 )
                    frame_number = 1;
                else
                {
                    if( current_ts > next_target_ts )
                        /* Between the current target and the next target, there are no input frames.
                         * Therefore, output the previous frame. This is absolutely correct. */
                        frame_number = prev_composition_frame_number;
                    else
                    {
                        if( current_ts > (next_target_ts + target_ts) / 2 )
                            /* The current frame is far from the current target and should be a candidate for the next target. */
                            frame_number = prev_composition_frame_number;
                        else
                        {
                            /* Choose the nearest one. */
                            if( current_ts - target_ts >= target_ts - prev_ts )
                                frame_number = prev_composition_frame_number;
                            else
                                frame_number = composition_frame_number;
                        }
                    }
                }
                break;
            }
            prev_ts = current_ts;
        }
    }
    if( composition_frame_number > vdhp->frame_count )
        frame_number = vdhp->frame_count;
    vdhp->last_ts_frame_number = frame_number;
    return frame_number;
}

/* The pixel formats described in the index may not match pixel formats supported by the active decoder.
 * This selects the best pixel format from supported pixel formats with best effort. */
static void handle_decoder_pix_fmt
(
    AVCodecParameters *codecpar,
    const AVCodec     *codec,
    enum AVPixelFormat pix_fmt
)
{
    assert( codecpar && codec );
    if( codec->pix_fmts )
        codecpar->format = (int)avcodec_find_best_pix_fmt_of_list( codec->pix_fmts, pix_fmt, 1, NULL );
    else
        codecpar->format = (int)pix_fmt;
}

static int get_video_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
)
{
    if( vohp->repeat_control )
        return lwlibav_repeat_control( vdhp, vohp, frame_number );
    if( frame_number == vdhp->last_frame_number )
        return 1;
    return get_requested_picture( vdhp, vdhp->frame_buffer, frame_number );
}

/* Return 0 if successful.
 * Return 1 if the same frame was requested at the last call.
 * Return a negative value otherwise. */
int lwlibav_video_get_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
)
{
    if( vohp->vfr2cfr )
    {
        frame_number = lwlibav_vfr2cfr( vdhp, vohp, frame_number );
        if( frame_number == 0 )
            return -1;
    }
    int ret;
    if( (ret = get_video_frame( vdhp, vohp, frame_number )) != 0
     || (ret = update_scaler_configuration_if_needed( &vohp->scaler, &vdhp->lh, vdhp->frame_buffer )) < 0 )
        return ret;
    return 0;
}

int lwlibav_video_is_keyframe
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
)
{
    assert( frame_number );
    if( vohp->vfr2cfr )
        frame_number = lwlibav_vfr2cfr( vdhp, vohp, frame_number );
    if( vohp->repeat_control )
    {
        lw_video_frame_order_t *curr = &vohp->frame_order_list[frame_number    ];
        lw_video_frame_order_t *prev = &vohp->frame_order_list[frame_number - 1];
        return ((vdhp->frame_list[ curr->top    ].flags & LW_VFRAME_FLAG_KEY) && curr->top    != prev->top && curr->top    != prev->bottom)
            || ((vdhp->frame_list[ curr->bottom ].flags & LW_VFRAME_FLAG_KEY) && curr->bottom != prev->top && curr->bottom != prev->bottom);
    }
    return !!(vdhp->frame_list[frame_number].flags & LW_VFRAME_FLAG_KEY);
}

int lwlibav_video_find_first_valid_frame
(
    lwlibav_video_decode_handler_t *vdhp
)
{
    vdhp->movable_frame_buffer = av_frame_alloc();
    if( !vdhp->movable_frame_buffer )
        return -1;
    const AVCodec       *codec    = vdhp->ctx->codec;
    AVCodecParameters   *codecpar = vdhp->format->streams[ vdhp->stream_index ]->codecpar;
    handle_decoder_pix_fmt( codecpar, codec, (enum AVPixelFormat)codecpar->format );
    vdhp->ctx->pix_fmt = (enum AVPixelFormat)codecpar->format;  /* Correct decoder pixel format. */
    vdhp->last_ts_frame_number = vdhp->frame_count;
    vdhp->av_seek_flags = (vdhp->lw_seek_flags & SEEK_POS_BASED) ? AVSEEK_FLAG_BYTE
                        : vdhp->lw_seek_flags == 0               ? AVSEEK_FLAG_FRAME
                        : 0;
    if( vdhp->frame_count != 1 )
    {
        vdhp->av_seek_flags |= AVSEEK_FLAG_BACKWARD;
        uint32_t rap_number;
        find_random_accessible_point( vdhp, 1, 0, &rap_number );
        int64_t rap_pos = get_random_accessible_point_position( vdhp, rap_number );
        if( av_seek_frame( vdhp->format, vdhp->stream_index, rap_pos, vdhp->av_seek_flags ) < 0 )
            av_seek_frame( vdhp->format, vdhp->stream_index, rap_pos, vdhp->av_seek_flags | AVSEEK_FLAG_ANY );
    }
    uint32_t decoder_delay = get_decoder_delay( vdhp->ctx );
    uint32_t thread_delay  = decoder_delay - vdhp->ctx->has_b_frames;
    AVPacket *pkt = &vdhp->packet;
    for( uint32_t i = 1; i <= vdhp->frame_count + vdhp->exh.delay_count; i++ )
    {
        lwlibav_get_av_frame( vdhp->format, vdhp->stream_index, i, pkt );
        av_frame_unref( vdhp->frame_buffer );
        set_output_order_id( vdhp, pkt, i );
        int got_picture;
        int ret = decode_video_packet( vdhp->ctx, vdhp->frame_buffer, &got_picture, pkt );
        /* Handle decoder delay derived from PAFF field coded pictures. */
        if( i <= vdhp->frame_count && i > decoder_delay
         && !got_picture && vdhp->frame_list[i].repeat_pict == 0 )
        {
            /* No output picture since the second field coded picture of the next frame is not decoded yet. */
            if( decoder_delay - thread_delay < 2 * vdhp->ctx->has_b_frames + 1UL )
                decoder_delay = thread_delay + 2 * vdhp->ctx->has_b_frames + 1UL;
        }
        if( ret >= 0 )
        {
            if( got_picture )
            {
                /* Found the first valid video frame. */
                int64_t output_id = get_output_order_id( vdhp->frame_buffer );
                if( output_id != AV_NOPTS_VALUE )
                    vdhp->first_valid_frame_number = (uint32_t)output_id;
                else
                    vdhp->first_valid_frame_number = i - MIN( decoder_delay, vdhp->exh.delay_count );
                if( vdhp->first_valid_frame_number > 1 || vdhp->frame_count == 1 )
                {
                    vdhp->first_valid_frame = av_frame_clone( vdhp->frame_buffer );
                    if( !vdhp->first_valid_frame )
                        return -1;
                    av_frame_unref( vdhp->frame_buffer );
                    vdhp->first_valid_frame->pts = vdhp->frame_list[ vdhp->first_valid_frame_number ].pts;
                }
                break;
            }
            else if( pkt->data )
                /* Output is delayed. */
                ++ vdhp->exh.delay_count;
            else
                /* No more output.
                 * Failed to find the first valid video frame. */
                return -1;
        }
    }
    return 0;
}

enum lw_field_info lwlibav_video_get_field_info
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        frame_number
)
{
    return frame_number <= vdhp->frame_count
         ? vdhp->frame_list[frame_number].field_info
         : LW_FIELD_INFO_UNKNOWN;
}

void set_video_basic_settings
(
    lwlibav_decode_handler_t *dhp,
    const AVCodec            *codec,
    uint32_t                  frame_number
)
{
    lwlibav_video_decode_handler_t *vdhp = (lwlibav_video_decode_handler_t *)dhp;
    AVCodecParameters   *codecpar = vdhp->format->streams[ vdhp->stream_index ]->codecpar;
    lwlibav_extradata_t *entry    = &vdhp->exh.entries[ vdhp->frame_list[frame_number].extradata_index ];
    codecpar->width                 = entry->width;
    codecpar->height                = entry->height;
    codecpar->bits_per_coded_sample = entry->bits_per_sample;
    handle_decoder_pix_fmt( codecpar, codec, entry->pixel_format );
}

int try_decode_video_frame
(
    lwlibav_decode_handler_t *dhp,
    uint32_t                  frame_number,
    int64_t                   rap_pos,
    char                     *error_string
)
{
    AVFrame *picture = av_frame_alloc();
    if( !picture )
    {
        strcpy( error_string, "Failed to alloc AVFrame to set up a decoder configuration.\n" );
        return -1;
    }
    lwlibav_video_decode_handler_t *vdhp = (lwlibav_video_decode_handler_t *)dhp;
    AVFormatContext *format_ctx   = vdhp->format;
    int              stream_index = vdhp->stream_index;
    AVCodecContext  *ctx          = vdhp->ctx;
    if( av_seek_frame( format_ctx, stream_index, rap_pos, vdhp->av_seek_flags ) < 0 )
        av_seek_frame( format_ctx, stream_index, rap_pos, vdhp->av_seek_flags | AVSEEK_FLAG_ANY );
    do
    {
        if( frame_number > vdhp->frame_count )
            break;
        /* Get a frame. */
        AVPacket pkt = { 0 };
        int extradata_index = vdhp->frame_list[frame_number].extradata_index;
        if( extradata_index != vdhp->exh.current_index )
            break;
        int ret = lwlibav_get_av_frame( format_ctx, stream_index, frame_number, &pkt );
        if( ret > 0 )
            break;
        else if( ret < 0 )
        {
            if( ctx->pix_fmt == AV_PIX_FMT_NONE )
                strcpy( error_string, "Failed to set up pixel format.\n" );
            else
                strcpy( error_string, "Failed to set up resolution.\n" );
            av_frame_free( &picture );
            return -1;
        }
        /* Try decode a frame. */
        av_frame_unref( picture );
        int dummy;
        decode_video_packet( ctx, picture, &dummy, &pkt );
        ++frame_number;
    } while( ctx->width == 0 || ctx->height == 0 || ctx->pix_fmt == AV_PIX_FMT_NONE );
    av_frame_free( &picture );
    return 0;
}
