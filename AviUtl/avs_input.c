/********************************************************************************
 * avs_input.c
 ********************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: Oka Motofumi <chikuzen.mo@gmail.com>
 *          Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

/* This file is available under an MIT license.
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "lwinput.h"

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include <avisynth_c.h>

#define AVS_INTERFACE_25 2

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "video_output.h"

#if (LIBAVUTIL_VERSION_MICRO >= 100) && (LIBSWSCALE_VERSION_MICRO >= 100)
#define FFMPEG_HIGH_DEPTH_SUPPORT 1
#else
#define FFMPEG_HIGH_DEPTH_SUPPORT 0
#endif

typedef struct {
    AVS_Clip              *clip;
    AVS_ScriptEnvironment *env;
    const AVS_VideoInfo   *vi;
    HMODULE                library;
    struct
    {
#define AVSC_DECLARE_FUNC( name ) name##_func name
        AVSC_DECLARE_FUNC( avs_clip_get_error );
        AVSC_DECLARE_FUNC( avs_create_script_environment );
        AVSC_DECLARE_FUNC( avs_delete_script_environment );
        AVSC_DECLARE_FUNC( avs_get_error );
        AVSC_DECLARE_FUNC( avs_get_frame );
        AVSC_DECLARE_FUNC( avs_get_audio );
        AVSC_DECLARE_FUNC( avs_get_video_info );
        AVSC_DECLARE_FUNC( avs_invoke );
        AVSC_DECLARE_FUNC( avs_release_clip );
        AVSC_DECLARE_FUNC( avs_release_value );
        AVSC_DECLARE_FUNC( avs_release_video_frame );
        AVSC_DECLARE_FUNC( avs_take_clip );
        AVSC_DECLARE_FUNC( avs_get_pitch_p );
        AVSC_DECLARE_FUNC( avs_get_read_ptr_p );
#undef AVSC_DECLARE_FUNC
    } func;
    /* Video stuff */
    AVFrame                  *av_frame;
    lw_video_output_handler_t voh;
    int                       bit_depth;
} avs_handler_t;

static int load_avisynth_dll( avs_handler_t *hp )
{
#define LOAD_AVS_FUNC( name, ignore_fail )                                 \
    do                                                                     \
    {                                                                      \
        hp->func.name = (name##_func)GetProcAddress( hp->library, #name ); \
        if( !ignore_fail && !hp->func.name )                               \
            goto fail;                                                     \
    } while( 0 )
    hp->library = LoadLibrary( "avisynth" );
    if( !hp->library )
        return -1;
    LOAD_AVS_FUNC( avs_clip_get_error,            0 );
    LOAD_AVS_FUNC( avs_create_script_environment, 0 );
    LOAD_AVS_FUNC( avs_delete_script_environment, 1 );
    LOAD_AVS_FUNC( avs_get_error,                 1 );
    LOAD_AVS_FUNC( avs_get_frame,                 0 );
    LOAD_AVS_FUNC( avs_get_audio,                 0 );
    LOAD_AVS_FUNC( avs_get_video_info,            0 );
    LOAD_AVS_FUNC( avs_invoke,                    0 );
    LOAD_AVS_FUNC( avs_release_clip,              0 );
    LOAD_AVS_FUNC( avs_release_value,             0 );
    LOAD_AVS_FUNC( avs_release_video_frame,       0 );
    LOAD_AVS_FUNC( avs_take_clip,                 0 );
    LOAD_AVS_FUNC( avs_get_pitch_p,               0 );
    LOAD_AVS_FUNC( avs_get_read_ptr_p,            0 );
    return 0;
fail:
    FreeLibrary( hp->library );
    return -1;
#undef LOAD_AVS_FUNC
}

static AVS_Value invoke_filter( avs_handler_t *hp, AVS_Value before, const char *filter )
{
    hp->func.avs_release_clip( hp->clip );
    AVS_Value after = hp->func.avs_invoke( hp->env, filter, before, NULL );
    hp->func.avs_release_value( before );
    hp->clip = hp->func.avs_take_clip( after, hp->env );
    hp->vi   = hp->func.avs_get_video_info( hp->clip );
    return after;
}

static AVS_Value initialize_avisynth( avs_handler_t *hp, char *input )
{
    if( load_avisynth_dll( hp ) )
        return avs_void;
    hp->env = hp->func.avs_create_script_environment( AVS_INTERFACE_25 );
    if( hp->func.avs_get_error && hp->func.avs_get_error( hp->env ) )
        return avs_void;
    AVS_Value arg = avs_new_value_string( input );
    AVS_Value res = hp->func.avs_invoke( hp->env, "Import", arg, NULL );
    if( avs_is_error( res ) )
        return res;
    AVS_Value mt_test = hp->func.avs_invoke( hp->env, "GetMTMode", avs_new_value_bool( 0 ), NULL );
    int mt_mode = avs_is_int( mt_test ) ? avs_as_int( mt_test ) : 0;
    hp->func.avs_release_value( mt_test );
    if( mt_mode > 0 && mt_mode < 5 )
    {
        AVS_Value temp = hp->func.avs_invoke( hp->env, "Distributor", res, NULL );
        hp->func.avs_release_value( res );
        res = temp;
    }
    hp->clip = hp->func.avs_take_clip( res, hp->env );
    hp->vi   = hp->func.avs_get_video_info( hp->clip );
    if( hp->vi->sample_type & AVS_SAMPLE_FLOAT )
        res = invoke_filter( hp, res, "ConvertAudioTo16bit" );
    return res;
}

static void close_avisynth_dll( avs_handler_t *hp )
{
    if( hp->clip )
        hp->func.avs_release_clip( hp->clip );
    if( hp->func.avs_delete_script_environment )
        hp->func.avs_delete_script_environment( hp->env );
    FreeLibrary( hp->library );
}

static enum AVPixelFormat as_to_av_input_pixel_format
(
    int  as_input_pixel_format,
    int *as_input_bit_depth,
    int *input_width
)
{
    if( *as_input_bit_depth > 8 && (*input_width & 1) )
    {
        UINT uType = MB_ICONERROR | MB_OK;
        lw_log_handler_t lh = { 0 };
        lh.level = LW_LOG_WARNING;
        lh.priv  = &uType;
        au_message_box_desktop( &lh, LW_LOG_WARNING, "Width of interleaved fake high bit-depth format must be mod2." );
        /* Treat as 8-bit depth. */
        *as_input_bit_depth = 8;
    }

    typedef struct
    {
        int                as_input_pixel_format;
        enum AVPixelFormat av_input_pixel_format;
        int                as_input_bit_depth;
    } format_table_t;

    const format_table_t std_format_table[] =
        {
            { AVS_CS_I420,      AV_PIX_FMT_YUV420P,      8 },
            { AVS_CS_I420,      AV_PIX_FMT_YUV420P9LE,   9 },
            { AVS_CS_I420,      AV_PIX_FMT_YUV420P10LE, 10 },
            { AVS_CS_I420,      AV_PIX_FMT_YUV420P16LE, 16 },
            { AVS_CS_YV12,      AV_PIX_FMT_YUV420P,      8 },
            { AVS_CS_YV12,      AV_PIX_FMT_YUV420P9LE,   9 },
            { AVS_CS_YV12,      AV_PIX_FMT_YUV420P10LE, 10 },
            { AVS_CS_YV12,      AV_PIX_FMT_YUV420P16LE, 16 },
            { AVS_CS_YV16,      AV_PIX_FMT_YUV422P,      8 },
            { AVS_CS_YV16,      AV_PIX_FMT_YUV422P9LE,   9 },
            { AVS_CS_YV16,      AV_PIX_FMT_YUV422P10LE, 10 },
            { AVS_CS_YV16,      AV_PIX_FMT_YUV422P16LE, 16 },
            { AVS_CS_YV24,      AV_PIX_FMT_YUV444P,      8 },
            { AVS_CS_YV24,      AV_PIX_FMT_YUV444P9LE,   9 },
            { AVS_CS_YV24,      AV_PIX_FMT_YUV444P10LE, 10 },
            { AVS_CS_YV24,      AV_PIX_FMT_YUV444P16LE, 16 },
            { AVS_CS_YUV9,      AV_PIX_FMT_YUV410P,      8 },
            { AVS_CS_YV411,     AV_PIX_FMT_YUV411P,      8 },
            { AVS_CS_BGR24,     AV_PIX_FMT_BGR24,        8 },
            { AVS_CS_BGR24,     AV_PIX_FMT_BGR48LE,     16 },
            { AVS_CS_BGR32,     AV_PIX_FMT_BGRA,         8 },
            { AVS_CS_YUY2,      AV_PIX_FMT_YUYV422,      8 },
            { AVS_CS_Y8,        AV_PIX_FMT_GRAY8,        8 },
            { AVS_CS_Y8,        AV_PIX_FMT_GRAY16LE,    16 },
            { AVS_CS_UNKNOWN,   AV_PIX_FMT_NONE,         0 }
        };

    const format_table_t ext_format_table[] =
        {
            { AVS_CS_YUV420P10, AV_PIX_FMT_YUV420P10LE, 10 },
            { AVS_CS_YUV420P16, AV_PIX_FMT_YUV420P16LE, 16 },
            { AVS_CS_YUV422P10, AV_PIX_FMT_YUV422P10LE, 10 },
            { AVS_CS_YUV422P16, AV_PIX_FMT_YUV422P16LE, 16 },
            { AVS_CS_YUV444P10, AV_PIX_FMT_YUV444P10LE, 10 },
            { AVS_CS_YUV444P16, AV_PIX_FMT_YUV444P16LE, 16 },
#if FFMPEG_HIGH_DEPTH_SUPPORT
            { AVS_CS_YUV420P12, AV_PIX_FMT_YUV420P12LE, 12 },
            { AVS_CS_YUV420P14, AV_PIX_FMT_YUV420P14LE, 14 },
            { AVS_CS_YUV422P12, AV_PIX_FMT_YUV422P12LE, 12 },
            { AVS_CS_YUV422P14, AV_PIX_FMT_YUV422P14LE, 14 },
            { AVS_CS_YUV444P12, AV_PIX_FMT_YUV444P12LE, 12 },
            { AVS_CS_YUV444P14, AV_PIX_FMT_YUV444P14LE, 14 },
#endif
            { AVS_CS_UNKNOWN,   AV_PIX_FMT_NONE,         0 }
        };

    for( int i = 0; std_format_table[i].as_input_pixel_format != AVS_CS_UNKNOWN; i++ )
        if( as_input_pixel_format == std_format_table[i].as_input_pixel_format
         && *as_input_bit_depth   == std_format_table[i].as_input_bit_depth )
        {
            if( *as_input_bit_depth > 8 )
                *input_width >>= 1;
            return std_format_table[i].av_input_pixel_format;
        }

    for( int i = 0; ext_format_table[i].as_input_pixel_format != AVS_CS_UNKNOWN; i++ )
        if( as_input_pixel_format == ext_format_table[i].as_input_pixel_format )
        {
            *as_input_bit_depth = ext_format_table[i].as_input_bit_depth;
            return ext_format_table[i].av_input_pixel_format;
        }

    return AV_PIX_FMT_NONE;
}

static int prepare_video_decoding( lsmash_handler_t *h, video_option_t *opt )
{
    avs_handler_t *hp = (avs_handler_t *)h->video_private;
    h->video_sample_count = hp->vi->num_frames;
    h->framerate_num      = hp->vi->fps_numerator;
    h->framerate_den      = hp->vi->fps_denominator;
    /* Set up video rendering. */
    int input_width = hp->vi->width;
    enum AVPixelFormat input_pixel_format = as_to_av_input_pixel_format( hp->vi->pixel_type, &opt->avs.bit_depth, &input_width );
    return au_setup_video_rendering( &hp->voh, opt, &h->video_format, input_width, hp->vi->height, input_pixel_format );
}

static int prepare_audio_decoding( lsmash_handler_t *h, audio_option_t *opt )
{
    avs_handler_t *hp = (avs_handler_t *)h->audio_private;
    h->audio_pcm_sample_count = hp->vi->num_audio_samples;
    /* Support of WAVEFORMATEXTENSIBLE is much restrictive on AviUtl, so we always use WAVEFORMATEX instead. */
    WAVEFORMATEX *Format = &h->audio_format.Format;
    Format->nChannels       = hp->vi->nchannels;
    Format->nSamplesPerSec  = hp->vi->audio_samples_per_second;
    Format->wBitsPerSample  = avs_bytes_per_channel_sample( hp->vi ) * 8;
    Format->nBlockAlign     = avs_bytes_per_audio_sample( hp->vi );
    Format->nAvgBytesPerSec = Format->nSamplesPerSec * Format->nBlockAlign;
    Format->wFormatTag      = WAVE_FORMAT_PCM;
    Format->cbSize          = 0;
    return 0;
}

static void *open_file( char *file_name, reader_option_t *opt )
{
    /* Check file extension. */
    if( lw_check_file_extension( file_name, "avs" ) < 0 )
        return NULL;
    /* Try to open the file as avisynth script. */
    avs_handler_t *hp = lw_malloc_zero( sizeof(avs_handler_t) );
    if( !hp )
        return NULL;
    AVS_Value res = initialize_avisynth( hp, file_name );
    if( !avs_is_clip( res ) )
    {
        if( hp->library )
            close_avisynth_dll( hp );
        lw_free( hp );
        return NULL;
    }
    hp->func.avs_release_value( res );
    return hp;
}

static int get_video_track( lsmash_handler_t *h, video_option_t *opt )
{
    avs_handler_t *hp = (avs_handler_t *)h->video_private;
    if( hp->vi->num_frames <= 0 || hp->vi->width <= 0 || hp->vi->height <= 0 )
        return -1;
    hp->av_frame = av_frame_alloc();
    if( !hp->av_frame )
        return -1;
    hp->bit_depth = opt->avs.bit_depth;
    return prepare_video_decoding( h, opt );
}

static int get_audio_track( lsmash_handler_t *h, audio_option_t *opt )
{
    avs_handler_t *hp = (avs_handler_t *)h->audio_private;
    if( hp->vi->num_audio_samples <= 0 )
        return -1;
    return prepare_audio_decoding( h, opt );
}

static int read_video( lsmash_handler_t *h, int sample_number, void *buf )
{
    avs_handler_t *hp = (avs_handler_t *)h->video_private;
    AVS_VideoFrame *as_frame = hp->func.avs_get_frame( hp->clip, sample_number );
    if( hp->func.avs_clip_get_error( hp->clip ) )
        return 0;
    if( hp->vi->pixel_type & AVS_CS_INTERLEAVED )
    {
        uint8_t *read_ptr = (uint8_t *)hp->func.avs_get_read_ptr_p( as_frame, 0 );
        int      pitch    = hp->func.avs_get_pitch_p( as_frame, 0 );
        if( avs_is_rgb( hp->vi ) )
        {
            hp->av_frame->data    [0] = read_ptr + pitch * (hp->vi->height - 1);
            hp->av_frame->linesize[0] = -pitch;
        }
        else
        {
            hp->av_frame->data    [0] = read_ptr;
            hp->av_frame->linesize[0] = pitch;
        }
    }
    else
        for( int i = 0; i < 3; i++ )
        {
            static const int as_plane[3] = { AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V };
            hp->av_frame->data    [i] = (uint8_t *)hp->func.avs_get_read_ptr_p( as_frame, as_plane[i] );
            hp->av_frame->linesize[i] = hp->func.avs_get_pitch_p( as_frame, as_plane[i] );
        }
    hp->av_frame->width       = hp->vi->width;
    hp->av_frame->height      = hp->vi->height;
    hp->av_frame->format      = as_to_av_input_pixel_format( hp->vi->pixel_type, &hp->bit_depth, &hp->av_frame->width );
    hp->av_frame->color_range = AVCOL_RANGE_UNSPECIFIED;
    hp->av_frame->colorspace  = AVCOL_SPC_UNSPECIFIED;
    /* Here, update_scaler_configuration_if_needed() is required to activate the scaler. */
    if( update_scaler_configuration_if_needed( &hp->voh.scaler, NULL, hp->av_frame ) < 0 )
        return 0;
    int frame_size = convert_colorspace( &hp->voh, hp->av_frame, buf );
    hp->func.avs_release_video_frame( as_frame );
    return frame_size;
}

static int read_audio( lsmash_handler_t *h, int start, int wanted_length, void *buf )
{
    avs_handler_t *hp = (avs_handler_t *)h->audio_private;
    hp->func.avs_get_audio( hp->clip, buf, start, wanted_length );
    return wanted_length;
}

static int delay_audio( lsmash_handler_t *h, int *start, int wanted_length, int audio_delay )
{
    if( *start < audio_delay )
    {
        if( *start + wanted_length < audio_delay )
            return 0;
        *start = audio_delay - *start;
    }
    else
        *start -= audio_delay;
    return 1;
}

static void video_cleanup
(
    lsmash_handler_t *h
)
{
    avs_handler_t *hp = (avs_handler_t *)h->video_private;
    if( !hp )
        return;
    av_frame_free( &hp->av_frame );
    lw_cleanup_video_output_handler( &hp->voh );
}

static void close_file( void *private_stuff )
{
    avs_handler_t *hp = (avs_handler_t *)private_stuff;
    if( !hp )
        return;
    if( hp->library )
        close_avisynth_dll( hp );
    lw_free( hp );
}

lsmash_reader_t avs_reader =
{
    AVS_READER,
    open_file,
    get_video_track,
    get_audio_track,
    NULL,
    read_video,
    read_audio,
    NULL,
    delay_audio,
    video_cleanup,
    NULL,
    close_file
};
