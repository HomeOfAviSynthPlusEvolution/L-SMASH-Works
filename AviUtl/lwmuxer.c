/*****************************************************************************
 * lwmuxer.c
 *****************************************************************************
 * Copyright (C) 2011-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Hiroki Taniura <boiled.sugar@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <windows.h>
#include <ctype.h>

/* L-SMASH */
#include <lsmash.h>

#include <libavutil/mathematics.h>

#include "filter.h"

#include "config.h"
#include "resource.h"
#include "progress_dlg.h"

#include "../common/utils.h"

/* chapter handling */
#define CHAPTER_BUFSIZE 512
#define UTF8_BOM "\xEF\xBB\xBF"
#define UTF8_BOM_LENGTH 3

/* Macros for debug */
#define MESSAGE_BOX_DESKTOP( uType, ... ) \
do \
{ \
    char temp[512]; \
    wsprintf( temp, __VA_ARGS__ ); \
    MessageBox( HWND_DESKTOP, temp, "lwmuxer", uType ); \
} while( 0 )
#ifdef DEBUG
#define DEBUG_MESSAGE_BOX_DESKTOP( uType, ... ) \
do \
{ \
    char temp[512]; \
    wsprintf( temp, __VA_ARGS__ ); \
    MessageBox( HWND_DESKTOP, temp, "lwmuxer", uType ); \
} while( 0 )
#else
#define DEBUG_MESSAGE_BOX_DESKTOP( uType, ... )
#endif

/* File filter */
#define MPEG4_FILE_EXT "All Support Formats (*.*)\0*.mp4;*.mov;*.3gp;*.3g2\0" \
                       "MP4 file (*.mp4)\0*.mp4\0" \
                       "M4V file (*.m4v)\0*.m4v\0" \
                       "MOV file (*.mov)\0*.mov\0" \
                       "3GPP file (*.3gp)\0*.3gp\0" \
                       "3GPP2 file (*.3g2)\0*.3g2\0"

FILTER_DLL filter =
{
    FILTER_FLAG_EXPORT|FILTER_FLAG_EX_DATA|FILTER_FLAG_NO_CONFIG|FILTER_FLAG_ALWAYS_ACTIVE|FILTER_FLAG_PRIORITY_LOWEST|FILTER_FLAG_EX_INFORMATION,
    0,0,                                                /* Size of configuration window */
    "L-SMASH Works Muxer",                              /* Name of filter plugin */
    0,                                                  /* Number of trackbars */
    NULL,                                               /* Pointer to group of names of trackbar */
    NULL,                                               /* Pointer to group of initial values of trackbar */
    NULL,                                               /* Minimum of trackbar */
    NULL,                                               /* Maximum of trackbar */
    0,                                                  /* Number of checkboxes */
    NULL,                                               /* Pointer to group of names of checkbox */
    NULL,                                               /* Pointer to group of initial values of checkbox */
    NULL,                                               /* Pointer to filter process function (If NULL, won't be called.) */
    NULL,                                               /* Pointer to function called when beginning (If NULL, won't be called.) */
    func_exit,                                          /* Pointer to function called when ending (If NULL, won't be called.) */
    NULL,                                               /* Pointer to function called when its configuration is updated (If NULL, won't be called.) */
    func_WndProc,                                       /* Pointer to function called when window message comes on configuration window (If NULL, won't be called.) */
    NULL,                                               /* Pointer to group of set points of trackbar */
    NULL,                                               /* Pointer to group of set points of checkbox */
    NULL,                                               /* Pointer to extended data region (Valid only if FILTER_FLAG_EX_DATA is enabled.) */
    0,                                                  /* Size of extended data (Valid only if FILTER_FLAG_EX_DATA is enabled.) */
    "L-SMASH Works Muxer r" LSMASHWORKS_REV "\0",       /* Information of filter plugin */
};

EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable( void )
{
    return &filter;
}

EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTableYUY2( void )
{
    return &filter;
}

#define VIDEO_TRACK 0
#define AUDIO_TRACK 1

typedef struct
{
    void   *editp;
    FILTER *fp;
} muxer_t;

typedef struct
{
    int  optimize_pd;
    int  import_chapter;
    char chapter_file[MAX_PATH];
} option_t;

typedef struct
{
    int               active;
    uint32_t          out_mapped_index;
    lsmash_summary_t *summary;
} input_summary_t;

typedef struct
{
    int                       active;
    uint32_t                  track_ID;
    uint32_t                  number_of_summaries;
    uint32_t                  number_of_samples;
    uint32_t                  last_sample_delta;
    uint32_t                  timescale_integrator;
    uint32_t                  ctd_shift;
    uint64_t                  empty_duration;
    uint64_t                  skip_duration;
    double                    dts;
    lsmash_sample_t          *sample;
    input_summary_t          *summaries;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} input_track_t;

typedef struct
{
    uint32_t composition_to_decoding;
} order_converter_t;

typedef struct
{
    int                       file_id;
    FILE_INFO                 fi;
    lsmash_root_t            *root;
    input_track_t             track[2];
    lsmash_file_parameters_t  file_param;
    lsmash_movie_parameters_t movie_param;
    order_converter_t        *order_converter;
} input_movie_t;

typedef struct
{
    input_movie_t     *input;
    order_converter_t *order_converter;
    uint32_t number;
    uint32_t number_of_samples;
    /* input side */
    uint32_t current_sample_number;
    uint32_t presentation_start_sample_number;      /* stored in ascending order of decoding */
    uint32_t presentation_end_sample_number;        /* stored in ascending order of decoding */
    uint32_t media_start_sample_number;             /* stored in ascending order of decoding */
    uint32_t media_end_sample_number;               /* stored in ascending order of decoding */
    uint32_t presentation_end_next_sample_number;   /* stored in ascending order of decoding
                                                     * If the value is 0, the presentation ends at the last sample in the media. */
    uint64_t composition_delay;
    uint64_t smallest_cts;
    /* output side */
    uint64_t presentation_start_time;
    uint64_t presentation_end_time;
    uint64_t start_skip_duration;                   /* start duration skipped by edit list */
    uint64_t end_skip_duration;                     /* end duration skipped by edit list */
    uint64_t empty_duration;
    /* */
    uint32_t last_sample_delta;
} sequence_t;

typedef struct
{
    int                       active;
    uint32_t                  track_ID;
    uint32_t                  media_timescale;
    uint64_t                  edit_offset;
    uint64_t                  largest_cts;
    uint64_t                  second_largest_cts;
    lsmash_track_parameters_t track_param;
    lsmash_media_parameters_t media_param;
} output_track_t;

typedef struct
{
    lsmash_root_t           *root;
    output_track_t           track[2];
    lsmash_file_parameters_t file_param;
    uint32_t                 number_of_tracks;
    double                   largest_dts;
} output_movie_t;

typedef struct
{
    input_movie_t *input;
    uint32_t       sequence_number;
    uint32_t       sample_number;
} aviutl_sample_t;

typedef struct
{
    input_movie_t   **input;
    output_movie_t   *output;
    sequence_t       *sequence[2];
    uint64_t          number_of_samples[2];
    uint32_t          number_of_sequences;
    uint32_t          number_of_inputs;
    uint32_t          composition_sample_delay; /* for video track */
    uint64_t          composition_delay_time;   /* for video track */
    uint64_t         *prev_reordered_cts;       /* for video track */
    int               with_video;
    int               with_audio;
    int               ref_chap_available;
    int               av_sync;
    FILE             *log_file;
} lsmash_handler_t;

static FILE *open_settings( void )
{
    FILE *ini = NULL;
    for( int i = 0; i < 2; i++ )
    {
        static const char *settings_path_list[2] = { "lsmash.ini", "plugins/lsmash.ini" };
        ini = fopen( settings_path_list[i], "rb" );
        if( ini )
            return ini;
    }
    return NULL;
}

static int get_settings( lsmash_handler_t *hp )
{
    FILE *ini = open_settings();
    if( !ini )
    {
        hp->av_sync = 1;
        return 0;
    }
    char buf[128];
    while( fgets( buf, sizeof(buf), ini ) )
    {
        if( sscanf( buf, "av_sync=%d", &hp->av_sync ) == 1 )
            break;
    }
    int ret = 0;
    while( fgets( buf, sizeof(buf), ini ) )
    {
        int active;
        if( sscanf( buf, "vfr2cfr=%d", &active ) == 1 )
        {
            if( active )
            {
                MessageBox( HWND_DESKTOP, "Unavailable when VFR->CFR conversion is enabled.", "lwmuxer", MB_ICONERROR | MB_OK );
                ret = -1;
            }
            break;
        }
    }
    fclose( ini );
    return ret;
}

static uint64_t get_empty_duration( lsmash_root_t *root, uint32_t track_ID, uint32_t movie_timescale, uint32_t media_timescale )
{
    /* Consider empty duration if the first edit is an empty edit. */
    lsmash_edit_t edit;
    if( lsmash_get_explicit_timeline_map( root, track_ID, 1, &edit ) )
        return 0;
    if( edit.duration && edit.start_time == ISOM_EDIT_MODE_EMPTY )
        return av_rescale_q( edit.duration,
                             (AVRational){ 1, movie_timescale },
                             (AVRational){ 1, media_timescale } );
    return 0;
}

static int64_t get_start_time( lsmash_root_t *root, uint32_t track_ID )
{
    /* Consider start time of this media if any non-empty edit is present. */
    uint32_t edit_count = lsmash_count_explicit_timeline_map( root, track_ID );
    for( uint32_t edit_number = 1; edit_number <= edit_count; edit_number++ )
    {
        lsmash_edit_t edit;
        if( lsmash_get_explicit_timeline_map( root, track_ID, edit_number, &edit ) )
            return 0;
        if( edit.duration == 0 )
            return 0;   /* no edits */
        if( edit.start_time >= 0 )
            return edit.start_time;
    }
    return 0;
}

static void get_first_track_of_type( input_movie_t *input, uint32_t number_of_tracks, uint32_t type )
{
    uint32_t track_ID = 0;
    uint32_t i;
    for( i = 1; i <= number_of_tracks; i++ )
    {
        track_ID = lsmash_get_track_ID( input->root, i );
        if( track_ID == 0 )
            return;
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        if( lsmash_get_media_parameters( input->root, track_ID, &media_param ) )
        {
            DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get media parameters." );
            return;
        }
        if( media_param.handler_type == type )
            break;
    }
    if( i > number_of_tracks )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to find %s track.",
                                   type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? "video" : "audio" );
        return;
    }
    type = (type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK) ? VIDEO_TRACK : AUDIO_TRACK;
    input_track_t *in_track = &input->track[type];
    in_track->track_ID = track_ID;
    lsmash_initialize_track_parameters( &in_track->track_param );
    if( lsmash_get_track_parameters( input->root, track_ID, &in_track->track_param ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get track parameters." );
        return;
    }
    lsmash_initialize_media_parameters( &in_track->media_param );
    if( lsmash_get_media_parameters( input->root, track_ID, &in_track->media_param ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get media parameters." );
        return;
    }
    if( lsmash_construct_timeline( input->root, track_ID ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get construct timeline." );
        return;
    }
    if( lsmash_get_last_sample_delta_from_media_timeline( input->root, track_ID, &in_track->last_sample_delta ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get the last sample delta." );
        return;
    }
    if( lsmash_get_composition_to_decode_shift_from_media_timeline( input->root, track_ID, &in_track->ctd_shift ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get the timeline shift." );
        return;
    }
    in_track->number_of_summaries = lsmash_count_summary( input->root, in_track->track_ID );
    if( in_track->number_of_summaries == 0 )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to find valid summaries." );
        return;
    }
    in_track->summaries = lw_malloc_zero( in_track->number_of_summaries * sizeof(input_summary_t) );
    if( !in_track->summaries )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to alloc input summaries." );
        return;
    }
    for( i = 0; i < in_track->number_of_summaries; i++ )
    {
        lsmash_summary_t *summary = lsmash_get_summary( input->root, in_track->track_ID, i + 1 );
        if( !summary )
        {
            MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get a summary." );
            continue;
        }
        in_track->summaries[i].summary = summary;
        in_track->summaries[i].active  = 1;
    }
    in_track->empty_duration    = get_empty_duration( input->root, track_ID, input->movie_param.timescale, in_track->media_param.timescale );
    in_track->skip_duration     = get_start_time( input->root, track_ID ) + in_track->ctd_shift;
    in_track->number_of_samples = lsmash_get_sample_count_in_media_timeline( input->root, track_ID );
    in_track->active            = 1;
}

static inline uint32_t get_decoding_sample_number( order_converter_t *order_converter, uint32_t composition_sample_number )
{
    return order_converter
         ? order_converter[composition_sample_number].composition_to_decoding
         : composition_sample_number;
}

static int setup_movie_order_converter( input_movie_t *input )
{
    input_track_t *in_video_track = &input->track[VIDEO_TRACK];
    lsmash_media_ts_list_t ts_list;
    if( lsmash_get_media_timestamps( input->root, in_video_track->track_ID, &ts_list ) )
        return -1;
    if( ts_list.sample_count != in_video_track->number_of_samples )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return -1;
    }
    uint32_t composition_sample_delay;
    if( lsmash_get_max_sample_delay( &ts_list, &composition_sample_delay ) )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return -1;
    }
    if( composition_sample_delay )
    {
        /* Note: sample number for L-SMASH is 1-origin. */
        input->order_converter = lw_malloc_zero( (ts_list.sample_count + 1) * sizeof(order_converter_t) );
        if( !input->order_converter )
        {
            lsmash_delete_media_timestamps( &ts_list );
            return -1;
        }
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            ts_list.timestamp[i].dts = i + 1;
        lsmash_sort_timestamps_composition_order( &ts_list );
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            input->order_converter[i + 1].composition_to_decoding = ts_list.timestamp[i].dts;
    }
    lsmash_delete_media_timestamps( &ts_list );
    return 0;
}

static int open_input_movie( lsmash_handler_t *hp, FILE_INFO *fi, int file_id )
{
    input_movie_t **input_array = realloc( hp->input, (hp->number_of_inputs + 1) * sizeof(input_movie_t *) );
    if( !input_array )
        return -1;
    hp->input = input_array;
    input_movie_t *input = lw_malloc_zero( sizeof(input_movie_t) );
    if( !input )
    {
        input_array[hp->number_of_inputs] = NULL;
        return -1;
    }
    input_array[hp->number_of_inputs] = input;
    /* Open an input file. */
    input->root = lsmash_create_root();
    if( !input->root )
        return -1;
    if( lsmash_open_file( fi->name, 1, &input->file_param ) < 0 )
        return -1;
    lsmash_file_t *file = lsmash_set_file( input->root, &input->file_param );
    if( !file )
        return -1;
    if( lsmash_read_file( file, &input->file_param ) < 0 )
        return -1;
    lsmash_initialize_movie_parameters( &input->movie_param );
    lsmash_get_movie_parameters( input->root, &input->movie_param );
    uint32_t number_of_tracks = input->movie_param.number_of_tracks;
    if( number_of_tracks == 0 )
        return -1;
    /* Get video track. */
    get_first_track_of_type( input, number_of_tracks, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
    /* Get audio track. If absent, ignore audio track. */
    get_first_track_of_type( input, number_of_tracks, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
    if( !input->track[VIDEO_TRACK].active && !input->track[AUDIO_TRACK].active )
        return -1;
    lsmash_discard_boxes( input->root );
    if( input->track[VIDEO_TRACK].active
     && setup_movie_order_converter( input ) )
        return -1;
    input->file_id = file_id;
    input->fi      = *fi;
    hp->with_video |= input->track[VIDEO_TRACK].active;
    hp->with_audio |= input->track[AUDIO_TRACK].active;
    ++ hp->number_of_inputs;
    return 0;
}

static int check_file_id_duplication( lsmash_handler_t *hp, int file_id )
{
    for( uint32_t i = 0; i < hp->number_of_inputs; i++ )
        if( file_id == hp->input[i]->file_id )
            return 1;
    return 0;
}

static int get_input_number_from_file_id( lsmash_handler_t *hp, int file_id )
{
    for( int i = 0; i < hp->number_of_inputs; i++ )
        if( file_id == hp->input[i]->file_id )
            return i;
    return -1;
}

static int get_composition_delay( lsmash_root_t *root, uint32_t track_ID,
                                  uint32_t media_start_sample_number, uint32_t media_end_sample_number,
                                  uint32_t ctd_shift, uint64_t *composition_delay )
{
    lsmash_sample_t sample;
    if( lsmash_get_sample_info_from_media_timeline( root, track_ID, media_start_sample_number, &sample ) )
        return -1;
    uint64_t min_dts = sample.dts;
    uint64_t min_cts = sample.cts + ctd_shift;
    for( uint32_t i = media_start_sample_number; i <= media_end_sample_number; i++ )
    {
        if( lsmash_get_sample_info_from_media_timeline( root, track_ID, i, &sample ) )
            return -1;
        min_cts = MIN( min_cts, sample.cts + ctd_shift );
        if( min_cts <= sample.dts )
            break;  /* The minimum CTS of this video sequence must not be found here and later. */
    }
    *composition_delay = min_cts - min_dts;
    return 0;
}

static int setup_exported_range_of_sequence( lsmash_handler_t *hp, input_movie_t *input, uint32_t sequence_number,
                                             uint32_t presentation_start_sample_number, uint32_t presentation_end_sample_number )
{
    /* Edit of video sequence is complicated because of picture reordering.
     * The start and end of some samples included in the video sequence may be invisible on AviUtl's presentation timeline. */
    input_track_t *in_video_track = &input->track[VIDEO_TRACK];
    sequence_t    *video_sequence = &hp->sequence[VIDEO_TRACK][sequence_number - 1];
    video_sequence->input                            = input;
    video_sequence->number                           = sequence_number;
    video_sequence->presentation_start_sample_number = get_decoding_sample_number( input->order_converter, presentation_start_sample_number );
    video_sequence->presentation_end_sample_number   = get_decoding_sample_number( input->order_converter, presentation_end_sample_number );
    video_sequence->media_end_sample_number          = video_sequence->presentation_end_sample_number;
    if( in_video_track->active )
    {
        /* Extend the range of a sequence that may be placed in outside of a presentation.
         * This is needed to export all samples of selected range.
         * For instance,
         *   composition order: ... I[1] B[2] P[3] ...
         *   decoding order:    ... I[1] P[3] B[2] ...
         * Here, B[2] depends on I[1] and P[3].
         * If you want to select P[3] as the end of presentation of a sequence, you need also B[2] obviously.
         * If you want to select B[2] as the end of presentation of a sequence, you need also P[3] obviously. */
        for( uint32_t i = 1; i < presentation_end_sample_number; i++ )
            video_sequence->media_end_sample_number = MAX( video_sequence->media_end_sample_number, get_decoding_sample_number( input->order_converter, i ) );
        /* Find the closest random accessible point, which may be placed in outside of a presentation.
         * A sequence shall start from it or follow the portion of media that includes it.
         * If the closest random accessible point is placed in outside of a presentation,
         * the portion that includes it should be skipped by edit list. */
        uint32_t rap_number;
        if( lsmash_get_closest_random_accessible_point_from_media_timeline( input->root, in_video_track->track_ID, video_sequence->presentation_start_sample_number, &rap_number )
         || rap_number > video_sequence->presentation_start_sample_number )
            /* Any random accessible point is not found. */
            video_sequence->media_start_sample_number = video_sequence->presentation_start_sample_number;
        else
            /* A media starts from random accessible point, while a presentation does NOT always start from random accessible point. */
            video_sequence->media_start_sample_number = rap_number;
        /* Find composition delay within this video sequence. */
        if( get_composition_delay( input->root, in_video_track->track_ID,
                                   video_sequence->media_start_sample_number, video_sequence->media_end_sample_number,
                                   in_video_track->ctd_shift, &video_sequence->composition_delay ) )
            return -1;
    }
    else
    {
        /* Dummy video sequence */
        video_sequence->media_start_sample_number = video_sequence->presentation_start_sample_number;
        in_video_track->ctd_shift = 0;
    }
    video_sequence->current_sample_number = video_sequence->media_start_sample_number;
    video_sequence->number_of_samples     = video_sequence->media_end_sample_number - video_sequence->media_start_sample_number + 1;
    hp->number_of_samples[VIDEO_TRACK]   += in_video_track->active ? video_sequence->number_of_samples : 0;
    /* Decide presentation range of audio track from one of video track if audio track is present. */
    if( !input->track[AUDIO_TRACK].active )
        return 0;
    if( in_video_track->media_param.timescale == 0 && input->fi.video_rate == 0 )
        return -1;
    uint64_t video_presentation_start_time;
    uint64_t video_presentation_end_time;
    uint64_t video_skip_duration;
    uint32_t video_timescale;
    if( in_video_track->active )
    {
        uint64_t video_pts_offset;
        if( hp->av_sync )
        {
            /* Note: Here, assume video samples are displayed on AviUtl's presentation as if ignoring edit list.
             *       Therefore, skipped duration will be added to the audio sequence. */
            video_pts_offset    = in_video_track->empty_duration;
            video_skip_duration = in_video_track->skip_duration;
        }
        else
        {
            uint64_t composition_delay;
            if( get_composition_delay( input->root, in_video_track->track_ID,
                                       1, video_sequence->media_end_sample_number,
                                       in_video_track->ctd_shift, &composition_delay ) )
                return -1;
            video_pts_offset    = -composition_delay;
            video_skip_duration = 0;
        }
        if( lsmash_get_cts_from_media_timeline( input->root, in_video_track->track_ID, video_sequence->presentation_start_sample_number, &video_presentation_start_time )
         || lsmash_get_cts_from_media_timeline( input->root, in_video_track->track_ID, video_sequence->presentation_end_sample_number,   &video_presentation_end_time   ) )
            return -1;
        if( presentation_end_sample_number < in_video_track->number_of_samples )
        {
            video_sequence->presentation_end_next_sample_number = get_decoding_sample_number( input->order_converter, presentation_end_sample_number + 1 );
            uint64_t next_cts;
            if( lsmash_get_cts_from_media_timeline( input->root, in_video_track->track_ID, video_sequence->presentation_end_next_sample_number, &next_cts ) )
                return -1;
            if( next_cts <= video_presentation_end_time )
                return -1;
            video_presentation_end_time += next_cts - video_presentation_end_time;
        }
        else
            video_presentation_end_time += in_video_track->last_sample_delta;
        video_presentation_start_time += video_pts_offset + in_video_track->ctd_shift;
        video_presentation_end_time   += video_pts_offset + in_video_track->ctd_shift;
        video_timescale                = in_video_track->media_param.timescale;
    }
    else
    {
        /* Dummy video sequence */
        video_presentation_start_time = input->fi.video_scale * (video_sequence->media_start_sample_number - 1);
        video_presentation_end_time   = input->fi.video_scale * video_sequence->media_end_sample_number;
        video_skip_duration           = 0;
        video_timescale               = input->fi.video_rate;
    }
    input_track_t *in_audio_track = &input->track[AUDIO_TRACK];
    double timescale_convert_multiplier = (double)in_audio_track->media_param.timescale / video_timescale;
    video_skip_duration *= timescale_convert_multiplier;
    uint64_t    audio_start_time = video_presentation_start_time * timescale_convert_multiplier;
    uint64_t    audio_end_time   = video_presentation_end_time   * timescale_convert_multiplier + 0.5;
    uint32_t    sample_number    = in_audio_track->number_of_samples;
    sequence_t *audio_sequence   = &hp->sequence[AUDIO_TRACK][sequence_number - 1];
    uint64_t    audio_pts_offset;
    uint64_t    audio_skip_duration;
    if( hp->av_sync )
    {
        audio_pts_offset    = in_audio_track->empty_duration;
        audio_skip_duration = in_audio_track->skip_duration;
    }
    else
    {
        audio_pts_offset    = 0;
        audio_skip_duration = 0;
    }
    uint64_t last_sample_end_time = 0;  /* excluding skipped duration at the end. */
    uint64_t pts = 0;
    do
    {
        uint64_t cts;
        if( lsmash_get_cts_from_media_timeline( input->root, in_audio_track->track_ID, sample_number, &cts ) )
        {
            if( sample_number == 0 )
                /* Empty duration might be needed for this sequence. */
                sample_number = 1;
            break;
        }
        cts += in_audio_track->ctd_shift;
        pts = cts + audio_pts_offset - audio_skip_duration + video_skip_duration;
        if( cts + audio_pts_offset + video_skip_duration < audio_skip_duration )
        {
            /* Shift audio timeline to avoid any negative PTS. */
            audio_start_time -= pts;
            audio_end_time   -= pts;
            audio_pts_offset -= pts;
            pts = 0;
        }
        if( sample_number == in_audio_track->number_of_samples )
        {
            last_sample_end_time = pts + in_audio_track->last_sample_delta;
            if( last_sample_end_time > audio_end_time )
                audio_sequence->end_skip_duration = last_sample_end_time - audio_end_time;
        }
        if( pts > audio_end_time )
        {
            audio_sequence->presentation_end_next_sample_number = sample_number;
            audio_sequence->end_skip_duration = pts - audio_end_time;
        }
        if( pts <= audio_start_time )
            break;
        if( cts <= audio_skip_duration )
            /* Empty duration is needed for this sequence. */
            break;
        --sample_number;
    } while( 1 );
    if( audio_start_time >= last_sample_end_time )
    {
        /* No samples in this sequence. */
        audio_sequence->input  = input;
        audio_sequence->number = sequence_number;
        return 0;
    }
    if( pts > audio_start_time )
        audio_sequence->empty_duration = pts - audio_start_time;
    else
        /* Cut off duration within the first audio frame on the presentation of this sequence. */
        audio_sequence->start_skip_duration = audio_start_time - pts;
    if( audio_sequence->presentation_end_next_sample_number == 0 )
        audio_sequence->presentation_end_sample_number = in_audio_track->number_of_samples;
    else
    {
        if( audio_sequence->presentation_end_next_sample_number > 1 )
            audio_sequence->presentation_end_sample_number = audio_sequence->presentation_end_next_sample_number - 1;
        else
            /* This audio track cosists of only one sample. */
            audio_sequence->presentation_end_sample_number = 1;
    }
    uint32_t media_start_sample_number = sample_number;
    if( in_audio_track->media_param.roll_grouping )
    {
        /* Get pre-roll info.
         * Here, if an error occurs, ignore that since it is not so fatal. */
        lsmash_sample_property_t prop;
        if( !lsmash_get_sample_property_from_media_timeline( input->root, in_audio_track->track_ID, sample_number, &prop ) )
            media_start_sample_number -= media_start_sample_number > prop.pre_roll.distance ? prop.pre_roll.distance : 0;
    }
    audio_sequence->input                            = input;
    audio_sequence->number                           = sequence_number;
    audio_sequence->media_end_sample_number          = audio_sequence->presentation_end_sample_number;
    audio_sequence->media_start_sample_number        = media_start_sample_number;
    audio_sequence->presentation_start_sample_number = sample_number;
    audio_sequence->current_sample_number            = audio_sequence->media_start_sample_number;
    audio_sequence->number_of_samples                = audio_sequence->media_end_sample_number - audio_sequence->media_start_sample_number + 1;
    hp->number_of_samples[AUDIO_TRACK]              += audio_sequence->number_of_samples;
    return lsmash_get_cts_from_media_timeline( input->root, in_audio_track->track_ID,
                                               audio_sequence->media_start_sample_number, &audio_sequence->smallest_cts );
}

static int get_input_movies( lsmash_handler_t *hp, void *editp, FILTER *fp, int frame_s, int frame_e )
{
    uint32_t number_of_samples = frame_e - frame_s + 1;
    aviutl_sample_t *aviutl_video_sample = malloc( number_of_samples * sizeof(aviutl_sample_t) );
    if( !aviutl_video_sample )
        return -1;
    int prev_source_file_id      = -1;
    int prev_source_video_number = -1;
    int current_input_number     = 0;
    for( int i = 0; i < number_of_samples; i++ )
    {
        FRAME_STATUS fs;
        if( !fp->exfunc->get_frame_status( editp, frame_s + i, &fs ) )
        {
            MessageBox( HWND_DESKTOP, "Failed to get the status of the first frame.", "lwmuxer", MB_ICONERROR | MB_OK );
            goto fail;
        }
        int source_file_id;
        int source_video_number;
        if( !fp->exfunc->get_source_video_number( editp, fs.video, &source_file_id, &source_video_number ) )
        {
            MessageBox( HWND_DESKTOP, "Failed to get the number of the source video.", "lwmuxer", MB_ICONERROR | MB_OK );
            goto fail;
        }
        FILE_INFO fi;
        if( !fp->exfunc->get_source_file_info( editp, &fi, source_file_id ) )
        {
            MessageBox( HWND_DESKTOP, "Failed to get the information of the source file.", "lwmuxer", MB_ICONERROR | MB_OK );
            goto fail;
        }
        /* Check if encountered a new sequence. */
        if( source_file_id != prev_source_file_id || source_video_number != (prev_source_video_number + 1) )
        {
            if( !check_file_id_duplication( hp, source_file_id )
             && open_input_movie( hp, &fi, source_file_id ) )
                goto fail;
            current_input_number = get_input_number_from_file_id( hp, source_file_id );
            if( current_input_number == -1 )
                goto fail;      /* unknown error */
            ++ hp->number_of_sequences;
            prev_source_file_id = source_file_id;
        }
        prev_source_video_number = source_video_number;
        input_movie_t *input = hp->input[current_input_number];
        aviutl_video_sample[i].input           = input;
        aviutl_video_sample[i].sequence_number = hp->number_of_sequences;
        aviutl_video_sample[i].sample_number   = source_video_number + 1;   /* source_video_number is in ascending order of composition. */
    }
    hp->sequence[VIDEO_TRACK] = lw_malloc_zero( hp->number_of_sequences * sizeof(sequence_t) );
    if( !hp->sequence[VIDEO_TRACK] )
        goto fail;
    if( hp->with_audio )
    {
        hp->sequence[AUDIO_TRACK] = lw_malloc_zero( hp->number_of_sequences * sizeof(sequence_t) );
        if( !hp->sequence[AUDIO_TRACK] )
            goto fail;
    }
    /* Set up exported range of each sequence.
     * Also count number of audio samples for exporting if audio track is present. */
    for( int i = 0; i < number_of_samples; )
    {
        input_movie_t *input               = aviutl_video_sample[i].input;
        uint32_t       sequence_number     = aviutl_video_sample[i].sequence_number;
        uint32_t       start_sample_number = aviutl_video_sample[i].sample_number;
        for( i += 1; i < number_of_samples && sequence_number == aviutl_video_sample[i].sequence_number; i++ );
        uint32_t end_sample_number = aviutl_video_sample[i - 1].sample_number;
        if( setup_exported_range_of_sequence( hp, input, sequence_number, start_sample_number, end_sample_number ) )
            goto fail;
    }
    for( uint32_t i = 0; i < hp->number_of_inputs; i++ )
        if( hp->input[i]->order_converter )
            lw_freep( &hp->input[i]->order_converter );
    free( aviutl_video_sample );
    return 0;
fail:
    free( aviutl_video_sample );
    return -1;
}

static inline uint64_t get_lcm( uint64_t a, uint64_t b )
{
    if( !a )
        return 0;
    return (a / get_gcd( a, b )) * b;
}

static int integrate_media_timescale( lsmash_handler_t *hp )
{
    if( hp->with_audio )
    {
        uint32_t lcm_timescale = hp->input[0]->track[AUDIO_TRACK].media_param.timescale;
        for( uint32_t i = 1; i < hp->number_of_inputs; i++ )
            if( hp->input[i]->track[AUDIO_TRACK].active )
                lcm_timescale = get_lcm( lcm_timescale, hp->input[i]->track[AUDIO_TRACK].media_param.timescale );
        if( lcm_timescale != hp->input[0]->track[AUDIO_TRACK].media_param.timescale )
            return -1;      /* Variable samplerate is not supported. */
        for( uint32_t i = 0; i < hp->number_of_inputs; i++ )
            if( hp->input[i]->track[AUDIO_TRACK].active )
                hp->input[i]->track[AUDIO_TRACK].timescale_integrator = 1;
        hp->output->track[AUDIO_TRACK].media_timescale = lcm_timescale;
        if( !hp->with_video )
            return 0;
    }
    uint32_t lcm_timescale = hp->input[0]->track[VIDEO_TRACK].media_param.timescale;
    for( uint32_t i = 1; i < hp->number_of_inputs; i++ )
        if( hp->input[i]->track[VIDEO_TRACK].active )
            lcm_timescale = get_lcm( lcm_timescale, hp->input[i]->track[VIDEO_TRACK].media_param.timescale );
    if( lcm_timescale == 0 )
        return -1;
    for( uint32_t i = 0; i < hp->number_of_inputs; i++ )
        if( hp->input[i]->track[VIDEO_TRACK].active )
            hp->input[i]->track[VIDEO_TRACK].timescale_integrator = lcm_timescale / hp->input[i]->track[VIDEO_TRACK].media_param.timescale;
    hp->output->track[VIDEO_TRACK].media_timescale = lcm_timescale;
    return 0;
}

static int setup_sequence_order_converter( sequence_t *sequence, uint32_t *composition_sample_delay )
{
    input_movie_t *input = sequence->input;
    input_track_t *in_video_track = &input->track[VIDEO_TRACK];
    lsmash_media_ts_list_t ts_list;
    if( lsmash_get_media_timestamps( input->root, in_video_track->track_ID, &ts_list ) )
        return -1;
    if( ts_list.sample_count != in_video_track->number_of_samples )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return -1;
    }
    ts_list.sample_count = sequence->number_of_samples;
    for( uint32_t i = sequence->media_start_sample_number - 1, j = 0; i < sequence->media_end_sample_number; )
        ts_list.timestamp[j++].cts = ts_list.timestamp[i++].cts;
    if( lsmash_get_max_sample_delay( &ts_list, composition_sample_delay ) )
    {
        lsmash_delete_media_timestamps( &ts_list );
        return -1;
    }
    if( *composition_sample_delay )
    {
        /* Note: sample number for L-SMASH is 1-origin. */
        sequence->order_converter = lw_malloc_zero( (ts_list.sample_count + 1) * sizeof(order_converter_t) );
        if( !sequence->order_converter )
        {
            lsmash_delete_media_timestamps( &ts_list );
            return -1;
        }
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            ts_list.timestamp[i].dts = i + 1;
        lsmash_sort_timestamps_composition_order( &ts_list );
        for( uint32_t i = 0; i < ts_list.sample_count; i++ )
            sequence->order_converter[i + 1].composition_to_decoding = ts_list.timestamp[i].dts;
    }
    lsmash_delete_media_timestamps( &ts_list );
    return 0;
}

static int open_output_file( lsmash_handler_t *hp, FILTER *fp, char *file_name )
{
    output_movie_t *output = hp->output;
    output->root = lsmash_create_root();
    if( !output->root )
        return -1;
    if( lsmash_open_file( file_name, 0, &output->file_param ) < 0 )
        return -1;
    sequence_t *sequence = &hp->sequence[VIDEO_TRACK][0];
    input_movie_t *input = sequence->input;
    output->file_param.major_brand   = input->file_param.major_brand;
    output->file_param.minor_version = input->file_param.minor_version;
    output->file_param.brand_count   = input->file_param.brand_count;
    output->file_param.brands        = input->file_param.brands;
    if( fp->ex_data_ptr && ((option_t *)fp->ex_data_ptr)->import_chapter )
        for( uint32_t i = 0; i < output->file_param.brand_count; i++ )
            if( output->file_param.brands[i] == ISOM_BRAND_TYPE_QT  || output->file_param.brands[i] == ISOM_BRAND_TYPE_M4A
             || output->file_param.brands[i] == ISOM_BRAND_TYPE_M4B || output->file_param.brands[i] == ISOM_BRAND_TYPE_M4P
             || output->file_param.brands[i] == ISOM_BRAND_TYPE_M4V )
            {
                hp->ref_chap_available = 1;
                break;
            }
    if( !lsmash_set_file( output->root, &output->file_param ) )
        return -1;
#ifdef DEBUG
    char log_file_name[MAX_PATH + 10];
    sprintf( log_file_name, "%s_log.txt", file_name );
    hp->log_file = fopen( log_file_name, "wb" );
    if( !hp->log_file )
        return -1;
#endif
    if( integrate_media_timescale( hp ) )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to integrate media timescale." );
        return -1;
    }
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    lsmash_set_movie_parameters( output->root, &movie_param );
    output->number_of_tracks = hp->with_video + hp->with_audio;
    for( uint32_t i = 0; i < 2; i++ )
    {
        input_track_t *in_track = &input->track[i];
        if( !in_track->active )
            continue;
        output_track_t *out_track = &output->track[i];
        out_track->track_ID = lsmash_create_track( output->root, in_track->media_param.handler_type );
        if( !out_track->track_ID )
        {
            DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to create a track." );
            return -1;
        }
        /* Copy track and media parameters except for track_ID and timescale. */
        out_track->track_param = in_track->track_param;
        out_track->media_param = in_track->media_param;
        out_track->track_param.track_ID  = out_track->track_ID;
        out_track->media_param.timescale = out_track->media_timescale;
        if( lsmash_set_track_parameters( output->root, out_track->track_ID, &out_track->track_param ) )
        {
            DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to set track parameters." );
            return -1;
        }
        if( lsmash_set_media_parameters( output->root, out_track->track_ID, &out_track->media_param ) )
        {
            DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to set media parameters." );
            return -1;
        }
        out_track->active = 1;
    }
    if( !output->track[VIDEO_TRACK].active )
        return 0;
    /* Prepare concatenation of video sequences. */
    for( uint32_t i = 0; i < hp->number_of_sequences; i++ )
    {
        /* Set up the order converter of the current video sequence. */
        sequence = &hp->sequence[VIDEO_TRACK][i];
        uint32_t composition_sample_delay;
        if( setup_sequence_order_converter( sequence, &composition_sample_delay ) )
            return -1;
        hp->composition_sample_delay = MAX( hp->composition_sample_delay, composition_sample_delay );
        /* Get the smallest CTS within the current video sequence. */
        input = sequence->input;
        input_track_t *in_track = &input->track[VIDEO_TRACK];
        uint32_t decoding_sample_number = get_decoding_sample_number( sequence->order_converter, 1 )
                                        + sequence->media_start_sample_number - 1;
        if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, decoding_sample_number, &sequence->smallest_cts ) )
        {
            DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to get the smallest CTS within a video sequence." );
            return -1;
        }
    }
    if( hp->composition_sample_delay == 0 )
        return 0;
    hp->prev_reordered_cts = malloc( hp->composition_sample_delay * sizeof(uint64_t) );
    if( !hp->prev_reordered_cts )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to allocate prev_reordered_cts." );
        return -1;
    }
    /* Calculate overall composition time delay. */
    uint32_t composition_sample_delay = hp->composition_sample_delay;
    uint64_t edit_offset = 0;
    uint64_t largest_ts = 0;
    uint64_t second_largest_ts = 0;
    sequence = NULL;
    input_track_t *in_track = NULL;
    for( uint32_t i = 0; i < hp->number_of_sequences; i++ )
    {
        sequence = &hp->sequence[VIDEO_TRACK][i];
        input = sequence->input;
        in_track = &input->track[VIDEO_TRACK];
        if( sequence->number_of_samples > composition_sample_delay )
            break;
        /* Calculate edit timestamp offset. */
        uint64_t cts;
        if( sequence->number_of_samples == 1 )
            second_largest_ts = largest_ts;     /* Not found the second largest CTS.
                                                 * Pick from the largest CTS within the previous sequence. */
        else
        {
            uint32_t second_largest_cts_sample_number = get_decoding_sample_number( sequence->order_converter, sequence->number_of_samples - 1 )
                                                      + sequence->media_start_sample_number - 1;
            if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, second_largest_cts_sample_number, &cts ) )
            {
                MessageBox( HWND_DESKTOP, "Failed to get the second largest CTS from an input track.", "lwmuxer", MB_ICONERROR | MB_OK );
                return -1;
            }
            second_largest_ts = (cts - sequence->smallest_cts) * in_track->timescale_integrator + edit_offset;
        }
        uint32_t largest_cts_sample_number = get_decoding_sample_number( sequence->order_converter, sequence->number_of_samples )
                                           + sequence->media_start_sample_number - 1;
        if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, largest_cts_sample_number, &cts ) )
        {
            MessageBox( HWND_DESKTOP, "Failed to get the largest CTS from an input track.", "lwmuxer", MB_ICONERROR | MB_OK );
            return -1;
        }
        largest_ts = (cts - sequence->smallest_cts) * in_track->timescale_integrator + edit_offset;
        uint64_t last_composition_duration;
        if( largest_ts > second_largest_ts )
            last_composition_duration = largest_ts - second_largest_ts;
        else
        {
            uint32_t sample_delta;
            if( lsmash_get_sample_delta_from_media_timeline( input->root, in_track->track_ID, sequence->media_end_sample_number, &sample_delta ) )
            {
                MessageBox( HWND_DESKTOP, "Failed to get sample delta.", "lwmuxer", MB_ICONERROR | MB_OK );
                return -1;
            }
            last_composition_duration = sample_delta * in_track->timescale_integrator;
        }
        edit_offset = largest_ts + last_composition_duration;
        /* Decrement sample delay by the number of samples within the current sequence
         * since the target sample is in the subsequent sequences. */
        composition_sample_delay -= sequence->number_of_samples;
    }
    uint32_t decoding_sample_number = get_decoding_sample_number( sequence->order_converter, composition_sample_delay + 1 )
                                    + sequence->media_start_sample_number - 1;
    if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, decoding_sample_number, &hp->composition_delay_time ) )
    {
        MessageBox( HWND_DESKTOP, "Failed to get CTS from an input track.", "lwmuxer", MB_ICONERROR | MB_OK );
        return -1;
    }
    hp->composition_delay_time = (hp->composition_delay_time - sequence->smallest_cts) * in_track->timescale_integrator + edit_offset;
    return 0;
}

static uint32_t get_identical_summary( output_movie_t *output, output_track_t *out_track, lsmash_summary_t *summary )
{
    uint32_t number_of_summaries = lsmash_count_summary( output->root, out_track->track_ID );
    for( uint32_t index = 1; index <= number_of_summaries; index++ )
    {
        lsmash_summary_t *out_summary = lsmash_get_summary( output->root, out_track->track_ID, index );
        if( !out_summary )
            continue;
        int ret = lsmash_compare_summary( summary, out_summary );
        lsmash_cleanup_summary( out_summary );
        if( ret == 0 )
            return index;
    }
    return 0;
}

static uint32_t get_output_sample_description_index( output_movie_t *output, output_track_t *out_track, input_track_t *in_track, uint32_t index )
{
    input_summary_t *in_summary = &in_track->summaries[index - 1];
    if( in_summary->active && in_summary->out_mapped_index == 0 )
    {
        in_summary->out_mapped_index = get_identical_summary( output, out_track, in_summary->summary );
        if( in_summary->out_mapped_index == 0 )
        {
            /* Append a new sample description entry. */
            in_summary->out_mapped_index = lsmash_add_sample_entry( output->root, out_track->track_ID, in_summary->summary );
            if( in_summary->out_mapped_index == 0 )
            {
                DEBUG_MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "failed to append a new summary" );
                lsmash_cleanup_summary( in_summary->summary );
                in_summary->summary = NULL;
                in_summary->active  = 0;
            }
        }
    }
    return in_summary->out_mapped_index;
}

static void update_largest_cts( output_track_t *out_track, uint64_t cts )
{
    if( out_track->largest_cts < cts )
    {
        out_track->second_largest_cts = out_track->largest_cts;
        out_track->largest_cts = cts;
    }
    else if( out_track->second_largest_cts < cts )
        out_track->second_largest_cts = cts;
}

static int do_mux( lsmash_handler_t *hp )
{
    int               type                        = VIDEO_TRACK;
    int               active[2]                   = { hp->with_video, hp->with_audio };
    output_movie_t   *output                      = hp->output;
    sequence_t       *sequence[2]                 = { hp->sequence[VIDEO_TRACK], hp->sequence[AUDIO_TRACK] };
    lsmash_sample_t  *sample[2]                   = { NULL, NULL };
    uint32_t          num_consecutive_sample_skip = 0;
    uint32_t          num_active_input_tracks     = output->number_of_tracks;
    uint32_t          num_output_samples[2]       = { 0, 0 };
    uint64_t          total_num_samples           = hp->number_of_samples[VIDEO_TRACK] + hp->number_of_samples[AUDIO_TRACK];
    progress_dlg_t    progress_dlg;
    init_progress_dlg( &progress_dlg, "lwmuxer.auf", IDD_PROGRESS_ABORTABLE );
    while( 1 )
    {
        /* Try append a sample in an input track where we didn't reach the end of media timeline. */
        if( !active[type] )
        {
            type ^= 0x01;
            continue;
        }
        input_movie_t *input    = sequence[type]->input;
        input_track_t *in_track = &input->track[type];
        /* Get a new sample data if the track doesn't hold any one. */
        if( !sample[type] )
        {
            output_track_t *out_track = &output->track[type];
            if( sequence[type]->current_sample_number > sequence[type]->media_end_sample_number )
            {
                /* Give presentation time of the end of this sequence. */
                if( sequence[type]->presentation_end_next_sample_number )
                {
                    uint64_t next_cts;
                    if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, sequence[type]->presentation_end_next_sample_number, &next_cts ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to get CTS from an output track.", "lwmuxer", MB_ICONERROR | MB_OK );
                        break;
                    }
                    next_cts = (next_cts - sequence[type]->smallest_cts) * in_track->timescale_integrator + out_track->edit_offset;
                    if( type == VIDEO_TRACK )
                        next_cts += hp->composition_delay_time;
                    sequence[type]->presentation_end_time += next_cts - sequence[type]->presentation_end_time;
                }
                else
                    sequence[type]->presentation_end_time += sequence[type]->last_sample_delta;
                sequence[type]->presentation_end_time -= sequence[type]->end_skip_duration;
                /* Check if no more sequences. */
                if( sequence[type]->number == hp->number_of_sequences )
                {
                    active[type] = 0;
                    if( --num_active_input_tracks == 0 )
                        break;      /* end of muxing */
                    continue;
                }
                /* Give edit timestamp offset. */
                uint64_t last_composition_duration = out_track->largest_cts > out_track->second_largest_cts
                                                   ? out_track->largest_cts - out_track->second_largest_cts
                                                   : sequence[type]->last_sample_delta;
                out_track->edit_offset = out_track->largest_cts + last_composition_duration;
                if( type == VIDEO_TRACK )
                    out_track->edit_offset -= hp->composition_delay_time;
                /* Move to the next sequence. */
                sequence[type] = &hp->sequence[type][ sequence[type]->number ];
                input    = sequence[type]->input;
                in_track = &input->track[type];
                if( !in_track->active || sequence[type]->number_of_samples == 0 )
                    continue;
                sequence[type]->start_skip_duration *= in_track->timescale_integrator;
                sequence[type]->end_skip_duration   *= in_track->timescale_integrator;
            }
            sample[type] = lsmash_get_sample_from_media_timeline( input->root, in_track->track_ID, sequence[type]->current_sample_number );
            if( sample[type] )
            {
                /* CTS generation */
                sample[type]->cts = (sample[type]->cts - sequence[type]->smallest_cts) * in_track->timescale_integrator + out_track->edit_offset;
                if( type == VIDEO_TRACK )
                    sample[type]->cts += hp->composition_delay_time;
                /* DTS generation */
                if( type == VIDEO_TRACK && hp->composition_sample_delay )
                {
                    uint32_t sequence_sample_number = sequence[type]->current_sample_number - sequence[type]->media_start_sample_number + 1;
                    uint32_t decoding_sample_number = get_decoding_sample_number( sequence[type]->order_converter, sequence_sample_number )
                                                    + sequence[type]->media_start_sample_number - 1;
                    uint64_t reordered_ts;
                    if( lsmash_get_cts_from_media_timeline( input->root, in_track->track_ID, decoding_sample_number, &reordered_ts ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to get CTS from an input track.", "lwmuxer", MB_ICONERROR | MB_OK );
                        break;
                    }
                    reordered_ts = (reordered_ts - sequence[type]->smallest_cts) * in_track->timescale_integrator + out_track->edit_offset;
                    sample[type]->dts = num_output_samples[type] > hp->composition_sample_delay
                                      ? hp->prev_reordered_cts[ (num_output_samples[type] - hp->composition_sample_delay) % hp->composition_sample_delay ]
                                      : reordered_ts;
                    hp->prev_reordered_cts[ num_output_samples[type] % hp->composition_sample_delay ] = reordered_ts + hp->composition_delay_time;
                }
                else
                    sample[type]->dts = sample[type]->cts;
                in_track->dts = (double)sample[type]->dts / out_track->media_param.timescale;
            }
            else
            {
                if( lsmash_check_sample_existence_in_media_timeline( input->root, in_track->track_ID, sequence[type]->current_sample_number ) )
                {
                    MessageBox( HWND_DESKTOP, "Failed to get a sample.", "lwmuxer", MB_ICONERROR | MB_OK );
                    break;
                }
                /* no more samples in this track */
                active[type] = 0;
                if( --num_active_input_tracks == 0 )
                    break;      /* end of muxing */
                type ^= 0x01;
                continue;
            }
        }
        /* Append a sample if meeting a condition. */
        if( in_track->dts <= output->largest_dts || num_consecutive_sample_skip == num_active_input_tracks )
        {
            output_track_t *out_track = &output->track[type];
            sample[type]->index = get_output_sample_description_index( output, out_track, in_track, sample[type]->index );
            uint64_t sample_cts = sample[type]->cts;    /* sample might be deleted internally after appending. */
            uint32_t sample_delta;
            if( lsmash_get_sample_delta_from_media_timeline( input->root, in_track->track_ID, sequence[type]->current_sample_number, &sample_delta ) )
            {
                lsmash_delete_sample( sample[type] );
                MessageBox( HWND_DESKTOP, "Failed to get sample delta.", "lwmuxer", MB_ICONERROR | MB_OK );
                break;
            }
            sample_delta *= in_track->timescale_integrator;
#ifdef DEBUG
            char log_data[1024];
            sprintf( log_data, "sequence_number=%"PRIu32", file_id=%d, type=%s, source_sample_number=%"PRIu32", "
                     "edit_offset=%"PRIu64", smallest_cts=%"PRIu64", DTS=%"PRIu64", CTS=%"PRIu64", "
                     "sample_delta=%"PRIu32", sample_description_index=%"PRIu32"\n",
                     sequence[type]->number, input->file_id, type ? "audio" : "video", sequence[type]->current_sample_number,
                     out_track->edit_offset, sequence[type]->smallest_cts,
                     sample[type]->dts, sample[type]->cts, sample_delta, sample[type]->index );
            fwrite( log_data, 1, strlen( log_data ), hp->log_file );
#endif
            if( sample[type]->index )
            {
                /* Append the current sample into output movie. */
                if( lsmash_append_sample( output->root, out_track->track_ID, sample[type] ) )
                {
                    lsmash_delete_sample( sample[type] );
                    MessageBox( HWND_DESKTOP, "Failed to append a sample.", "lwmuxer", MB_ICONERROR | MB_OK );
                    break;
                }
                num_consecutive_sample_skip = 0;
                ++ num_output_samples[type];
            }
            else
            {
                /* Drop the current invalid sample that doesn't have usable sample_description_index. */
                lsmash_delete_sample( sample[type] );
                --total_num_samples;
            }
            sample[type]                      = NULL;
            sequence[type]->last_sample_delta = sample_delta;
            output->largest_dts               = MAX( output->largest_dts, in_track->dts );
            update_largest_cts( out_track, sample_cts );
            if( sequence[type]->current_sample_number >= sequence[type]->presentation_start_sample_number
             && sequence[type]->current_sample_number <= sequence[type]->presentation_end_sample_number )
            {
                if( sequence[type]->current_sample_number == sequence[type]->presentation_start_sample_number )
                    sequence[type]->presentation_start_time = sample_cts + sequence[type]->start_skip_duration;
                if( sequence[type]->current_sample_number == sequence[type]->presentation_end_sample_number )
                    /* The actual value shall be decided at appending the sample of the end of this sequence. */
                    sequence[type]->presentation_end_time   = sample_cts;
            }
            ++ sequence[type]->current_sample_number;
            /* Update progress dialog.
             * Users can abort muxing by pressing Cancel button. */
            if( update_progress_dlg( &progress_dlg, "Muxing",
                                    ((double)(num_output_samples[VIDEO_TRACK] + num_output_samples[AUDIO_TRACK]) / total_num_samples) * 100.0 ) )
                break;
        }
        else
            ++num_consecutive_sample_skip;      /* Skip appendig sample. */
        type ^= 0x01;
    }
    /* Flush the rest of internally pooled samples and add the last sample_delta. */
    for( uint32_t i = 0; i < 2; i++ )
        if( output->track[i].active
         && lsmash_flush_pooled_samples( output->root, output->track[i].track_ID, sequence[i]->last_sample_delta ) )
            MessageBox( HWND_DESKTOP, "Failed to flush samples.", "lwmuxer", MB_ICONERROR | MB_OK );
    int abort = progress_dlg.abort;
    close_progress_dlg( &progress_dlg );
    return abort;
}

static int construct_timeline_maps( lsmash_handler_t *hp )
{
    output_movie_t *output = hp->output;
    uint32_t movie_timescale = lsmash_get_movie_timescale( output->root );
    for( uint32_t i = 0; i < 2; i++ )
    {
        if( !output->track[i].active || !hp->sequence[i] )
            continue;
        output_track_t *out_track = &output->track[i];
        uint32_t media_timescale = lsmash_get_media_timescale( output->root, out_track->track_ID );
        if( media_timescale == 0 )
            continue;
        double timescale_convert_multiplier = (double)movie_timescale / media_timescale;
        /* Create edits per sequence. */
        for( int j = 0; j < hp->number_of_sequences; j++ )
        {
            sequence_t *sequence = &hp->sequence[i][j];
            lsmash_edit_t edit;
            if( sequence->empty_duration )
            {
                /* Set up an empty edit for this sequence. */
                edit.duration   = sequence->empty_duration * timescale_convert_multiplier;
                edit.start_time = ISOM_EDIT_MODE_EMPTY;
                edit.rate       = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                    MessageBox( HWND_DESKTOP, "Failed to create an empty edit of an output track.", "lwmuxer", MB_ICONERROR | MB_OK );
            }
            if( sequence->number_of_samples )
            {
                /* Set up a normal edit for this sequence. */
                edit.duration   = (sequence->presentation_end_time - sequence->presentation_start_time) * timescale_convert_multiplier;
                edit.start_time = sequence->presentation_start_time;
                edit.rate       = ISOM_EDIT_MODE_NORMAL;
                if( lsmash_create_explicit_timeline_map( output->root, out_track->track_ID, edit ) )
                {
                    MessageBox( HWND_DESKTOP, "Failed to create the timeline map of an output track.", "lwmuxer", MB_ICONERROR | MB_OK );
                    continue;
                }
            }
#ifdef DEBUG
            char log_data[1024];
            sprintf( log_data, "type=%s, sequence_number=%"PRIu32", presentation_start_time=%"PRIu64", presentation_end_time=%"PRIu64", "
                     "start_skip_duration=%"PRIu64", end_skip_duration=%"PRIu64"\n",
                     i ? "audio" : "video", sequence->number, sequence->presentation_start_time, sequence->presentation_end_time,
                     sequence->start_skip_duration, sequence->end_skip_duration );
            fwrite( log_data, 1, strlen( log_data ), hp->log_file );
#endif
        }
    }
    return 0;
}

static int validate_chapter( char *chapter_file )
{
    int ret = -1;
    char buff[CHAPTER_BUFSIZE];
    FILE *fp = fopen( chapter_file, "rb" );
    if( !fp )
    {
        MessageBox( HWND_DESKTOP, "Failed to open the chapter file.", "lwmuxer", MB_ICONERROR | MB_OK );
        return -1;
    }
    if( fgets( buff, CHAPTER_BUFSIZE, fp ) )
    {
        char *p_buff = !memcmp( buff, UTF8_BOM, UTF8_BOM_LENGTH ) ? &buff[UTF8_BOM_LENGTH] : &buff[0];   /* BOM detection */
        if( !strncmp( p_buff, "CHAPTER", 7 ) || ( isdigit( p_buff[0] ) && isdigit( p_buff[1] ) && p_buff[2] == ':'
            && isdigit( p_buff[3] ) && isdigit( p_buff[4] ) && p_buff[5] == ':' ) )
            ret = 0;
        else
            MessageBox( HWND_DESKTOP, "The chapter file is malformed.", "lwmuxer", MB_ICONERROR | MB_OK );
    }
    else
        MessageBox( HWND_DESKTOP, "Failed to read the chapter file.", "lwmuxer", MB_ICONERROR | MB_OK );
    fclose( fp );
    return ret;
}

static int write_reference_chapter( lsmash_handler_t *hp, FILTER *fp )
{
    if( !hp->ref_chap_available )
        return 0;
    option_t *opt = (option_t *)fp->ex_data_ptr;
    if( !opt || !opt->import_chapter )
        return 0;
    uint32_t track_ID = hp->output->track[ hp->with_video ? VIDEO_TRACK : AUDIO_TRACK ].track_ID;
    return lsmash_create_reference_chapter_track( hp->output->root, track_ID, opt->chapter_file );
}

static int write_chapter_list( lsmash_handler_t *hp, FILTER *fp )
{
    option_t *opt = (option_t *)fp->ex_data_ptr;
    if( !opt || !opt->import_chapter )
        return 0;
    return lsmash_set_tyrant_chapter( hp->output->root, opt->chapter_file, 0 );
}

static int moov_to_front_callback( void *param, uint64_t written_movie_size, uint64_t total_movie_size )
{
    progress_dlg_t *progress_dlg = (progress_dlg_t *)param;
    update_progress_dlg( progress_dlg, "Finalizing", ((double)written_movie_size / total_movie_size) * 100.0 );
    return 0;
}

static int finish_movie( output_movie_t *output, int optimize_pd )
{
    if( !optimize_pd )
        return lsmash_finish_movie( output->root, NULL );
    progress_dlg_t progress_dlg;
    init_progress_dlg( &progress_dlg, "lwmuxer.auf", IDD_PROGRESS_UNABORTABLE );
    lsmash_adhoc_remux_t moov_to_front;
    moov_to_front.func        = moov_to_front_callback;
    moov_to_front.buffer_size = 4*1024*1024;    /* 4MiB */
    moov_to_front.param       = &progress_dlg;
    int ret = lsmash_finish_movie( output->root, &moov_to_front );
    close_progress_dlg( &progress_dlg );
    return ret;
}

static void cleanup_handler( lsmash_handler_t *hp )
{
    if( !hp )
        return;
    output_movie_t *output = hp->output;
    if( output )
    {
        lsmash_close_file( &output->file_param );
        lsmash_destroy_root( output->root );
    }
    if( hp->input )
        for( uint32_t i = 0; i < hp->number_of_inputs; i++ )
        {
            input_movie_t *input = hp->input[i];
            if( input )
            {
                if( input->order_converter )
                    free( input->order_converter );
                lsmash_close_file( &input->file_param );
                lsmash_destroy_root( input->root );
                for( uint32_t j = 0; j < 2; j++ )
                {
                    input_track_t *in_track = &input->track[j];
                    if( in_track->summaries )
                    {
                        for( uint32_t k = 0; k < in_track->number_of_summaries; k++ )
                            lsmash_cleanup_summary( in_track->summaries[k].summary );
                        free( in_track->summaries );
                    }
                }
            }
        }
    if( hp->sequence[VIDEO_TRACK] )
    {
        for( uint32_t i = 0; i < hp->number_of_sequences; i++ )
            if( hp->sequence[VIDEO_TRACK][i].order_converter )
                free( hp->sequence[VIDEO_TRACK][i].order_converter );
        free( hp->sequence[VIDEO_TRACK] );
    }
    if( hp->sequence[AUDIO_TRACK] )
        free( hp->sequence[AUDIO_TRACK] );
    if( hp->prev_reordered_cts )
        free( hp->prev_reordered_cts );
#ifdef DEBUG
    if( hp->log_file )
        fclose( hp->log_file );
#endif
}

static BOOL exporter_error( lsmash_handler_t *hp )
{
    cleanup_handler( hp );
    return FALSE;
}

static void cleanup_option( FILTER *fp )
{
    if( !fp )
        return;
    if( fp->ex_data_ptr )
        lw_freep( &fp->ex_data_ptr );
    fp->ex_data_size = 0;
}

static inline void disable_chapter( HWND hwnd, option_t *opt )
{
    opt->import_chapter = 0;
    memset( opt->chapter_file, 0, MAX_PATH );
    SetDlgItemText( hwnd, IDC_EDIT_CHAPTER_PATH, (LPCTSTR)opt->chapter_file );
}

static BOOL CALLBACK dialog_proc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
    static void   *editp;
    static FILTER *fp;
	switch( message )
    {
        case WM_INITDIALOG:
        {
            editp = ((muxer_t *)lparam)->editp;
            fp    = ((muxer_t *)lparam)->fp;
            option_t *opt = (option_t *)fp->ex_data_ptr;
            if( !opt )
            {
                opt = lw_malloc_zero( sizeof(option_t) );
                if( !opt )
                {
                    MessageBox( HWND_DESKTOP, "Failed to allocate memory for option.", "lwmuxer", MB_ICONERROR | MB_OK );
                    return FALSE;
                }
                fp->ex_data_ptr  = opt;
                fp->ex_data_size = sizeof(option_t);
                opt->optimize_pd = 1;
            }
            SendMessage( GetDlgItem( hwnd, IDC_CHECK_OPTIMIZE_PD ), BM_SETCHECK, (WPARAM) opt->optimize_pd ? BST_CHECKED : BST_UNCHECKED, 0 );
            SetDlgItemText( hwnd, IDC_EDIT_CHAPTER_PATH, (LPCTSTR)opt->chapter_file );
            break;
        }
        case WM_COMMAND:
            switch( wparam )
            {
                case IDOK :
                {
                    lsmash_handler_t h = { 0 };
                    if( get_settings( &h ) < 0 )
                    {
                        EndDialog( hwnd, IDCANCEL );
                        return FALSE;
                    }
                    option_t *opt = (option_t *)fp->ex_data_ptr;
                    opt->optimize_pd = (BST_CHECKED == SendMessage( GetDlgItem( hwnd, IDC_CHECK_OPTIMIZE_PD ), BM_GETCHECK, 0, 0 ));
                    char file_name_ansi[MAX_PATH * 4];
                    char file_name_utf8[MAX_PATH * 4];
                    if( !fp->exfunc->dlg_get_save_name( (LPSTR)file_name_ansi, MPEG4_FILE_EXT, NULL ) )
                    {
                        EndDialog( hwnd, IDOK );
                        return FALSE;
                    }
                    ShowWindow( hwnd, SW_HIDE );
                    if( lsmash_convert_ansi_to_utf8( file_name_ansi, file_name_utf8, sizeof(file_name_utf8) ) == 0 )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to convert the output file name to UTF-8.", "lwmuxer", MB_ICONERROR  | MB_OK );
                        return FALSE;
                    }
                    int frame_s;
                    int frame_e;
                    if( !fp->exfunc->get_select_frame( editp, &frame_s, &frame_e ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to get the selection range.", "lwmuxer", MB_ICONERROR | MB_OK );
                        return FALSE;
                    }
                    output_movie_t out_movie = { 0 };
                    h.output = &out_movie;
                    if( get_input_movies( &h, editp, fp, frame_s, frame_e ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to open the input files.", "lwmuxer", MB_ICONERROR | MB_OK );
                        return exporter_error( &h );
                    }
                    if( open_output_file( &h, fp, file_name_utf8 ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to open the output file.", "lwmuxer", MB_ICONERROR  | MB_OK );
                        return exporter_error( &h );
                    }
                    if( write_reference_chapter( &h, fp ) )
                        MessageBox( HWND_DESKTOP, "Failed to set reference chapter.", "lwmuxer", MB_ICONWARNING  | MB_OK );
                    /* Mux with a progress dialog.
                     * Users can abort muxing by pressing Cancel button on it. */
                    int abort = do_mux( &h );
                    /* Finalize with or without a progress dialog.
                     * Users can NOT abort finalizing. */
                    if( construct_timeline_maps( &h ) )
                        MessageBox( HWND_DESKTOP, "Failed to costruct timeline maps.", "lwmuxer", MB_ICONERROR | MB_OK );
                    if( write_chapter_list( &h, fp ) )
                        MessageBox( HWND_DESKTOP, "Failed to write chapter list.", "lwmuxer", MB_ICONWARNING | MB_OK );
                    if( finish_movie( h.output, !abort && opt->optimize_pd ) )
                    {
                        MessageBox( HWND_DESKTOP, "Failed to finish movie.", "lwmuxer", MB_ICONERROR | MB_OK );
                        return exporter_error( &h );
                    }
                    cleanup_handler( &h );
                    EndDialog( hwnd, IDOK );
                    break;
                }
                case IDC_BUTTON_OPTION_DEFAULT :
                {
                    option_t *opt = (option_t *)fp->ex_data_ptr;
                    opt->optimize_pd = 1;
                    SendMessage( GetDlgItem( hwnd, IDC_CHECK_OPTIMIZE_PD ), BM_SETCHECK, (WPARAM)BST_CHECKED, 0 );
                    disable_chapter( hwnd, opt );
                    break;
                }
                case IDC_BUTTON_CHAPTER_BROWSE :
                {
                    option_t *opt = (option_t *)fp->ex_data_ptr;
                    char *chapter_file = opt->chapter_file;
                    if( !fp->exfunc->dlg_get_load_name( (LPSTR)chapter_file, "Chapter file\0*.*\0", chapter_file[0] == '\0' ? "chapter.txt" : chapter_file ) )
                    {
                        FILE *existence_check = fopen( chapter_file, "rb" );
                        if( !existence_check )
                        {
                            disable_chapter( hwnd, opt );
                            break;
                        }
                        fclose( existence_check );
                        if( MessageBox( HWND_DESKTOP,
                                        "Do you want to clear which chapter file is currently selected?",
                                        "lwmuxer",
                                        MB_ICONQUESTION | MB_YESNO ) == IDYES )
                        {
                            disable_chapter( hwnd, opt );
                            break;
                        }
                    }
                    else if( validate_chapter( chapter_file ) )
                    {
                        disable_chapter( hwnd, opt );
                        break;
                    }
                    SetDlgItemText( hwnd, IDC_EDIT_CHAPTER_PATH, (LPCTSTR)chapter_file );
                    opt->import_chapter = 1;
                    break;
                }
                default :
                    break;
            }
            break;
        default :
            break;
	}
	return FALSE;
}

BOOL func_WndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, void *editp, FILTER *fp )
{
    if( !fp->exfunc->is_editing( editp ) )
        return FALSE;
    switch( message )
    {
        case WM_FILTER_EXPORT :
        {
            muxer_t muxer;
            muxer.editp = editp;
            muxer.fp    = fp;
            DialogBoxParam( GetModuleHandle( "lwmuxer.auf" ), MAKEINTRESOURCE( IDD_MUXER_OPTIONS ), hwnd, dialog_proc, (LPARAM)&muxer );
            break;
        }
        case WM_FILTER_FILE_CLOSE :
            cleanup_option( fp );
            break;
        default :
            break;
    }
    return FALSE;
}

BOOL func_exit( FILTER *fp )
{
    cleanup_option( fp );
    return FALSE;
}
