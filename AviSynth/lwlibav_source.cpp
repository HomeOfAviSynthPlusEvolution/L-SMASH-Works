/*****************************************************************************
 * lwlibav_source.cpp
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

#include <stdio.h>
#include <string.h>
#include "lsmashsource.h"

extern "C"
{
/* Libav
 * The binary file will be LGPLed or GPLed. */
#include <libavformat/avformat.h>       /* Codec specific info importer */
#include <libavcodec/avcodec.h>         /* Decoder */
#include <libswscale/swscale.h>         /* Colorspace converter */
#include <libswresample/swresample.h>   /* Audio resampler */
#include <libavutil/imgutils.h>
}

#include "video_output.h"
#include "audio_output.h"
#include "lwlibav_source.h"
#include "../common/lwlibav_video_internal.h"

#ifdef _MSC_VER
#pragma warning( disable:4996 )
#endif

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

static void set_frame_properties
(
    AVFrame* av_frame,
    AVStream* stream,
    VideoInfo vi,
    PVideoFrame& avs_frame,
    IScriptEnvironment* env
)
{
    /* Variable Frame Rate is not supported yet. */
    int64_t duration_num = vi.fps_denominator;
    int64_t duration_den = vi.fps_numerator;
    bool rgb = vi.IsRGB();
    avs_set_frame_properties(av_frame, stream, duration_num, duration_den, rgb, avs_frame, env);
}

static void prepare_video_decoding
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    int                             direct_rendering,
    enum AVPixelFormat              pixel_format,
    IScriptEnvironment             *env
)
{
    /* Import AVIndexEntrys. */
    if( lwlibav_import_av_index_entry( (lwlibav_decode_handler_t *)vdhp ) < 0 )
        env->ThrowError( "LWLibavVideoSource: failed to import AVIndexEntrys for video." );
    /* Set up output format. */
    lwlibav_video_set_initial_input_format( vdhp );
    AVCodecContext *ctx = lwlibav_video_get_codec_context( vdhp );
    int max_width  = lwlibav_video_get_max_width ( vdhp );
    int max_height = lwlibav_video_get_max_height( vdhp );
    as_setup_video_rendering( vohp, ctx, "LWLibavVideoSource",
                              direct_rendering, pixel_format,
                              max_width, max_height );
    lwlibav_video_set_get_buffer_func( vdhp );
    /* Find the first valid video sample. */
    if( lwlibav_video_find_first_valid_frame( vdhp ) < 0 )
        env->ThrowError( "LWLibavVideoSource: failed to find the first valid video frame." );
    /* Force seeking at the first reading. */
    lwlibav_video_force_seek( vdhp );
}

LWLibavVideoSource::LWLibavVideoSource
(
    lwlibav_option_t   *opt,
    int                 seek_mode,
    uint32_t            forward_seek_threshold,
    int                 direct_rendering,
    enum AVPixelFormat  pixel_format,
    const char         *preferred_decoder_names,
    int                 prefer_hw_decoder,
    IScriptEnvironment *env
) : LWLibavVideoSource{}
{
    memset( &vi,  0, sizeof(VideoInfo) );
    memset( &lwh, 0, sizeof(lwlibav_file_handler_t) );
    lwlibav_video_decode_handler_t *vdhp = this->vdhp.get();
    lwlibav_video_output_handler_t *vohp = this->vohp.get();
    set_preferred_decoder_names( preferred_decoder_names );
    lwlibav_video_set_seek_mode              ( vdhp, seek_mode );
    lwlibav_video_set_forward_seek_threshold ( vdhp, forward_seek_threshold );
    lwlibav_video_set_preferred_decoder_names( vdhp, tokenize_preferred_decoder_names() );
    lwlibav_video_set_prefer_hw_decoder      ( vdhp, prefer_hw_decoder);
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)lw_malloc_zero( sizeof(as_video_output_handler_t) );
    if( !as_vohp )
        env->ThrowError( "LWLibavVideoSource: failed to allocate the AviSynth video output handler." );
    as_vohp->vi  = &vi;
    as_vohp->env = env;
    vohp->private_handler      = as_vohp;
    vohp->free_private_handler = as_free_video_output_handler;
    /* Set up error handler. */
    lw_log_handler_t *lhp = lwlibav_video_get_log_handler( vdhp );
    lhp->level    = LW_LOG_FATAL; /* Ignore other than fatal error. */
    lhp->priv     = env;
    lhp->show_log = throw_error;
    /* Set up progress indicator. */
    progress_indicator_t indicator;
    indicator.open   = NULL;
    indicator.update = update_indicator;
    indicator.close  = close_indicator;
    /* Construct index. */
    int ret = lwlibav_construct_index( &lwh, vdhp, vohp, adhp.get(), aohp.get(), lhp, opt, &indicator, NULL );
    free_audio_decode_handler();
    free_audio_output_handler();
    if( ret < 0 )
        env->ThrowError( "LWLibavVideoSource: failed to construct index." );
    /* Get the desired video track. */
    if( lwlibav_video_get_desired_track( lwh.file_path, vdhp, lwh.threads ) < 0 )
        env->ThrowError( "LWLibavVideoSource: failed to get the video track." );
    /* Set average framerate. */
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    lwlibav_video_setup_timestamp_info( &lwh, vdhp, vohp, &fps_num, &fps_den, opt->apply_repeat_flag );
    vi.fps_numerator   = static_cast<unsigned>(fps_num);
    vi.fps_denominator = static_cast<unsigned>(fps_den);
    vi.num_frames      = vohp->frame_count;
    /* */
    prepare_video_decoding( vdhp, vohp, direct_rendering, pixel_format, env );

    has_at_least_v8 = true;
    try { env->CheckVersion(8); }
    catch (const AvisynthError&) { has_at_least_v8 = false; }

    av_frame = lwlibav_video_get_frame_buffer(vdhp);
    int num = av_frame->sample_aspect_ratio.num;
    int den = av_frame->sample_aspect_ratio.den;
    env->SetVar(env->Sprintf("%s", "FFSAR_NUM"), num);
    env->SetVar(env->Sprintf("%s", "FFSAR_DEN"), den);
    if (num > 0 && den > 0)
        env->SetVar(env->Sprintf("%s", "FFSAR"), num / static_cast<double>(den));
}

LWLibavVideoSource::~LWLibavVideoSource()
{
    lwlibav_video_decode_handler_t *vdhp = this->vdhp.get();
    lw_free( lwlibav_video_get_preferred_decoder_names( vdhp ) );
    lw_free( lwh.file_path );
}

PVideoFrame __stdcall LWLibavVideoSource::GetFrame( int n, IScriptEnvironment *env )
{
    uint32_t frame_number = n + 1;     /* frame_number is 1-origin. */
    lwlibav_video_decode_handler_t *vdhp = this->vdhp.get();
    lwlibav_video_output_handler_t *vohp = this->vohp.get();
    lw_log_handler_t *lhp = lwlibav_video_get_log_handler( vdhp );
    lhp->priv = env;
    if( lwlibav_video_get_error( vdhp )
     || lwlibav_video_get_frame( vdhp, vohp, frame_number ) < 0 )
        return env->NewVideoFrame( vi );
    PVideoFrame as_frame;
    if( make_frame( vohp, av_frame, as_frame, env ) < 0 )
        env->ThrowError( "LWLibavVideoSource: failed to make a frame." );
    if (has_at_least_v8)
        set_frame_properties(av_frame, vdhp->format->streams[vdhp->stream_index], vi, as_frame, env);
    return as_frame;
}

bool __stdcall LWLibavVideoSource::GetParity( int n )
{
    uint32_t frame_number = n + 1;     /* frame_number is 1-origin. */
    lwlibav_video_decode_handler_t *vdhp = this->vdhp.get();
    lwlibav_video_output_handler_t *vohp = this->vohp.get();
    if( !vohp->repeat_control )
        return lwlibav_video_get_field_info( vdhp, frame_number ) == LW_FIELD_INFO_TOP ? true : false;
    uint32_t t = vohp->frame_order_list[frame_number].top;
    uint32_t b = vohp->frame_order_list[frame_number].bottom;
    uint32_t field_number = t < b ? t : b;
    bool top_field_first = lwlibav_video_get_field_info( vdhp, field_number ) == LW_FIELD_INFO_TOP ? true : false;
    if( frame_number <= 2 )
        return vohp->repeat_correction_ts ? !top_field_first : top_field_first;
    uint32_t prev_t = vohp->frame_order_list[frame_number - 1].top;
    uint32_t prev_b = vohp->frame_order_list[frame_number - 1].bottom;
    if( prev_t != prev_b && (field_number == prev_t || field_number == prev_b) )
        return !top_field_first;
    else
        return top_field_first;
}

static void prepare_audio_decoding
(
    lwlibav_audio_decode_handler_t *adhp,
    lwlibav_audio_output_handler_t *aohp,
    uint64_t                        channel_layout,
    int                             sample_rate,
    lwlibav_file_handler_t         &lwh,
    VideoInfo                      &vi,
    IScriptEnvironment             *env
)
{
    lw_log_handler_t *lhp = lwlibav_audio_get_log_handler( adhp );
    lhp->priv = env;
    /* Import AVIndexEntrys. */
    if( lwlibav_import_av_index_entry( (lwlibav_decode_handler_t *)adhp ) < 0 )
        env->ThrowError( "LWLibavAudioSource: failed to import AVIndexEntrys for audio." );
    /* */
    AVCodecContext *ctx = lwlibav_audio_get_codec_context( adhp );
    as_setup_audio_rendering( aohp, ctx, &vi, env, "LWLibavAudioSource", channel_layout, sample_rate );
    /* Count the number of PCM audio samples. */
    vi.num_audio_samples = lwlibav_audio_count_overall_pcm_samples( adhp, aohp->output_sample_rate );
    if( vi.num_audio_samples == 0 )
        env->ThrowError( "LWLibavAudioSource: no valid audio frame." );
    if( lwh.av_gap && aohp->output_sample_rate != ctx->sample_rate )
        lwh.av_gap = ((int64_t)lwh.av_gap * aohp->output_sample_rate - 1) / ctx->sample_rate + 1;
    vi.num_audio_samples += lwh.av_gap;
    /* Force seeking at the first reading. */
    lwlibav_audio_force_seek( adhp );
}

LWLibavAudioSource::LWLibavAudioSource
(
    lwlibav_option_t   *opt,
    uint64_t            channel_layout,
    int                 sample_rate,
    const char         *preferred_decoder_names,
    IScriptEnvironment *env
) : LWLibavAudioSource{}
{
    memset( &vi,  0, sizeof(VideoInfo) );
    memset( &lwh, 0, sizeof(lwlibav_file_handler_t) );
    lwlibav_audio_decode_handler_t *adhp = this->adhp.get();
    lwlibav_audio_output_handler_t *aohp = this->aohp.get();
    set_preferred_decoder_names( preferred_decoder_names );
    lwlibav_audio_set_preferred_decoder_names( adhp, tokenize_preferred_decoder_names() );
    /* Set up error handler. */
    lw_log_handler_t *lhp = lwlibav_audio_get_log_handler( adhp );
    lhp->level    = LW_LOG_FATAL; /* Ignore other than fatal error. */
    lhp->priv     = env;
    lhp->show_log = throw_error;
    /* Set up progress indicator. */
    progress_indicator_t indicator;
    indicator.open   = NULL;
    indicator.update = update_indicator;
    indicator.close  = close_indicator;
    /* Construct index. */
    if( lwlibav_construct_index( &lwh, vdhp.get(), vohp.get(), adhp, aohp, lhp, opt, &indicator, NULL ) < 0 )
        env->ThrowError( "LWLibavAudioSource: failed to get construct index." );
    free_video_decode_handler();
    free_video_output_handler();
    /* Get the desired video track. */
    if( lwlibav_audio_get_desired_track( lwh.file_path, adhp, lwh.threads ) < 0 )
        env->ThrowError( "LWLibavAudioSource: failed to get the audio track." );
    prepare_audio_decoding( adhp, aohp, channel_layout, sample_rate, lwh, vi, env );
}

LWLibavAudioSource::~LWLibavAudioSource()
{
    lwlibav_audio_decode_handler_t *adhp = this->adhp.get();
    lw_free( lwlibav_audio_get_preferred_decoder_names( adhp ) );
    lw_free( lwh.file_path );
}

int LWLibavAudioSource::delay_audio( int64_t *start, int64_t wanted_length )
{
    /* Even if start become negative, its absolute value shall be equal to wanted_length or smaller. */
    int64_t end         = *start + wanted_length;
    int64_t audio_delay = lwh.av_gap;
    if( *start < audio_delay && end <= audio_delay )
    {
        lwlibav_audio_force_seek( adhp.get() ); /* Force seeking at the next access for valid audio frame. */
        return 0;
    }
    *start -= audio_delay;
    return 1;
}

void __stdcall LWLibavAudioSource::GetAudio( void *buf, int64_t start, int64_t wanted_length, IScriptEnvironment *env )
{
    lwlibav_audio_decode_handler_t *adhp = this->adhp.get();
    lwlibav_audio_output_handler_t *aohp = this->aohp.get();
    lw_log_handler_t *lhp = lwlibav_audio_get_log_handler( adhp );
    lhp->priv = env;
    if( delay_audio( &start, wanted_length ) )
        return (void)lwlibav_audio_get_pcm_samples( adhp, aohp, buf, start, wanted_length );
    uint8_t silence = vi.sample_type == SAMPLE_INT8 ? 128 : 0;
    memset( buf, silence, (size_t)(wanted_length * aohp->output_block_align) );
}

static void set_av_log_level( int level )
{
    if( level <= 0 )
        av_log_set_level( AV_LOG_QUIET );
    else if( level == 1 )
        av_log_set_level( AV_LOG_PANIC );
    else if( level == 2 )
        av_log_set_level( AV_LOG_FATAL );
    else if( level == 3 )
        av_log_set_level( AV_LOG_ERROR );
    else if( level == 4 )
        av_log_set_level( AV_LOG_WARNING );
    else if( level == 5 )
        av_log_set_level( AV_LOG_INFO );
    else if( level == 6 )
        av_log_set_level( AV_LOG_VERBOSE );
    else if( level == 7 )
        av_log_set_level( AV_LOG_DEBUG );
    else
        av_log_set_level( AV_LOG_TRACE );
}

AVSValue __cdecl CreateLWLibavVideoSource( AVSValue args, void *user_data, IScriptEnvironment *env )
{
    const char *source                  = args[0].AsString();
    int         stream_index            = args[1].AsInt( -1 );
    int         threads                 = args[2].AsInt( 0 );
    int         no_create_index         = args[3].AsBool( true ) ? 0 : 1;
    const char *index_file_path         = args[4].AsString( nullptr );
    int         seek_mode               = args[5].AsInt( 0 );
    uint32_t    forward_seek_threshold  = args[6].AsInt( 10 );
    int         direct_rendering        = args[7].AsBool( false ) ? 1 : 0;
    int         fps_num                 = args[8].AsInt( 0 );
    int         fps_den                 = args[9].AsInt( 1 );
    int         apply_repeat_flag       = args[10].AsBool( true ) ? 1 : 0;
    int         field_dominance         = args[11].AsInt( 0 );
    enum AVPixelFormat pixel_format     = get_av_output_pixel_format( args[12].AsString( nullptr ) );
    const char *preferred_decoder_names = args[13].AsString( nullptr );
    int         prefer_hw_decoder       = args[14].AsInt( 0 );
    int         ff_loglevel             = args[15].AsInt( 0 );
    /* Set LW-Libav options. */
    lwlibav_option_t opt;
    opt.file_path         = source;
    opt.cache_dir         = NULL; // TODO: add argument?
    opt.threads           = threads >= 0 ? threads : 0;
    opt.av_sync           = 0;
    opt.no_create_index   = no_create_index;
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
    seek_mode              = CLIP_VALUE( seek_mode, 0, 2 );
    forward_seek_threshold = CLIP_VALUE( forward_seek_threshold, 1, 999 );
    direct_rendering      &= (pixel_format == AV_PIX_FMT_NONE);
    prefer_hw_decoder      = CLIP_VALUE( prefer_hw_decoder, 0, 3 );
    set_av_log_level( ff_loglevel );
    return new LWLibavVideoSource( &opt, seek_mode, forward_seek_threshold,
                                   direct_rendering, pixel_format, preferred_decoder_names, prefer_hw_decoder, env );
}

AVSValue __cdecl CreateLWLibavAudioSource( AVSValue args, void *user_data, IScriptEnvironment *env )
{
    const char *source                  = args[0].AsString();
    int         stream_index            = args[1].AsInt( -1 );
    int         no_create_index         = args[2].AsBool( true  ) ? 0 : 1;
    const char *index_file_path         = args[3].AsString( nullptr );
    int         av_sync                 = args[4].AsBool( false ) ? 1 : 0;
    const char *layout_string           = args[5].AsString( nullptr );
    uint32_t    sample_rate             = args[6].AsInt( 0 );
    const char *preferred_decoder_names = args[7].AsString( nullptr );
    int         ff_loglevel             = args[8].AsInt( 0 );
    /* Set LW-Libav options. */
    lwlibav_option_t opt;
    opt.file_path         = source;
    opt.threads           = 0;
    opt.av_sync           = av_sync;
    opt.no_create_index   = no_create_index;
    opt.index_file_path   = index_file_path;
    opt.force_video       = 0;
    opt.force_video_index = -1;
    opt.force_audio       = (stream_index >= 0);
    opt.force_audio_index = stream_index >= 0 ? stream_index : -1;
    opt.apply_repeat_flag = 0;
    opt.field_dominance   = 0;
    opt.vfr2cfr.active    = 0;
    opt.vfr2cfr.fps_num   = 0;
    opt.vfr2cfr.fps_den   = 0;
    uint64_t channel_layout = layout_string ? av_get_channel_layout( layout_string ) : 0;
    set_av_log_level( ff_loglevel );
    return new LWLibavAudioSource( &opt, channel_layout, sample_rate, preferred_decoder_names, env );
}
