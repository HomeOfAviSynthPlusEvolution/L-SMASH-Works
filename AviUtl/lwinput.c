/*****************************************************************************
 * lwinput.c
 *****************************************************************************
 * Copyright (C) 2011-2015 L-SMASH Works project
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
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "lwinput.h"
#include "resource.h"

#include "config.h"

#include <commctrl.h>

#include <libavutil/channel_layout.h>
/* Version */
#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
#include <libswscale/version.h>
#include <libswresample/version.h>
/* License */
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define MAX_AUTO_NUM_THREADS 16

#define MPEG4_FILE_EXT      "*.mp4;*.m4v;*.m4a;*.mov;*.qt;*.3gp;*.3g2;*.f4v;*.ismv;*.isma"
#define INDEX_FILE_EXT      "*.lwi"
#define ANY_FILE_EXT        "*.*"

static char plugin_information[512] = { 0 };

static void get_plugin_information( void )
{
    sprintf( plugin_information,
             "L-SMASH Works File Reader r%s\n"
             "    libavutil %s: %s / libavcodec %s: %s\n"
             "    libavformat %s: %s / libswscale %s: %s\n"
             "    libavresample %s: %s",
             LSMASHWORKS_REV,
             AV_STRINGIFY( LIBAVUTIL_VERSION     ), avutil_license    (),
             AV_STRINGIFY( LIBAVCODEC_VERSION    ), avcodec_license   (),
             AV_STRINGIFY( LIBAVFORMAT_VERSION   ), avformat_license  (),
             AV_STRINGIFY( LIBSWSCALE_VERSION    ), swscale_license   (),
             AV_STRINGIFY( LIBSWRESAMPLE_VERSION ), swresample_license() );
}

INPUT_PLUGIN_TABLE input_plugin_table =
{
    INPUT_PLUGIN_FLAG_VIDEO | INPUT_PLUGIN_FLAG_AUDIO,              /* INPUT_PLUGIN_FLAG_VIDEO : support images
                                                                     * INPUT_PLUGIN_FLAG_AUDIO : support audio */
    "L-SMASH Works File Reader",                                    /* Name of plugin */
    "MPEG-4 File (" MPEG4_FILE_EXT ")\0" MPEG4_FILE_EXT "\0"        /* Filter for Input file */
    "LW-Libav Index File (" INDEX_FILE_EXT ")\0" INDEX_FILE_EXT "\0"
    "Any File (" ANY_FILE_EXT ")\0" ANY_FILE_EXT "\0",
    "L-SMASH Works File Reader r" LSMASHWORKS_REV "\0",             /* Information of plugin */
    NULL,                                                           /* Pointer to function called when opening DLL (If NULL, won't be called.) */
    NULL,                                                           /* Pointer to function called when closing DLL (If NULL, won't be called.) */
    func_open,                                                      /* Pointer to function to open input file */
    func_close,                                                     /* Pointer to function to close input file */
    func_info_get,                                                  /* Pointer to function to get information of input file */
    func_read_video,                                                /* Pointer to function to read image data */
    func_read_audio,                                                /* Pointer to function to read audio data */
    func_is_keyframe,                                               /* Pointer to function to check if it is a keyframe or not (If NULL, all is keyframe.) */
    func_config,                                                    /* Pointer to function called when configuration dialog is required */
};

EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable( void )
{
    return &input_plugin_table;
}

static reader_option_t reader_opt = { 0 };
static video_option_t *video_opt = &reader_opt.video_opt;
static audio_option_t *audio_opt = &reader_opt.audio_opt;
static int reader_disabled[5] = { 0 };
static int audio_delay = 0;
static char *settings_path = NULL;
static const char *settings_path_list[] = { "lsmash.ini", "plugins/lsmash.ini" };
static const char *seek_mode_list[] = { "Normal", "Unsafe", "Aggressive" };
static const char *dummy_colorspace_list[] = { "YUY2", "RGB", "YC48" };
static const char *scaler_list[] = { "Fast bilinear", "Bilinear", "Bicubic", "Experimental", "Nearest neighbor", "Area averaging",
                                     "L-bicubic/C-bilinear", "Gaussian", "Sinc", "Lanczos", "Bicubic spline" };
static const char *field_dominance_list[] = { "Obey source flags", "Top -> Bottom", "Bottom -> Top" };
static const char *avs_bit_depth_list[] = { "8", "9", "10", "16" };

void au_message_box_desktop
(
    lw_log_handler_t *lhp,
    lw_log_level      level,
    const char       *message
)
{
    UINT uType = *(UINT *)lhp->priv;
    MessageBox( HWND_DESKTOP, message, "lwinput", uType );
}

static FILE *open_settings( void )
{
    FILE *ini = NULL;
    for( int i = 0; i < 2; i++ )
    {
        ini = fopen( settings_path_list[i], "rb" );
        if( ini )
        {
            settings_path = (char *)settings_path_list[i];
            return ini;
        }
    }
    return NULL;
}

static int get_auto_threads( void )
{
    int n = atoi( getenv( "NUMBER_OF_PROCESSORS" ) );
    if( n > MAX_AUTO_NUM_THREADS )
        n = MAX_AUTO_NUM_THREADS;
    return n;
}

static inline void clean_preferred_decoder_names( void )
{
    lw_freep( &reader_opt.preferred_decoder_names );
    memset( reader_opt.preferred_decoder_names_buf, 0, PREFERRED_DECODER_NAMES_BUFSIZE );
}

static inline void set_preferred_decoder_names_on_buf
(
    const char *preferred_decoder_names
)
{
    clean_preferred_decoder_names();
    memcpy( reader_opt.preferred_decoder_names_buf, preferred_decoder_names,
            MIN( PREFERRED_DECODER_NAMES_BUFSIZE - 1, strlen(preferred_decoder_names) ) );
    reader_opt.preferred_decoder_names = lw_tokenize_string( reader_opt.preferred_decoder_names_buf, ',', NULL );
}

static void get_settings( void )
{
    FILE *ini = open_settings();
    char buf[512];
    if( ini )
    {
        /* threads */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "threads=%d", &reader_opt.threads ) != 1 )
            reader_opt.threads = 0;
        /* av_sync */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "av_sync=%d", &reader_opt.av_sync ) != 1 )
            reader_opt.av_sync = 1;
        /* no_create_index */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "no_create_index=%d", &reader_opt.no_create_index ) != 1 )
            reader_opt.no_create_index = 0;
        /* force stream index */
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "force_video_index=%d:%d",
                    &reader_opt.force_video, &reader_opt.force_video_index ) != 2 )
        {
            reader_opt.force_video       = 0;
            reader_opt.force_video_index = -1;
        }
        else
            reader_opt.force_video_index = MAX( reader_opt.force_video_index, -1 );
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "force_audio_index=%d:%d",
                    &reader_opt.force_audio, &reader_opt.force_audio_index ) != 2 )
        {
            reader_opt.force_audio       = 0;
            reader_opt.force_audio_index = -1;
        }
        else
            reader_opt.force_audio_index = MAX( reader_opt.force_audio_index, -1 );
        /* seek_mode */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "seek_mode=%d", &video_opt->seek_mode ) != 1 )
            video_opt->seek_mode = 0;
        else
            video_opt->seek_mode = CLIP_VALUE( video_opt->seek_mode, 0, 2 );
        /* forward_seek_threshold */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "forward_threshold=%d", &video_opt->forward_seek_threshold ) != 1 )
            video_opt->forward_seek_threshold = 10;
        else
            video_opt->forward_seek_threshold = CLIP_VALUE( video_opt->forward_seek_threshold, 1, 999 );
        /* scaler */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "scaler=%d", &video_opt->scaler ) != 1 )
            video_opt->scaler = 0;
        else
            video_opt->scaler = CLIP_VALUE( video_opt->scaler, 0, 10 );
        /* apply_repeat_flag */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "apply_repeat_flag=%d", &video_opt->apply_repeat_flag ) != 1 )
            video_opt->apply_repeat_flag = 0;
        /* field_dominance */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "field_dominance=%d", &video_opt->field_dominance ) != 1 )
            video_opt->field_dominance = 0;
        else
            video_opt->field_dominance = CLIP_VALUE( video_opt->field_dominance, 0, 2 );
        /* VFR->CFR */
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "vfr2cfr=%d:%d:%d",
                    &video_opt->vfr2cfr.active,
                    &video_opt->vfr2cfr.framerate_num,
                    &video_opt->vfr2cfr.framerate_den ) != 3 )
        {
            video_opt->vfr2cfr.active        = 0;
            video_opt->vfr2cfr.framerate_num = 60000;
            video_opt->vfr2cfr.framerate_den = 1001;
        }
        else
        {
            video_opt->vfr2cfr.framerate_num = MAX( video_opt->vfr2cfr.framerate_num, 1 );
            video_opt->vfr2cfr.framerate_den = MAX( video_opt->vfr2cfr.framerate_den, 1 );
        }
        /* LW48 output */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "colorspace=%d", (int *)&video_opt->colorspace ) != 1 )
            video_opt->colorspace = 0;
        else
            video_opt->colorspace = video_opt->colorspace ? OUTPUT_LW48 : 0;
        /* AVS bit-depth */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "avs_bit_depth=%d", &video_opt->avs.bit_depth ) != 1 )
            video_opt->avs.bit_depth = 8;
        else
        {
            video_opt->avs.bit_depth = CLIP_VALUE( video_opt->avs.bit_depth, 8, 16 );
            if( video_opt->avs.bit_depth > 10 )
                video_opt->avs.bit_depth = 16;
        }
        /* audio_delay */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "audio_delay=%d", &audio_delay ) != 1 )
            audio_delay = 0;
        /* channel_layout */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "channel_layout=0x%"SCNx64, &audio_opt->channel_layout ) != 1 )
            audio_opt->channel_layout = 0;
        /* sample_rate */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "sample_rate=%d", &audio_opt->sample_rate ) != 1 )
            audio_opt->sample_rate = 0;
        /* mix_level */
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "mix_level=%d:%d:%d",
                    &audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ],
                    &audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND],
                    &audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] ) != 3 )
        {
            audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ] = 71;
            audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND] = 71;
            audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] = 0;
        }
        else
        {
            audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ] = CLIP_VALUE( audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ], 0, 10000 );
            audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND] = CLIP_VALUE( audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND], 0, 10000 );
            audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] = CLIP_VALUE( audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ], 0, 30000 );
        }
        /* readers */
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "libavsmash_disabled=%d", &reader_disabled[0] ) != 1 )
            reader_disabled[0] = 0;
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "avs_disabled=%d",        &reader_disabled[1] ) != 1 )
            reader_disabled[1] = 0;
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "vpy_disabled=%d",        &reader_disabled[2] ) != 1 )
            reader_disabled[2] = 0;
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "libav_disabled=%d",      &reader_disabled[3] ) != 1 )
            reader_disabled[3] = 0;
        /* dummy reader */
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "dummy_resolution=%dx%d", &video_opt->dummy.width, &video_opt->dummy.height ) != 2 )
        {
            video_opt->dummy.width  = 720;
            video_opt->dummy.height = 480;
        }
        else
        {
            video_opt->dummy.width  = MAX( video_opt->dummy.width,  32 );
            video_opt->dummy.height = MAX( video_opt->dummy.height, 32 );
        }
        if( !fgets( buf, sizeof(buf), ini )
         || sscanf( buf, "dummy_framerate=%d/%d", &video_opt->dummy.framerate_num, &video_opt->dummy.framerate_den ) != 2 )
        {
            video_opt->dummy.framerate_num = 24;
            video_opt->dummy.framerate_den = 1;
        }
        else
        {
            video_opt->dummy.framerate_num = MAX( video_opt->dummy.framerate_num, 1 );
            video_opt->dummy.framerate_den = MAX( video_opt->dummy.framerate_den, 1 );
        }
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "dummy_colorspace=%d", (int *)&video_opt->dummy.colorspace ) != 1 )
            video_opt->dummy.colorspace = OUTPUT_YUY2;
        else
            video_opt->dummy.colorspace = CLIP_VALUE( video_opt->dummy.colorspace, 0, 2 );
        /* preferred decoders settings */
        char preferred_decoder_names[512] = { 0 };
        if( !fgets( buf, sizeof(buf), ini ) || sscanf( buf, "preferred_decoders=%s", preferred_decoder_names ) != 1 )
            clean_preferred_decoder_names();
        else
            set_preferred_decoder_names_on_buf( preferred_decoder_names );
        fclose( ini );
    }
    else
    {
        /* Set up defalut values. */
        clean_preferred_decoder_names();
        reader_opt.threads                = 0;
        reader_opt.av_sync                = 1;
        reader_opt.no_create_index        = 0;
        reader_opt.force_video            = 0;
        reader_opt.force_video_index      = -1;
        reader_opt.force_audio            = 0;
        reader_opt.force_audio_index      = -1;
        reader_disabled[0]                = 0;
        reader_disabled[1]                = 0;
        reader_disabled[2]                = 0;
        reader_disabled[3]                = 0;
        video_opt->seek_mode              = 0;
        video_opt->forward_seek_threshold = 10;
        video_opt->scaler                 = 0;
        video_opt->apply_repeat_flag      = 0;
        video_opt->field_dominance        = 0;
        video_opt->vfr2cfr.active         = 0;
        video_opt->vfr2cfr.framerate_num  = 60000;
        video_opt->vfr2cfr.framerate_den  = 1001;
        video_opt->colorspace             = 0;
        video_opt->dummy.width            = 720;
        video_opt->dummy.height           = 480;
        video_opt->dummy.framerate_num    = 24;
        video_opt->dummy.framerate_den    = 1;
        video_opt->dummy.colorspace       = OUTPUT_YUY2;
        video_opt->avs.bit_depth          = 8;
        audio_delay                       = 0;
        audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ] = 71;
        audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND] = 71;
        audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] = 0;
    }
}

INPUT_HANDLE func_open( LPSTR file )
{
    lsmash_handler_t *hp = (lsmash_handler_t *)lw_malloc_zero( sizeof(lsmash_handler_t) );
    if( !hp )
        return NULL;
    hp->video_reader = READER_NONE;
    hp->audio_reader = READER_NONE;
    get_settings();
    if( reader_opt.threads <= 0 )
        reader_opt.threads = get_auto_threads();
    extern lsmash_reader_t libavsmash_reader;
    extern lsmash_reader_t avs_reader;
    extern lsmash_reader_t vpy_reader;
    extern lsmash_reader_t libav_reader;
    extern lsmash_reader_t dummy_reader;
    enum
    {
        AU_VIDEO_READER  = 1,
        AU_SCRIPT_READER = 2,
        AU_DUMMY_READER  = 3
    };
    static const struct
    {
        lsmash_reader_t *reader;
        int              attribute;
    } lsmash_reader_table[] =
        {
            { &libavsmash_reader, AU_VIDEO_READER  },
            { &avs_reader       , AU_SCRIPT_READER },
            { &vpy_reader       , AU_SCRIPT_READER },
            { &libav_reader     , AU_VIDEO_READER  },
            { &dummy_reader     , AU_DUMMY_READER  },
            { NULL              , 0                }
        };
    for( int i = 0; lsmash_reader_table[i].reader; i++ )
    {
        if( reader_disabled[lsmash_reader_table[i].reader->type - 1] )
            continue;
        int video_none = 1;
        int audio_none = 1;
        lsmash_reader_t reader      = *lsmash_reader_table[i].reader;
        int             reader_attr =  lsmash_reader_table[i].attribute;
        void *private_stuff = reader.open_file( file, &reader_opt );
        if( private_stuff )
        {
            if( !hp->video_private )
            {
                hp->video_private = private_stuff;
                if( reader.get_video_track
                 && reader.get_video_track( hp, video_opt ) == 0 )
                {
                    hp->video_reader     = reader.type;
                    hp->read_video       = reader.read_video;
                    hp->is_keyframe      = reader.is_keyframe;
                    hp->video_cleanup    = reader.video_cleanup;
                    hp->close_video_file = reader.close_file;
                    video_none = 0;
                }
                else
                    hp->video_private = NULL;
            }
            if( !hp->audio_private )
            {
                hp->audio_private = private_stuff;
                if( reader.get_audio_track
                 && reader.get_audio_track( hp, audio_opt ) == 0 )
                {
                    hp->audio_reader     = reader.type;
                    hp->read_audio       = reader.read_audio;
                    hp->delay_audio      = reader.delay_audio;
                    hp->audio_cleanup    = reader.audio_cleanup;
                    hp->close_audio_file = reader.close_file;
                    audio_none = 0;
                }
                else
                    hp->audio_private = NULL;
            }
        }
        if( video_none && audio_none )
        {
            if( reader.close_file )
                reader.close_file( private_stuff );
        }
        else
            if( reader.destroy_disposable )
                reader.destroy_disposable( private_stuff );
        /* Found both video and audio reader. */
        if( hp->video_reader != READER_NONE && hp->audio_reader != READER_NONE )
            break;
        if( reader_attr == AU_SCRIPT_READER )
        {
            if( hp->video_reader == reader.type )
                break;
            if( hp->audio_reader == reader.type )
                while( lsmash_reader_table[i + 1].attribute != AU_DUMMY_READER )
                    i++;
        }
    }
    if( hp->video_reader == hp->audio_reader )
    {
        hp->global_private = hp->video_private;
        hp->close_file     = hp->close_video_file;
        hp->close_video_file = NULL;
        hp->close_audio_file = NULL;
    }
    if( hp->video_reader == READER_NONE && hp->audio_reader == READER_NONE )
    {
        DEBUG_MESSAGE_BOX_DESKTOP( MB_OK, "No readable video and/or audio stream" );
        func_close( hp );
        return NULL;
    }
    return hp;
}

BOOL func_close( INPUT_HANDLE ih )
{
    lw_freep( &reader_opt.preferred_decoder_names );
    lsmash_handler_t *hp = (lsmash_handler_t *)ih;
    if( !hp )
        return TRUE;
    if( hp->video_cleanup )
        hp->video_cleanup( hp );
    if( hp->audio_cleanup )
        hp->audio_cleanup( hp );
    if( hp->close_file )
        hp->close_file( hp->global_private );
    else
    {
        if( hp->close_video_file )
            hp->close_video_file( hp->video_private );
        if( hp->close_audio_file )
            hp->close_audio_file( hp->audio_private );
    }
    lw_free( hp );
    return TRUE;
}

BOOL func_info_get( INPUT_HANDLE ih, INPUT_INFO *iip )
{
    lsmash_handler_t *hp = (lsmash_handler_t *)ih;
    memset( iip, 0, sizeof(INPUT_INFO) );
    if( hp->video_reader != READER_NONE )
    {
        iip->flag             |= INPUT_INFO_FLAG_VIDEO | INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
        iip->rate              = hp->framerate_num;
        iip->scale             = hp->framerate_den;
        iip->n                 = hp->video_sample_count;
        iip->format            = &hp->video_format;
        iip->format_size       = hp->video_format.biSize;
        iip->handler           = 0;
    }
    if( hp->audio_reader != READER_NONE )
    {
        iip->flag             |= INPUT_INFO_FLAG_AUDIO;
        iip->audio_n           = hp->audio_pcm_sample_count + audio_delay;
        iip->audio_format      = &hp->audio_format.Format;
        iip->audio_format_size = sizeof( WAVEFORMATEX ) + hp->audio_format.Format.cbSize;
    }
    return TRUE;
}

int func_read_video( INPUT_HANDLE ih, int sample_number, void *buf )
{
    lsmash_handler_t *hp = (lsmash_handler_t *)ih;
    return hp->read_video ? hp->read_video( hp, sample_number, buf ) : 0;
}

int func_read_audio( INPUT_HANDLE ih, int start, int length, void *buf )
{
    lsmash_handler_t *hp = (lsmash_handler_t *)ih;
    if( hp->read_audio && hp->delay_audio( hp, &start, length, audio_delay ) )
        return hp->read_audio( hp, start, length, buf );
    uint8_t silence = hp->audio_format.Format.wBitsPerSample == 8 ? 128 : 0;
    memset( buf, silence, length * hp->audio_format.Format.nBlockAlign );
    return length;
}

BOOL func_is_keyframe( INPUT_HANDLE ih, int sample_number )
{
    lsmash_handler_t *hp = (lsmash_handler_t *)ih;
    if( sample_number >= hp->video_sample_count )
        return FALSE;   /* In reading as double framerate, keyframe detection doesn't work at all
                         * since sample_number exceeds the number of video samples. */
    return hp->is_keyframe ? hp->is_keyframe( hp, sample_number ) : TRUE;
}

static inline void set_buddy_window_for_updown_control
(
    HWND hwnd,
    int  spin_idc,
    int  buddy_idc
)
{
    SendMessage( GetDlgItem( hwnd, spin_idc ), UDM_SETBUDDY, (WPARAM)GetDlgItem( hwnd, buddy_idc ), 0 );
}

static inline void set_check_state
(
    HWND hwnd,
    int  idc,   /* identifier for control */
    int  value
)
{
    SendMessage( GetDlgItem( hwnd, idc ), BM_SETCHECK, (WPARAM) value ? BST_CHECKED : BST_UNCHECKED, 0 );
}

static inline int get_check_state
(
    HWND hwnd,
    int  idc    /* identifier for control */
)
{
    return (BST_CHECKED == SendMessage( GetDlgItem( hwnd, idc ), BM_GETCHECK, 0, 0 ));
}

static void set_int_to_dlg
(
    HWND hwnd,
    int  idc,   /* identifier for control */
    int  value  /* message value */
)
{
    char edit_buf[512];
    sprintf( edit_buf, "%d", value );
    SetDlgItemText( hwnd, idc, (LPCTSTR)edit_buf );
}

static int get_int_from_dlg
(
    HWND hwnd,
    int  idc    /* identifier for control */
)
{
    char edit_buf[512];
    GetDlgItemText( hwnd, idc, (LPTSTR)edit_buf, sizeof(edit_buf) );
    return atoi( edit_buf );
}

static int get_int_from_dlg_with_min
(
    HWND hwnd,
    int  idc,   /* identifier for control */
    int  min
)
{
    int value = get_int_from_dlg( hwnd, idc );
    return MAX( value, min );
}

static inline void set_string_to_dlg
(
    HWND  hwnd,
    int   idc,  /* identifier for control */
    char *value /* message value */
)
{
    SetDlgItemText( hwnd, idc, (LPCTSTR)value );
}

static void send_mix_level
(
    HWND  hwnd,
    int   slider_idc,
    int   text_idc,
    int   range_min,
    int   range_max,
    int   mix_level
)
{
    char edit_buf[512];
    HWND hslider = GetDlgItem( hwnd, slider_idc );
    SendMessage( hslider, TBM_SETRANGE,    TRUE, MAKELPARAM( range_min, range_max ) );
    SendMessage( hslider, TBM_SETTICFREQ,  1,    0 );
    SendMessage( hslider, TBM_SETPOS,      TRUE, mix_level );
    SendMessage( hslider, TBM_SETLINESIZE, 0,    1 );
    SendMessage( hslider, TBM_SETPAGESIZE, 0,    1 );
    sprintf( edit_buf, "%.2f", mix_level / 100.0 );
    SetWindowText( GetDlgItem( hwnd, text_idc ), (LPCTSTR)edit_buf );
}

static void get_mix_level
(
    HWND  hwnd,
    int   slider_idc,
    int   text_idc,
    int  *mix_level
)
{
    char edit_buf[512];
    HWND hslider = GetDlgItem( hwnd, slider_idc );
    *mix_level = SendMessage( hslider, TBM_GETPOS, 0, 0 );
    sprintf( edit_buf, "%.2f", *mix_level / 100.0 );
    SetWindowText( GetDlgItem( hwnd, text_idc ), (LPCTSTR)edit_buf );
}

static BOOL CALLBACK dialog_proc
(
    HWND   hwnd,
    UINT   message,
    WPARAM wparam,
    LPARAM lparam
)
{
    switch( message )
    {
        case WM_INITDIALOG :
            InitCommonControls();
            get_settings();
            /* threads */
            set_int_to_dlg( hwnd, IDC_EDIT_THREADS, reader_opt.threads );
            set_buddy_window_for_updown_control( hwnd, IDC_SPIN_THREADS, IDC_EDIT_THREADS );
            /* av_sync */
            set_check_state( hwnd, IDC_CHECK_AV_SYNC, reader_opt.av_sync );
            /* no_create_index */
            set_check_state( hwnd, IDC_CHECK_CREATE_INDEX_FILE, !reader_opt.no_create_index );
            /* force stream index */
            set_check_state( hwnd, IDC_CHECK_FORCE_VIDEO, reader_opt.force_video );
            set_check_state( hwnd, IDC_CHECK_FORCE_AUDIO, reader_opt.force_audio );
            set_int_to_dlg( hwnd, IDC_EDIT_FORCE_VIDEO_INDEX, reader_opt.force_video_index );
            set_int_to_dlg( hwnd, IDC_EDIT_FORCE_AUDIO_INDEX, reader_opt.force_audio_index );
            /* forward_seek_threshold */
            set_int_to_dlg( hwnd, IDC_EDIT_FORWARD_THRESHOLD, video_opt->forward_seek_threshold );
            set_buddy_window_for_updown_control( hwnd, IDC_SPIN_FORWARD_THRESHOLD, IDC_EDIT_FORWARD_THRESHOLD );
            /* seek mode */
            HWND hcombo = GetDlgItem( hwnd, IDC_COMBOBOX_SEEK_MODE );
            for( int i = 0; i < 3; i++ )
                SendMessage( hcombo, CB_ADDSTRING, 0, (LPARAM)seek_mode_list[i] );
            SendMessage( hcombo, CB_SETCURSEL, video_opt->seek_mode, 0 );
            /* scaler */
            hcombo = GetDlgItem( hwnd, IDC_COMBOBOX_SCALER );
            for( int i = 0; i < 11; i++ )
                SendMessage( hcombo, CB_ADDSTRING, 0, (LPARAM)scaler_list[i] );
            SendMessage( hcombo, CB_SETCURSEL, video_opt->scaler, 0 );
            /* apply_repeat_flag */
            set_check_state( hwnd, IDC_CHECK_APPLY_REPEAT_FLAG, video_opt->apply_repeat_flag );
            /* field_dominance */
            hcombo = GetDlgItem( hwnd, IDC_COMBOBOX_FIELD_DOMINANCE );
            for( int i = 0; i < 3; i++ )
                SendMessage( hcombo, CB_ADDSTRING, 0, (LPARAM)field_dominance_list[i] );
            SendMessage( hcombo, CB_SETCURSEL, video_opt->field_dominance, 0 );
            /* VFR->CFR */
            set_check_state( hwnd, IDC_CHECK_VFR_TO_CFR, video_opt->vfr2cfr.active );
            set_int_to_dlg( hwnd, IDC_EDIT_CONST_FRAMERATE_NUM, video_opt->vfr2cfr.framerate_num );
            set_int_to_dlg( hwnd, IDC_EDIT_CONST_FRAMERATE_DEN, video_opt->vfr2cfr.framerate_den );
            /* LW48 output */
            set_check_state( hwnd, IDC_CHECK_LW48_OUTPUT, video_opt->colorspace != 0 );
            /* AVS bit-depth */
            hcombo = GetDlgItem( hwnd, IDC_COMBOBOX_AVS_BITDEPTH );
            for( int i = 0; i < 4; i++ )
            {
                SendMessage( hcombo, CB_ADDSTRING, 0, (LPARAM)avs_bit_depth_list[i] );
                if( video_opt->avs.bit_depth == atoi( avs_bit_depth_list[i] ) )
                    SendMessage( hcombo, CB_SETCURSEL, i, 0 );
            }
            /* audio_delay */
            set_int_to_dlg( hwnd, IDC_EDIT_AUDIO_DELAY, audio_delay );
            /* channel_layout */
            if( audio_opt->channel_layout )
            {
                char edit_buf[512] = { 0 };
                char *buf = edit_buf;
                for( int i = 0; i < 64; i++ )
                {
                    uint64_t audio_channel = audio_opt->channel_layout & (1ULL << i);
                    if( audio_channel )
                    {
                        const char *channel_name = av_get_channel_name( audio_channel );
                        if( channel_name )
                        {
                            int name_length = strlen( channel_name );
                            memcpy( buf, channel_name, name_length );
                            buf += name_length;
                            *(buf++) = '+';
                        }
                    }
                }
                if( buf > edit_buf )
                {
                    *(buf - 1) = '\0';  /* Replace the last '+' with NULL terminator. */
                    SetDlgItemText( hwnd, IDC_EDIT_CHANNEL_LAYOUT, (LPCTSTR)edit_buf );
                }
                else
                    set_string_to_dlg( hwnd, IDC_EDIT_CHANNEL_LAYOUT, "Unspecified" );
            }
            else
                set_string_to_dlg( hwnd, IDC_EDIT_CHANNEL_LAYOUT, "Unspecified" );
            /* sample_rate */
            if( audio_opt->sample_rate > 0 )
                set_int_to_dlg( hwnd, IDC_EDIT_SAMPLE_RATE, audio_opt->sample_rate );
            else
            {
                audio_opt->sample_rate = 0;
                set_string_to_dlg( hwnd, IDC_EDIT_SAMPLE_RATE, "0 (Auto)" );
            }
            /* mix_level */
            send_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_CENTER,   IDC_TEXT_MIX_LEVEL_CENTER,   0, 500, audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ] );
            send_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_SURROUND, IDC_TEXT_MIX_LEVEL_SURROUND, 0, 500, audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND] );
            send_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_LFE,      IDC_TEXT_MIX_LEVEL_LFE,      0, 500, audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] );
            /* readers */
            set_check_state( hwnd, IDC_CHECK_LIBAVSMASH_INPUT, !reader_disabled[0] );
            set_check_state( hwnd, IDC_CHECK_AVS_INPUT,        !reader_disabled[1] );
            set_check_state( hwnd, IDC_CHECK_VPY_INPUT,        !reader_disabled[2] );
            set_check_state( hwnd, IDC_CHECK_LIBAV_INPUT,      !reader_disabled[3] );
            /* dummy reader */
            set_int_to_dlg( hwnd, IDC_EDIT_DUMMY_WIDTH,         video_opt->dummy.width );
            set_int_to_dlg( hwnd, IDC_EDIT_DUMMY_HEIGHT,        video_opt->dummy.height );
            set_int_to_dlg( hwnd, IDC_EDIT_DUMMY_FRAMERATE_NUM, video_opt->dummy.framerate_num );
            set_int_to_dlg( hwnd, IDC_EDIT_DUMMY_FRAMERATE_DEN, video_opt->dummy.framerate_den );
            hcombo = GetDlgItem( hwnd, IDC_COMBOBOX_DUMMY_COLORSPACE );
            for( int i = 0; i < 3; i++ )
                SendMessage( hcombo, CB_ADDSTRING, 0, (LPARAM)dummy_colorspace_list[i] );
            SendMessage( hcombo, CB_SETCURSEL, video_opt->dummy.colorspace, 0 );
            /* preferred decoders */
            if( reader_opt.preferred_decoder_names )
            {
                char edit_buf[512] = { 0 };
                char *buf = edit_buf;
                for( const char **decoder = reader_opt.preferred_decoder_names; *decoder != NULL; decoder++ )
                {
                    if( *decoder != *reader_opt.preferred_decoder_names )
                        *(buf++) = ',';
                    int length = strlen( *decoder );
                    memcpy( buf, *decoder, length );
                    buf += length;
                }
                set_string_to_dlg( hwnd, IDC_EDIT_PREFERRED_DECODERS, edit_buf );
            }
            else
                set_string_to_dlg( hwnd, IDC_EDIT_PREFERRED_DECODERS, "" );
            /* Library informations */
            if( plugin_information[0] == 0 )
                get_plugin_information();
            SetDlgItemText( hwnd, IDC_TEXT_LIBRARY_INFO, (LPCTSTR)plugin_information );
            HFONT hfont = (HFONT)GetStockObject( DEFAULT_GUI_FONT );
            LOGFONT lf = { 0 };
            GetObject( hfont, sizeof(lf), &lf );
            lf.lfWidth  *= 0.90;
            lf.lfHeight *= 0.90;
            lf.lfQuality = ANTIALIASED_QUALITY;
            SendMessage( GetDlgItem( hwnd, IDC_TEXT_LIBRARY_INFO ), WM_SETFONT, (WPARAM)CreateFontIndirect( &lf ), 1 );
            return TRUE;
        case WM_NOTIFY :
            if( wparam == IDC_SPIN_THREADS )
            {
                LPNMUPDOWN lpnmud = (LPNMUPDOWN)lparam;
                if( lpnmud->hdr.code == UDN_DELTAPOS )
                {
                    reader_opt.threads = get_int_from_dlg( hwnd, IDC_EDIT_THREADS );
                    if( lpnmud->iDelta )
                        reader_opt.threads += lpnmud->iDelta > 0 ? -1 : 1;
                    if( reader_opt.threads < 0 )
                        reader_opt.threads = 0;
                    set_int_to_dlg( hwnd, IDC_EDIT_THREADS, reader_opt.threads );
                }
            }
            else if( wparam == IDC_SPIN_FORWARD_THRESHOLD )
            {
                LPNMUPDOWN lpnmud = (LPNMUPDOWN)lparam;
                if( lpnmud->hdr.code == UDN_DELTAPOS )
                {
                    video_opt->forward_seek_threshold = get_int_from_dlg( hwnd, IDC_EDIT_FORWARD_THRESHOLD );
                    if( lpnmud->iDelta )
                        video_opt->forward_seek_threshold += lpnmud->iDelta > 0 ? -1 : 1;
                    video_opt->forward_seek_threshold = CLIP_VALUE( video_opt->forward_seek_threshold, 1, 999 );
                    set_int_to_dlg( hwnd, IDC_EDIT_FORWARD_THRESHOLD, video_opt->forward_seek_threshold );
                }
            }
            return TRUE;
        case WM_HSCROLL :
            if( GetDlgItem( hwnd, IDC_SLIDER_MIX_LEVEL_CENTER ) == (HWND)lparam )
                get_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_CENTER,   IDC_TEXT_MIX_LEVEL_CENTER,   &audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ] );
            else if( GetDlgItem( hwnd, IDC_SLIDER_MIX_LEVEL_SURROUND ) == (HWND)lparam )
                get_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_SURROUND, IDC_TEXT_MIX_LEVEL_SURROUND, &audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND] );
            else if( GetDlgItem( hwnd, IDC_SLIDER_MIX_LEVEL_LFE ) == (HWND)lparam )
                get_mix_level( hwnd, IDC_SLIDER_MIX_LEVEL_LFE,      IDC_TEXT_MIX_LEVEL_LFE,      &audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] );
            return FALSE;
        case WM_COMMAND :
            switch( wparam )
            {
                case IDCANCEL :
                    EndDialog( hwnd, IDCANCEL );
                    return TRUE;
                case IDOK :
                {
                    /* Open */
                    if( !settings_path )
                        settings_path = (char *)settings_path_list[0];
                    FILE *ini = fopen( settings_path, "w" );
                    if( !ini )
                    {
                        MESSAGE_BOX_DESKTOP( MB_ICONERROR | MB_OK, "Failed to update configuration file" );
                        return FALSE;
                    }
                    /* threads */
                    reader_opt.threads = get_int_from_dlg_with_min( hwnd, IDC_EDIT_THREADS, 0 );
                    if( reader_opt.threads > 0 )
                        fprintf( ini, "threads=%d\n", reader_opt.threads );
                    else
                        fprintf( ini, "threads=0 (auto)\n" );
                    /* av_sync */
                    reader_opt.av_sync = get_check_state( hwnd, IDC_CHECK_AV_SYNC );
                    fprintf( ini, "av_sync=%d\n", reader_opt.av_sync );
                    /* no_create_index */
                    reader_opt.no_create_index = !get_check_state( hwnd, IDC_CHECK_CREATE_INDEX_FILE );
                    fprintf( ini, "no_create_index=%d\n", reader_opt.no_create_index );
                    /* force stream index */
                    reader_opt.force_video = get_check_state( hwnd, IDC_CHECK_FORCE_VIDEO );
                    reader_opt.force_audio = get_check_state( hwnd, IDC_CHECK_FORCE_AUDIO );
                    reader_opt.force_video_index = get_int_from_dlg_with_min( hwnd, IDC_EDIT_FORCE_VIDEO_INDEX, -1 );
                    reader_opt.force_audio_index = get_int_from_dlg_with_min( hwnd, IDC_EDIT_FORCE_AUDIO_INDEX, -1 );
                    fprintf( ini, "force_video_index=%d:%d\n", reader_opt.force_video, reader_opt.force_video_index );
                    fprintf( ini, "force_audio_index=%d:%d\n", reader_opt.force_audio, reader_opt.force_audio_index );
                    /* seek_mode */
                    video_opt->seek_mode = SendMessage( GetDlgItem( hwnd, IDC_COMBOBOX_SEEK_MODE ), CB_GETCURSEL, 0, 0 );
                    fprintf( ini, "seek_mode=%d\n", video_opt->seek_mode );
                    /* forward_seek_threshold */
                    video_opt->forward_seek_threshold = get_int_from_dlg( hwnd, IDC_EDIT_FORWARD_THRESHOLD );
                    video_opt->forward_seek_threshold = CLIP_VALUE( video_opt->forward_seek_threshold, 1, 999 );
                    fprintf( ini, "forward_threshold=%d\n", video_opt->forward_seek_threshold );
                    /* scaler */
                    video_opt->scaler = SendMessage( GetDlgItem( hwnd, IDC_COMBOBOX_SCALER ), CB_GETCURSEL, 0, 0 );
                    fprintf( ini, "scaler=%d\n", video_opt->scaler );
                    /* apply_repeat_flag */
                    video_opt->apply_repeat_flag = get_check_state( hwnd, IDC_CHECK_APPLY_REPEAT_FLAG );
                    fprintf( ini, "apply_repeat_flag=%d\n", video_opt->apply_repeat_flag );
                    /* field_dominance */
                    video_opt->field_dominance = SendMessage( GetDlgItem( hwnd, IDC_COMBOBOX_FIELD_DOMINANCE ), CB_GETCURSEL, 0, 0 );
                    fprintf( ini, "field_dominance=%d\n", video_opt->field_dominance );
                    /* VFR->CFR */
                    video_opt->vfr2cfr.active = get_check_state( hwnd, IDC_CHECK_VFR_TO_CFR );
                    video_opt->vfr2cfr.framerate_num = get_int_from_dlg_with_min( hwnd, IDC_EDIT_CONST_FRAMERATE_NUM, 1 );
                    video_opt->vfr2cfr.framerate_den = get_int_from_dlg_with_min( hwnd, IDC_EDIT_CONST_FRAMERATE_DEN, 1 );
                    fprintf( ini, "vfr2cfr=%d:%d:%d\n", video_opt->vfr2cfr.active, video_opt->vfr2cfr.framerate_num, video_opt->vfr2cfr.framerate_den );
                    /* LW48 output */
                    video_opt->colorspace = (get_check_state( hwnd, IDC_CHECK_LW48_OUTPUT ) ? OUTPUT_LW48 : 0);
                    fprintf( ini, "colorspace=%d\n", video_opt->colorspace );
                    /* AVS bit-depth */
                    video_opt->avs.bit_depth = SendMessage( GetDlgItem( hwnd, IDC_COMBOBOX_AVS_BITDEPTH ), CB_GETCURSEL, 0, 0 );
                    video_opt->avs.bit_depth = atoi( avs_bit_depth_list[ video_opt->avs.bit_depth ] );
                    fprintf( ini, "avs_bit_depth=%d\n", video_opt->avs.bit_depth );
                    /* audio_delay */
                    audio_delay = get_int_from_dlg( hwnd, IDC_EDIT_AUDIO_DELAY );
                    fprintf( ini, "audio_delay=%d\n", audio_delay );
                    /* channel_layout */
                    {
                        char edit_buf[512];
                        GetDlgItemText( hwnd, IDC_EDIT_CHANNEL_LAYOUT, (LPTSTR)edit_buf, sizeof(edit_buf) );
                        audio_opt->channel_layout = av_get_channel_layout( edit_buf );
                    }
                    fprintf( ini, "channel_layout=0x%"PRIx64"\n", audio_opt->channel_layout );
                    /* sample_rate */
                    audio_opt->sample_rate = get_int_from_dlg_with_min( hwnd, IDC_EDIT_SAMPLE_RATE, 0 );
                    fprintf( ini, "sample_rate=%d\n", audio_opt->sample_rate );
                    /* mix_level */
                    fprintf( ini, "mix_level=%d:%d:%d\n",
                             audio_opt->mix_level[MIX_LEVEL_INDEX_CENTER  ],
                             audio_opt->mix_level[MIX_LEVEL_INDEX_SURROUND],
                             audio_opt->mix_level[MIX_LEVEL_INDEX_LFE     ] );
                    /* readers */
                    reader_disabled[0] = !get_check_state( hwnd, IDC_CHECK_LIBAVSMASH_INPUT );
                    reader_disabled[1] = !get_check_state( hwnd, IDC_CHECK_AVS_INPUT        );
                    reader_disabled[2] = !get_check_state( hwnd, IDC_CHECK_VPY_INPUT        );
                    reader_disabled[3] = !get_check_state( hwnd, IDC_CHECK_LIBAV_INPUT      );
                    fprintf( ini, "libavsmash_disabled=%d\n", reader_disabled[0] );
                    fprintf( ini, "avs_disabled=%d\n",        reader_disabled[1] );
                    fprintf( ini, "vpy_disabled=%d\n",        reader_disabled[2] );
                    fprintf( ini, "libav_disabled=%d\n",      reader_disabled[3] );
                    /* dummy reader */
                    video_opt->dummy.width         = get_int_from_dlg_with_min( hwnd, IDC_EDIT_DUMMY_WIDTH,  32 );
                    video_opt->dummy.height        = get_int_from_dlg_with_min( hwnd, IDC_EDIT_DUMMY_HEIGHT, 32 );
                    video_opt->dummy.framerate_num = get_int_from_dlg_with_min( hwnd, IDC_EDIT_DUMMY_FRAMERATE_NUM, 1 );
                    video_opt->dummy.framerate_den = get_int_from_dlg_with_min( hwnd, IDC_EDIT_DUMMY_FRAMERATE_DEN, 1 );
                    video_opt->dummy.colorspace    = SendMessage( GetDlgItem( hwnd, IDC_COMBOBOX_DUMMY_COLORSPACE ), CB_GETCURSEL, 0, 0 );
                    fprintf( ini, "dummy_resolution=%dx%d\n", video_opt->dummy.width, video_opt->dummy.height );
                    fprintf( ini, "dummy_framerate=%d/%d\n",  video_opt->dummy.framerate_num, video_opt->dummy.framerate_den );
                    fprintf( ini, "dummy_colorspace=%d\n",    video_opt->dummy.colorspace );
                    /* preferred decoders */
                    {
                        char edit_buf[512];
                        GetDlgItemText( hwnd, IDC_EDIT_PREFERRED_DECODERS, (LPTSTR)edit_buf, sizeof(edit_buf) );
                        set_preferred_decoder_names_on_buf( edit_buf );
                        fprintf( ini, "preferred_decoders=%s\n", edit_buf );
                    }
                    /* Close */
                    fclose( ini );
                    EndDialog( hwnd, IDOK );
                    MESSAGE_BOX_DESKTOP( MB_OK, "Please reopen the input file for updating settings!" );
                    return TRUE;
                }
                default :
                    return FALSE;
            }
        case WM_CLOSE :
            EndDialog( hwnd, IDOK );
            return TRUE;
        default :
            return FALSE;
    }
}

BOOL func_config( HWND hwnd, HINSTANCE dll_hinst )
{
    DialogBox( dll_hinst, "LWINPUT_CONFIG", hwnd, dialog_proc );
    return TRUE;
}
