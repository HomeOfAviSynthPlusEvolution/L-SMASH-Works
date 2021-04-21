/*****************************************************************************
 * libavsmash_source.cpp
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

/* This file is available under an ISC license.
 * However, when distributing its binary file, it will be under LGPL or GPL. */

#include "../common/cpp_compat.h"

extern "C"
{
/* L-SMASH */
#include <lsmash.h>                 /* Demuxer */

/* Libav
 * The binary file will be LGPLed or GPLed. */
#include <libavformat/avformat.h>       /* Codec specific info importer */
#include <libavcodec/avcodec.h>         /* Decoder */
#include <libswscale/swscale.h>         /* Colorspace converter */
#include <libswresample/swresample.h>   /* Audio resampler */
#include <libavutil/imgutils.h>
}

#include "lsmashsource.h"
#include "video_output.h"
#include "audio_output.h"
#include "libavsmash_source.h"
#include "../common/libavsmash_video_internal.h"

static const char func_name_video_source[] = "LSMASHVideoSource";
static const char func_name_audio_source[] = "LSMASHAudioSource";

uint32_t LSMASHVideoSource::open_file
(
    const char                        *source,
    IScriptEnvironment                *env
)
{
    libavsmash_video_decode_handler_t *vdhp = this->vdhp.get();
    lw_log_handler_t *lhp = libavsmash_video_get_log_handler( vdhp );
    lhp->name     = func_name_video_source;
    lhp->level    = LW_LOG_FATAL;
    lhp->priv     = env;
    lhp->show_log = throw_error;
    lsmash_movie_parameters_t movie_param;
    AVFormatContext *format_ctx = nullptr;
    lsmash_root_t *root = libavsmash_open_file( &format_ctx, source, &file_param, &movie_param, lhp );
    this->format_ctx.reset( format_ctx );
    libavsmash_video_set_root( vdhp, root );
    return movie_param.number_of_tracks;
}

void LSMASHVideoSource::get_video_track
(
    const char                        *source,
    uint32_t                           track_number,
    IScriptEnvironment                *env
)
{
    libavsmash_video_decode_handler_t *vdhp = this->vdhp.get();
    uint32_t number_of_tracks = open_file( source, env );
    if( track_number && track_number > number_of_tracks )
        env->ThrowError( "LSMASHVideoSource: the number of tracks equals %u.", number_of_tracks );
    (void)libavsmash_video_get_track( vdhp, track_number );
}

static int get_composition_duration
(
    libavsmash_video_decode_handler_t* vdhp,
    uint32_t                           composition_sample_number,
    uint32_t                           last_sample_number
)
{
    uint32_t coded_sample_number = libavsmash_video_get_coded_sample_number(vdhp, composition_sample_number);
    if (composition_sample_number == last_sample_number)
        goto no_composition_duration;
    {
        uint32_t next_coded_sample_number = libavsmash_video_get_coded_sample_number(vdhp, composition_sample_number + 1);
        uint64_t      cts;
        uint64_t next_cts;

        if (libavsmash_video_get_cts(vdhp, coded_sample_number, &cts) < 0
            || libavsmash_video_get_cts(vdhp, next_coded_sample_number, &next_cts) < 0)
            goto no_composition_duration;

        if (next_cts <= cts || (next_cts - cts) > INT_MAX)
            return 0;
        return (int)(next_cts - cts);
    }
no_composition_duration:;
    uint32_t sample_duration;
    if (libavsmash_video_get_sample_duration(vdhp, coded_sample_number, &sample_duration) < 0)
        return 0;
    return sample_duration <= INT_MAX ? sample_duration : 0;
}

static void get_sample_duration
(
    libavsmash_video_decode_handler_t* vdhp,
    VideoInfo vi,
    uint32_t sample_number,
    int64_t* duration_num,
    int64_t* duration_den
)
{
    int sample_duration = get_composition_duration(vdhp, sample_number, vi.num_frames);
    if (sample_duration == 0)
    {
        *duration_num = vi.fps_numerator;
        *duration_den = vi.fps_denominator;
    }
    else
    {
        uint32_t media_timescale = libavsmash_video_get_media_timescale(vdhp);
        *duration_num = media_timescale;
        *duration_den = sample_duration;
    }
}

static void set_frame_properties
(
    libavsmash_video_decode_handler_t* vdhp,
    AVFrame* av_frame,
    VideoInfo vi,
    PVideoFrame& avs_frame,
    uint32_t sample_number,
    IScriptEnvironment* env
)
{
    /* Variable Frame Rate is not supported yet. */
    int64_t duration_num;
    int64_t duration_den;
    bool rgb = vi.IsRGB();
    get_sample_duration(vdhp, vi, sample_number, &duration_num, &duration_den);
    avs_set_frame_properties(av_frame, NULL, duration_num, duration_den, rgb, avs_frame, env);
}

static void prepare_video_decoding
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    AVFormatContext                   *format_ctx,
    int                                threads,
    int                                direct_rendering,
    enum AVPixelFormat                 pixel_format,
    VideoInfo                         &vi,
    IScriptEnvironment                *env
)
{
    /* Initialize the video decoder configuration. */
    if( libavsmash_video_initialize_decoder_configuration( vdhp, format_ctx, threads ) < 0 )
        env->ThrowError( "LSMASHVideoSource: failed to initialize the decoder configuration." );
    /* Set up output format. */
    AVCodecContext *ctx = libavsmash_video_get_codec_context( vdhp );
    int max_width  = libavsmash_video_get_max_width ( vdhp );
    int max_height = libavsmash_video_get_max_height( vdhp );
    as_setup_video_rendering( vohp, ctx, "LSMASHVideoSource",
                              direct_rendering, pixel_format,
                              max_width, max_height );
    libavsmash_video_set_get_buffer_func( vdhp );
    /* Calculate average framerate. */
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    libavsmash_video_setup_timestamp_info( vdhp, vohp, &fps_num, &fps_den );
    if( vohp->vfr2cfr )
    {
        if( libavsmash_video_get_error( vdhp ) )
            env->ThrowError( "LSMASHVideoSource: failed to get the minimum CTS of video stream." );
    }
    else
        libavsmash_video_clear_error( vdhp );
    /* Find the first valid video sample. */
    if( libavsmash_video_find_first_valid_frame( vdhp ) < 0 )
        env->ThrowError( "LSMASHVideoSource: failed to find the first valid video frame." );
    /* Setup filter specific info. */
    vi.fps_numerator   = (unsigned int)fps_num;
    vi.fps_denominator = (unsigned int)fps_den;
    vi.num_frames      = vohp->frame_count;
    /* Force seeking at the first reading. */
    libavsmash_video_force_seek( vdhp );
}

LSMASHVideoSource::LSMASHVideoSource
(
    const char         *source,
    uint32_t            track_number,
    int                 threads,
    int                 seek_mode,
    uint32_t            forward_seek_threshold,
    int                 direct_rendering,
    int                 fps_num,
    int                 fps_den,
    enum AVPixelFormat  pixel_format,
    const char         *preferred_decoder_names,
    int                 prefer_hw_decoder,
    IScriptEnvironment *env
) : LSMASHVideoSource{}
{
    memset( &vi,  0, sizeof(VideoInfo) );
    libavsmash_video_decode_handler_t *vdhp = this->vdhp.get();
    libavsmash_video_output_handler_t *vohp = this->vohp.get();
    set_preferred_decoder_names( preferred_decoder_names );
    libavsmash_video_set_seek_mode              ( vdhp, seek_mode );
    libavsmash_video_set_forward_seek_threshold ( vdhp, forward_seek_threshold );
    libavsmash_video_set_preferred_decoder_names( vdhp, tokenize_preferred_decoder_names() );
    libavsmash_video_set_prefer_hw_decoder      ( vdhp, prefer_hw_decoder );
    vohp->vfr2cfr = (fps_num > 0 && fps_den > 0);
    vohp->cfr_num = (uint32_t)fps_num;
    vohp->cfr_den = (uint32_t)fps_den;
    as_video_output_handler_t *as_vohp = (as_video_output_handler_t *)lw_malloc_zero( sizeof(as_video_output_handler_t) );
    if( as_vohp == nullptr )
        env->ThrowError( "LSMASHVideoSource: failed to allocate the AviSynth video output handler." );
    as_vohp->vi  = &vi;
    as_vohp->env = env;
    vohp->private_handler      = as_vohp;
    vohp->free_private_handler = as_free_video_output_handler;
    get_video_track( source, track_number, env );
    prepare_video_decoding( vdhp, vohp, format_ctx.get(), threads, direct_rendering, pixel_format, vi, env );
    lsmash_discard_boxes( libavsmash_video_get_root( vdhp ) );

    has_at_least_v8 = true;
    try { env->CheckVersion(8); }
    catch (const AvisynthError&) { has_at_least_v8 = false; }

    av_frame = libavsmash_video_get_frame_buffer(vdhp);
    int num = av_frame->sample_aspect_ratio.num;
    int den = av_frame->sample_aspect_ratio.den;
    env->SetVar(env->Sprintf("%s", "FFSAR_NUM"), num);
    env->SetVar(env->Sprintf("%s", "FFSAR_DEN"), den);
    if (num > 0 && den > 0)
        env->SetVar(env->Sprintf("%s", "FFSAR"), num / static_cast<double>(den));
}

LSMASHVideoSource::~LSMASHVideoSource()
{
    libavsmash_video_decode_handler_t *vdhp = this->vdhp.get();
    lsmash_root_t *root = libavsmash_video_get_root( vdhp );
    lw_free( libavsmash_video_get_preferred_decoder_names( vdhp ) );
    lsmash_close_file( &file_param );
    lsmash_destroy_root( root );
}

PVideoFrame __stdcall LSMASHVideoSource::GetFrame( int n, IScriptEnvironment *env )
{
    uint32_t sample_number = n + 1;     /* For L-SMASH, sample_number is 1-origin. */
    libavsmash_video_decode_handler_t *vdhp = this->vdhp.get();
    libavsmash_video_output_handler_t *vohp = this->vohp.get();
    lw_log_handler_t *lhp = libavsmash_video_get_log_handler( vdhp );
    lhp->priv = env;
    if( libavsmash_video_get_error( vdhp )
     || libavsmash_video_get_frame( vdhp, vohp, sample_number ) < 0 )
        return env->NewVideoFrame( vi );
    PVideoFrame as_frame;
    if( make_frame( vohp, av_frame, as_frame, env ) < 0 )
        env->ThrowError( "LSMASHVideoSource: failed to make a frame." );
    if (has_at_least_v8)
        set_frame_properties(vdhp, av_frame,vi, as_frame, sample_number, env);
    return as_frame;
}

uint32_t LSMASHAudioSource::open_file( const char *source, IScriptEnvironment *env )
{
    libavsmash_audio_decode_handler_t *adhp = this->adhp.get();
    lw_log_handler_t *lhp = libavsmash_audio_get_log_handler( adhp );
    lhp->name     = func_name_audio_source;
    lhp->level    = LW_LOG_FATAL;
    lhp->priv     = env;
    lhp->show_log = throw_error;
    lsmash_movie_parameters_t movie_param;
    AVFormatContext *format_ctx = nullptr;
    lsmash_root_t *root = libavsmash_open_file( &format_ctx, source, &file_param, &movie_param, lhp );
    this->format_ctx.reset( format_ctx );
    libavsmash_audio_set_root( adhp, root );
    return movie_param.number_of_tracks;
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

static char *duplicate_as_string( void *src, size_t length )
{
    char *dst = new char[length + 1];
    if( !dst )
        return nullptr;
    memcpy( dst, src, length );
    dst[length] = '\0';
    return dst;
}

void LSMASHAudioSource::get_audio_track( const char *source, uint32_t track_number, IScriptEnvironment *env )
{
    libavsmash_audio_decode_handler_t *adhp = this->adhp.get();
    uint32_t number_of_tracks = open_file( source, env );
    if( track_number && track_number > number_of_tracks )
        env->ThrowError( "LSMASHAudioSource: the number of tracks equals %u.", number_of_tracks );
    /* L-SMASH */
    (void)libavsmash_audio_get_track( adhp, track_number );
}

static void count_output_audio_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    libavsmash_audio_output_handler_t *aohp,
    bool                               skip_priming,
    VideoInfo                         &vi,
    IScriptEnvironment                *env
)
{
    lsmash_root_t *root = libavsmash_audio_get_root( adhp );
    uint32_t track_id   = libavsmash_audio_get_track_id( adhp );
    uint64_t start_time = 0;
    if( skip_priming )
    {
        uint32_t media_timescale = libavsmash_audio_get_media_timescale( adhp );
        uint32_t itunes_metadata_count = lsmash_count_itunes_metadata( root );
        for( uint32_t i = 1; i <= itunes_metadata_count; i++ )
        {
            lsmash_itunes_metadata_t metadata;
            if( lsmash_get_itunes_metadata( root, i, &metadata ) < 0 )
                continue;
            if( metadata.item != ITUNES_METADATA_ITEM_CUSTOM
             || (metadata.type != ITUNES_METADATA_TYPE_STRING && metadata.type != ITUNES_METADATA_TYPE_BINARY)
             || !metadata.meaning || !metadata.name
             || memcmp( "com.apple.iTunes", metadata.meaning, strlen( metadata.meaning ) )
             || memcmp( "iTunSMPB", metadata.name, strlen( metadata.name ) ) )
            {
                lsmash_cleanup_itunes_metadata( &metadata );
                continue;
            }
            char *value = nullptr;
            if( metadata.type == ITUNES_METADATA_TYPE_STRING )
            {
                size_t length = strlen( metadata.value.string );
                if( length >= 116 )
                    value = duplicate_as_string( metadata.value.string, length );
            }
            else    /* metadata.type == ITUNES_METADATA_TYPE_BINARY */
            {
                if( metadata.value.binary.size >= 116 )
                    value = duplicate_as_string( metadata.value.binary.data, metadata.value.binary.size );
            }
            lsmash_cleanup_itunes_metadata( &metadata );
            if( !value )
                continue;
            uint32_t dummy[9];
            uint32_t priming_samples;
            uint32_t padding;
            uint64_t duration;
            if( 12 != sscanf( value, " %x %x %x %" SCNx64 " %x %x %x %x %x %x %x %x",
                              &dummy[0], &priming_samples, &padding, &duration, &dummy[1], &dummy[2],
                              &dummy[3], &dummy[4], &dummy[5], &dummy[6], &dummy[7], &dummy[8] ) )
            {
                delete [] value;
                continue;
            }
            delete [] value;
            libavsmash_audio_set_implicit_preroll( adhp );
            start_time = av_rescale( priming_samples, media_timescale, aohp->output_sample_rate );
            aohp->skip_decoded_samples = priming_samples;
            // vi.num_audio_samples = duration + priming_samples;
            break;
        }
        if( aohp->skip_decoded_samples == 0 )
        {
            uint32_t ctd_shift;
            if( lsmash_get_composition_to_decode_shift_from_media_timeline( root, track_id, &ctd_shift ) )
                env->ThrowError( "LSMASHAudioSource: failed to get the timeline shift." );
            start_time = ctd_shift + get_start_time( root, track_id );
            aohp->skip_decoded_samples = av_rescale( start_time, aohp->output_sample_rate, media_timescale );
        }
    }
    vi.num_audio_samples = libavsmash_audio_count_overall_pcm_samples( adhp, aohp->output_sample_rate, start_time );
    if( vi.num_audio_samples == 0 )
        env->ThrowError( "LSMASHAudioSource: no valid audio frame." );
}

static void prepare_audio_decoding
(
    libavsmash_audio_decode_handler_t *adhp,
    libavsmash_audio_output_handler_t *aohp,
    AVFormatContext                   *format_ctx,
    uint64_t                           channel_layout,
    int                                sample_rate,
    bool                               skip_priming,
    VideoInfo                         &vi,
    IScriptEnvironment                *env
)
{
    /* Initialize the audio decoder configuration. */
    if( libavsmash_audio_initialize_decoder_configuration( adhp, format_ctx, 0 ) < 0 )
        env->ThrowError( "LSMASHAudioSource: failed to initialize the decoder configuration." );
    aohp->output_channel_layout  = libavsmash_audio_get_best_used_channel_layout ( adhp );
    aohp->output_sample_format   = libavsmash_audio_get_best_used_sample_format  ( adhp );
    aohp->output_sample_rate     = libavsmash_audio_get_best_used_sample_rate    ( adhp );
    aohp->output_bits_per_sample = libavsmash_audio_get_best_used_bits_per_sample( adhp );
    AVCodecContext *ctx = libavsmash_audio_get_codec_context( adhp );
    as_setup_audio_rendering( aohp, ctx, &vi, env, "LSMASHAudioSource", channel_layout, sample_rate );
    count_output_audio_samples( adhp, aohp, skip_priming, vi, env );
    /* Force seeking at the first reading. */
    libavsmash_audio_force_seek( adhp );
}

LSMASHAudioSource::LSMASHAudioSource
(
    const char         *source,
    uint32_t            track_number,
    bool                skip_priming,
    uint64_t            channel_layout,
    int                 sample_rate,
    const char         *preferred_decoder_names,
    IScriptEnvironment *env
) : LSMASHAudioSource{}
{
    memset( &vi,  0, sizeof(VideoInfo) );
    libavsmash_audio_decode_handler_t *adhp = this->adhp.get();
    libavsmash_audio_output_handler_t *aohp = this->aohp.get();
    set_preferred_decoder_names( preferred_decoder_names );
    libavsmash_audio_set_preferred_decoder_names( adhp, tokenize_preferred_decoder_names() );
    get_audio_track( source, track_number, env );
    prepare_audio_decoding( adhp, aohp, format_ctx.get(), channel_layout, sample_rate, skip_priming, vi, env );
    lsmash_discard_boxes( libavsmash_audio_get_root( adhp ) );
}

LSMASHAudioSource::~LSMASHAudioSource()
{
    libavsmash_audio_decode_handler_t *adhp = this->adhp.get();
    lsmash_root_t *root = libavsmash_audio_get_root( adhp );
    lw_free( libavsmash_audio_get_preferred_decoder_names( adhp ) );
    lsmash_close_file( &file_param );
    lsmash_destroy_root( root );
}

void __stdcall LSMASHAudioSource::GetAudio( void *buf, int64_t start, int64_t wanted_length, IScriptEnvironment *env )
{
    libavsmash_audio_decode_handler_t *adhp = this->adhp.get();
    libavsmash_audio_output_handler_t *aohp = this->aohp.get();
    lw_log_handler_t *lhp = libavsmash_audio_get_log_handler( adhp );
    lhp->priv = env;
    return (void)libavsmash_audio_get_pcm_samples( adhp, aohp, buf, start, wanted_length );
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

AVSValue __cdecl CreateLSMASHVideoSource( AVSValue args, void *user_data, IScriptEnvironment *env )
{
    const char *source                  = args[0].AsString();
    uint32_t    track_number            = args[1].AsInt( 0 );
    int         threads                 = args[2].AsInt( 0 );
    int         seek_mode               = args[3].AsInt( 0 );
    uint32_t    forward_seek_threshold  = args[4].AsInt( 10 );
    int         direct_rendering        = args[5].AsBool( false ) ? 1 : 0;
    int         fps_num                 = args[6].AsInt( 0 );
    int         fps_den                 = args[7].AsInt( 1 );
    enum AVPixelFormat pixel_format     = get_av_output_pixel_format( args[8].AsString( nullptr ) );
    const char *preferred_decoder_names = args[9].AsString( nullptr );
    int         prefer_hw_decoder       = args[10].AsInt( 0 );
    int         ff_loglevel             = args[11].AsInt( 0 );
    threads                = threads >= 0 ? threads : 0;
    seek_mode              = CLIP_VALUE( seek_mode, 0, 2 );
    forward_seek_threshold = CLIP_VALUE( forward_seek_threshold, 1, 999 );
    direct_rendering      &= (pixel_format == AV_PIX_FMT_NONE);
    prefer_hw_decoder      = CLIP_VALUE( prefer_hw_decoder, 0, 3 );
    set_av_log_level( ff_loglevel );
    return new LSMASHVideoSource( source, track_number, threads, seek_mode, forward_seek_threshold,
                                  direct_rendering, fps_num, fps_den, pixel_format, preferred_decoder_names, prefer_hw_decoder, env );
}

AVSValue __cdecl CreateLSMASHAudioSource( AVSValue args, void *user_data, IScriptEnvironment *env )
{
    const char *source                  = args[0].AsString();
    uint32_t    track_number            = args[1].AsInt( 0 );
    bool        skip_priming            = args[2].AsBool( true );
    const char *layout_string           = args[3].AsString( nullptr );
    int         sample_rate             = args[4].AsInt( 0 );
    const char *preferred_decoder_names = args[5].AsString( nullptr );
    int         ff_loglevel             = args[6].AsInt( 0 );
    uint64_t channel_layout = layout_string ? av_get_channel_layout( layout_string ) : 0;
    set_av_log_level( ff_loglevel );
    return new LSMASHAudioSource( source, track_number, skip_priming,
                                  channel_layout, sample_rate, preferred_decoder_names, env );
}
