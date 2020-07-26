/*****************************************************************************
 * lwlibav_source.c
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
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
 * However, when distributing its binary file, it will be under LGPL or GPL. */

#define NO_PROGRESS_HANDLER

/* Libav (LGPL or GPL) */
#include <libavformat/avformat.h>       /* Codec specific info importer */
#include <libavcodec/avcodec.h>         /* Decoder */
#include <libswscale/swscale.h>         /* Colorspace converter */
#include <libswresample/swresample.h>   /* Audio resampler */
#include <libavutil/imgutils.h>

#include "../common/audio_output.h"
#ifndef _MSC_VER
/* Dummy definitions.
 * Audio resampler/buffer is NOT used at all in this filter. */
typedef void AVAudioResampleContext;
typedef void audio_samples_t;
int flush_resampler_buffers( AVAudioResampleContext *avr ){ return 0; }
int update_resampler_configuration( AVAudioResampleContext *avr,
                                    uint64_t out_channel_layout, int out_sample_rate, enum AVSampleFormat out_sample_fmt,
                                    uint64_t  in_channel_layout, int  in_sample_rate, enum AVSampleFormat  in_sample_fmt,
                                    int *input_planes, int *input_block_align ){ return 0; }
int resample_audio( AVAudioResampleContext *avr, audio_samples_t *out, audio_samples_t *in ){ return 0; }
uint64_t output_pcm_samples_from_buffer
(
    lw_audio_output_handler_t *aohp,
    AVFrame                   *frame_buffer,
    uint8_t                  **output_buffer,
    enum audio_output_flag    *output_flags
)
{
    return 0;
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
    return 0;
}

void lw_cleanup_audio_output_handler( lw_audio_output_handler_t *aohp ){ }
#endif

#include <stdio.h>
#include <string.h>
#include "lsmashsource.h"
#include "video_output.h"

#include "../common/progress.h"
#include "../common/lwlibav_dec.h"
#include "../common/lwlibav_video.h"
#include "../common/lwlibav_video_internal.h"
#include "../common/lwlibav_audio.h"
#include "../common/lwindex.h"

typedef struct
{
    VSVideoInfo                     vi[2];
    lwlibav_file_handler_t          lwh;
    lwlibav_video_decode_handler_t *vdhp;
    lwlibav_video_output_handler_t *vohp;
    lwlibav_audio_decode_handler_t *adhp;
    lwlibav_audio_output_handler_t *aohp;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
} lwlibav_handler_t;

/* Deallocate the handler of this plugin. */
static void free_handler
(
    lwlibav_handler_t **hpp
)
{
    if( !hpp || !*hpp )
        return;
    lwlibav_handler_t *hp = *hpp;
    lw_free( lwlibav_video_get_preferred_decoder_names( hp->vdhp ) );
    lwlibav_video_free_decode_handler( hp->vdhp );
    lwlibav_video_free_output_handler( hp->vohp );
    lwlibav_audio_free_decode_handler( hp->adhp );
    lwlibav_audio_free_output_handler( hp->aohp );
    lw_free( hp->lwh.file_path );
    lw_free( hp );
}

/* Allocate the handler of this plugin. */
static lwlibav_handler_t *alloc_handler
(
    void
)
{
    lwlibav_handler_t *hp = (lwlibav_handler_t *)lw_malloc_zero( sizeof(lwlibav_handler_t) );
    if( !hp )
        return NULL;
    if( !(hp->vdhp = lwlibav_video_alloc_decode_handler())
     || !(hp->vohp = lwlibav_video_alloc_output_handler())
     || !(hp->adhp = lwlibav_audio_alloc_decode_handler())
     || !(hp->aohp = lwlibav_audio_alloc_output_handler()) )
    {
        free_handler( &hp );
        return NULL;
    }
    return hp;
}

static int update_indicator( progress_handler_t *php, const char *message, int percent )
{
    static int last_percent = -1;
    if ( !strcmp( message, "Creating Index file" ) && last_percent != percent )
    {
        last_percent = percent;
        fprintf( stderr, "Creating lwi index file %d%%\r", percent );
        fflush( stderr );
    }
    return 0;
}

static void close_indicator( progress_handler_t *php )
{
    fprintf( stderr, "\n" );
}

static void VS_CC vs_filter_init( VSMap *in, VSMap *out, void **instance_data, VSNode *node, VSCore *core, const VSAPI *vsapi )
{
    lwlibav_handler_t *hp = (lwlibav_handler_t *)*instance_data;
    AVCodecContext *ctx = lwlibav_video_get_codec_context( hp->vdhp );
    vsapi->setVideoInfo( hp->vi, (av_pix_fmt_desc_get( ctx->pix_fmt )->flags & AV_PIX_FMT_FLAG_ALPHA) ? 2 : 1, node );
}

static void set_frame_properties
(
    VSVideoInfo *vi,
    AVFrame     *av_frame,
    AVStream    *stream,
    VSFrameRef  *vs_frame,
    const VSAPI *vsapi
)
{
    /* Variable Frame Rate is not supported yet. */
    int64_t duration_num = vi->fpsDen;
    int64_t duration_den = vi->fpsNum;
    vs_set_frame_properties( av_frame, stream, duration_num, duration_den, vs_frame, vsapi );
}

static int prepare_video_decoding
(
    lwlibav_handler_t *hp,
    VSMap             *out,
    VSCore            *core,
    const VSAPI       *vsapi
)
{
    lwlibav_video_decode_handler_t *vdhp = hp->vdhp;
    lwlibav_video_output_handler_t *vohp = hp->vohp;
    VSVideoInfo                    *vi   = &hp->vi[0];
    /* Import AVIndexEntrys. */
    if( lwlibav_import_av_index_entry( (lwlibav_decode_handler_t *)vdhp ) < 0 )
        return -1;
    /* Set up output format. */
    lwlibav_video_set_initial_input_format( vdhp );
    AVCodecContext *ctx = lwlibav_video_get_codec_context( vdhp );
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)vohp->private_handler;
    vs_vohp->frame_ctx = NULL;
    vs_vohp->core      = core;
    vs_vohp->vsapi     = vsapi;
    int max_width  = lwlibav_video_get_max_width ( vdhp );
    int max_height = lwlibav_video_get_max_height( vdhp );
    if( vs_setup_video_rendering( vohp, ctx, vi, out, max_width, max_height ) < 0 )
        return -1;
    lwlibav_video_set_get_buffer_func( vdhp );
    /* Find the first valid video frame. */
    if( lwlibav_video_find_first_valid_frame( vdhp ) < 0 )
    {
        set_error_on_init( out, vsapi, "lsmas: failed to allocate the first valid video frame." );
        return -1;
    }
    if( (av_pix_fmt_desc_get( ctx->pix_fmt )->flags & AV_PIX_FMT_FLAG_ALPHA)
     && hp->vi[0].format )
    {
        hp->vi[1] = hp->vi[0];
        hp->vi[1].format = vsapi->registerFormat( cmGray, hp->vi[0].format->sampleType, hp->vi[0].format->bitsPerSample, 0, 0, core );
        vs_vohp->background_frame[1] = vsapi->newVideoFrame( hp->vi[1].format, hp->vi[1].width, hp->vi[1].height, NULL, core );
        if( !vs_vohp->background_frame[1] )
        {
            set_error_on_init( out, vsapi, "lsmas: failed to allocate memory for the alpha frame data." );
            return -1;
        }
    }
    /* Force seeking at the first reading. */
    lwlibav_video_force_seek( vdhp );
    return 0;
}

static const VSFrameRef *VS_CC vs_filter_get_frame( int n, int activation_reason, void **instance_data, void **frame_data, VSFrameContext *frame_ctx, VSCore *core, const VSAPI *vsapi )
{
    if( activation_reason != arInitial )
        return NULL;
    lwlibav_handler_t *hp = (lwlibav_handler_t *)*instance_data;
    VSVideoInfo       *vi = &hp->vi[0];
    uint32_t frame_number = MIN( n + 1, vi->numFrames );    /* frame_number is 1-origin. */
    lwlibav_video_decode_handler_t *vdhp = hp->vdhp;
    lwlibav_video_output_handler_t *vohp = hp->vohp;
    if( lwlibav_video_get_error( vdhp ) )
    {
        vsapi->setFilterError( "lsmas: failed to output a video frame.", frame_ctx );
        return NULL;
    }
    /* Set up VapourSynth error handler. */
    vs_basic_handler_t vsbh = { 0 };
    vsbh.out       = NULL;
    vsbh.frame_ctx = frame_ctx;
    vsbh.vsapi     = vsapi;
    lw_log_handler_t *lhp = lwlibav_video_get_log_handler( vdhp );
    lhp->priv     = &vsbh;
    lhp->show_log = set_error;
    /* Get and decode the desired video frame. */
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)vohp->private_handler;
    vs_vohp->frame_ctx = frame_ctx;
    vs_vohp->core      = core;
    vs_vohp->vsapi     = vsapi;
    if( lwlibav_video_get_frame( vdhp, vohp, frame_number ) < 0 )
    {
        vsapi->setFilterError( "lsmas: failed to output a video frame.", frame_ctx );
        return NULL;
    }
    /* Output the video frame. */
    AVFrame    *av_frame = lwlibav_video_get_frame_buffer( vdhp );
    int output_index = vsapi->getOutputIndex( frame_ctx );
    VSFrameRef *vs_frame = make_frame( vohp, av_frame, output_index );
    if( !vs_frame )
    {
        vsapi->setFilterError( "lsmas: failed to output a video frame.", frame_ctx );
        return NULL;
    }
    set_frame_properties( vi, av_frame, vdhp->format->streams[vdhp->stream_index], vs_frame, vsapi );
    return vs_frame;
}

static void VS_CC vs_filter_free( void *instance_data, VSCore *core, const VSAPI *vsapi )
{
    free_handler( (lwlibav_handler_t **)&instance_data );
}

void VS_CC vs_lwlibavsource_create( const VSMap *in, VSMap *out, void *user_data, VSCore *core, const VSAPI *vsapi )
{
    const char *file_path = vsapi->propGetData( in, "source", 0, NULL );
    /* Allocate the handler of this filter function. */
    lwlibav_handler_t *hp = alloc_handler();
    if( !hp )
    {
        vsapi->setError( out, "lsmas: failed to allocate the LW-Libav handler." );
        return;
    }
    lwlibav_file_handler_t         *lwhp = &hp->lwh;
    lwlibav_video_decode_handler_t *vdhp = hp->vdhp;
    lwlibav_video_output_handler_t *vohp = hp->vohp;
    vs_video_output_handler_t *vs_vohp = vs_allocate_video_output_handler( vohp );
    if( !vs_vohp )
    {
        free_handler( &hp );
        vsapi->setError( out, "lsmas: failed to allocate the VapourSynth video output handler." );
        return;
    }
    /* Set up VapourSynth error handler. */
    vs_basic_handler_t vsbh = { 0 };
    vsbh.out       = out;
    vsbh.frame_ctx = NULL;
    vsbh.vsapi     = vsapi;
    /* Set up log handler. */
    lw_log_handler_t lh = { 0 };
    lh.level    = LW_LOG_FATAL;
    lh.priv     = &vsbh;
    lh.show_log = set_error;
    /* Get options. */
    int64_t stream_index;
    int64_t threads;
    int64_t cache_index;
    int64_t seek_mode;
    int64_t seek_threshold;
    int64_t variable_info;
    int64_t direct_rendering;
    int64_t fps_num;
    int64_t fps_den;
    int64_t prefer_hw_decoder;
    int64_t apply_repeat_flag;
    int64_t field_dominance;
    int64_t ff_loglevel;
    const char *index_file_path;
    const char *format;
    const char *preferred_decoder_names;
    set_option_int64 ( &stream_index,           -1,    "stream_index",   in, vsapi );
    set_option_int64 ( &threads,                 0,    "threads",        in, vsapi );
    set_option_int64 ( &cache_index,             1,    "cache",          in, vsapi );
    set_option_int64 ( &seek_mode,               0,    "seek_mode",      in, vsapi );
    set_option_int64 ( &seek_threshold,          10,   "seek_threshold", in, vsapi );
    set_option_int64 ( &variable_info,           0,    "variable",       in, vsapi );
    set_option_int64 ( &direct_rendering,        0,    "dr",             in, vsapi );
    set_option_int64 ( &fps_num,                 0,    "fpsnum",         in, vsapi );
    set_option_int64 ( &fps_den,                 1,    "fpsden",         in, vsapi );
    set_option_int64 ( &prefer_hw_decoder,       0,    "prefer_hw",      in, vsapi );
    set_option_int64 ( &apply_repeat_flag,       1,    "repeat",         in, vsapi );
    set_option_int64 ( &field_dominance,         0,    "dominance",      in, vsapi );
    set_option_int64 ( &ff_loglevel,             0,    "ff_loglevel",    in, vsapi );
    set_option_string( &index_file_path,         NULL, "cachefile",      in, vsapi );
    set_option_string( &format,                  NULL, "format",         in, vsapi );
    set_option_string( &preferred_decoder_names, NULL, "decoder",        in, vsapi );
    set_preferred_decoder_names_on_buf( hp->preferred_decoder_names_buf, preferred_decoder_names );
    /* Set options. */
    lwlibav_option_t opt;
    opt.file_path         = file_path;
    opt.threads           = threads >= 0 ? threads : 0;
    opt.av_sync           = 0;
    opt.no_create_index   = !cache_index;
    opt.index_file_path   = index_file_path;
    opt.force_video       = (stream_index >= 0);
    opt.force_video_index = stream_index >= 0 ? stream_index : -1;
    opt.force_audio       = 0;
    opt.force_audio_index = -2;
    opt.apply_repeat_flag = apply_repeat_flag;
    opt.field_dominance   = CLIP_VALUE( field_dominance, 0, 2 );    /* 0: Obey source flags, 1: TFF, 2: BFF */
    opt.vfr2cfr.active    = fps_num > 0 && fps_den > 0 ? 1 : 0;
    opt.vfr2cfr.fps_num   = fps_num;
    opt.vfr2cfr.fps_den   = fps_den;
    lwlibav_video_set_seek_mode              ( vdhp, CLIP_VALUE( seek_mode,      0, 2 ) );
    lwlibav_video_set_forward_seek_threshold ( vdhp, CLIP_VALUE( seek_threshold, 1, 999 ) );
    lwlibav_video_set_preferred_decoder_names( vdhp, tokenize_preferred_decoder_names( hp->preferred_decoder_names_buf ) );
    lwlibav_video_set_prefer_hw_decoder      ( vdhp, CLIP_VALUE( prefer_hw_decoder, 0, 3 ) );
    vs_vohp->variable_info          = CLIP_VALUE( variable_info,     0, 1 );
    vs_vohp->direct_rendering       = CLIP_VALUE( direct_rendering,  0, 1 ) && !format;
    vs_vohp->vs_output_pixel_format = vs_vohp->variable_info ? pfNone : get_vs_output_pixel_format( format );
    if( ff_loglevel <= 0 )
        av_log_set_level( AV_LOG_QUIET );
    else if( ff_loglevel == 1 )
        av_log_set_level( AV_LOG_PANIC );
    else if( ff_loglevel == 2 )
        av_log_set_level( AV_LOG_FATAL );
    else if( ff_loglevel == 3 )
        av_log_set_level( AV_LOG_ERROR );
    else if( ff_loglevel == 4 )
        av_log_set_level( AV_LOG_WARNING );
    else if( ff_loglevel == 5 )
        av_log_set_level( AV_LOG_INFO );
    else if( ff_loglevel == 6 )
        av_log_set_level( AV_LOG_VERBOSE );
    else if( ff_loglevel == 7 )
        av_log_set_level( AV_LOG_DEBUG );
    else
        av_log_set_level( AV_LOG_TRACE );
    /* Set up progress indicator. */
    progress_indicator_t indicator;
    indicator.open   = NULL;
    indicator.update = update_indicator;
    indicator.close  = close_indicator;
    /* Construct index. */
    int ret = lwlibav_construct_index( lwhp, vdhp, vohp, hp->adhp, hp->aohp, &lh, &opt, &indicator, NULL );
    lwlibav_audio_free_decode_handler_ptr( &hp->adhp );
    lwlibav_audio_free_output_handler_ptr( &hp->aohp );
    if( ret < 0 )
    {
        free_handler( &hp );
        set_error_on_init( out, vsapi, "lsmas: failed to construct index." );
        return;
    }
    /* Get the desired video track. */
    lwlibav_video_set_log_handler( vdhp, &lh );
    if( lwlibav_video_get_desired_track( lwhp->file_path, vdhp, lwhp->threads ) < 0 )
    {
        free_handler( &hp );
        vsapi->setError( out, "lsmas: failed to get video track." );
        return;
    }
    /* Set average framerate. */
    hp->vi[0].numFrames = vohp->frame_count;
    hp->vi[0].fpsNum    = 25;
    hp->vi[0].fpsDen    = 1;
    lwlibav_video_setup_timestamp_info( lwhp, vdhp, vohp, &hp->vi[0].fpsNum, &hp->vi[0].fpsDen, opt.apply_repeat_flag );
    /* Set up decoders for this stream. */
    if( prepare_video_decoding( hp, out, core, vsapi ) < 0 )
    {
        free_handler( &hp );
        return;
    }
    vsapi->createFilter( in, out, "LWLibavSource", vs_filter_init, vs_filter_get_frame, vs_filter_free, fmUnordered, nfMakeLinear, hp, core );
}
