/********************************************************************************
 * vpy_input.c
 ********************************************************************************
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
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "lwinput.h"

#include <VSHelper.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "video_output.h"

typedef struct VSScript VSScript;

typedef enum VSEvalFlags
{
    efSetWorkingDir = 1,
} VSEvalFlags;

#define VSSCRIPT_API( ret, name ) typedef ret (VS_CC *vsscript_##name##_func)
VSSCRIPT_API( int,           init         )( void );
VSSCRIPT_API( int,           finalize     )( void );
VSSCRIPT_API( int,           evaluateFile )( VSScript **handle, const char *scriptFilename, int flags );
VSSCRIPT_API( void,          freeScript   )( VSScript *handle );
VSSCRIPT_API( VSNodeRef *,   getOutput    )( VSScript *handle, int index );
VSSCRIPT_API( const VSAPI *, getVSApi     )( void );
#undef VSSCRIPT_API

typedef struct
{
    /* VSScript */
    HMODULE                   library;
    struct
    {
        VSScript *handle;
        struct
        {
#define VSSCRIPT_DECLARE_FUNC( name ) vsscript_##name##_func name
            VSSCRIPT_DECLARE_FUNC( init         );
            VSSCRIPT_DECLARE_FUNC( finalize     );
            VSSCRIPT_DECLARE_FUNC( evaluateFile );
            VSSCRIPT_DECLARE_FUNC( freeScript   );
            VSSCRIPT_DECLARE_FUNC( getOutput    );
            VSSCRIPT_DECLARE_FUNC( getVSApi     );
#undef VSSCRIPT_DECLARE_FUNC
        } func;
    } vsscript;
    /* VapourSynth */
    const VSAPI              *vsapi;
    VSNodeRef                *node;
    const VSVideoInfo        *vi;
    /* Video stuff */
    AVFrame                  *av_frame;
    lw_video_output_handler_t voh;
} vpy_handler_t;

static int load_vsscript_dll
(
    vpy_handler_t *hp
)
{
    HKEY  key_handle;
    DWORD data_size;
    if( RegOpenKeyEx( HKEY_LOCAL_MACHINE, "Software\\VapourSynth", 0, KEY_QUERY_VALUE, &key_handle ) == ERROR_SUCCESS
     && RegQueryValueEx( key_handle, "VSScriptDLL", NULL, NULL, NULL, &data_size ) == ERROR_SUCCESS )
    {
        char dll_path[data_size];
        if( RegQueryValueEx( key_handle, "VSScriptDLL", NULL, NULL, (LPBYTE)dll_path, &data_size ) == ERROR_SUCCESS )
            hp->library = LoadLibrary( dll_path );
        RegCloseKey( key_handle );
    }
    if( !hp->library )
        hp->library = LoadLibrary( "vsscript" );
    if( !hp->library )
        return -1;
#define SYM( name, size ) LW_STRINGFY( _vsscript_##name ) "@" LW_STRINGFY( size )
#define LOAD_VSSCRIPT_FUNC( name, size )                                              \
    do                                                                                \
    {                                                                                 \
        hp->vsscript.func.name =                                                      \
            (vsscript_##name##_func)GetProcAddress( hp->library, SYM( name, size ) ); \
        if( !hp->vsscript.func.name )                                                 \
            goto fail;                                                                \
    } while( 0 )
    LOAD_VSSCRIPT_FUNC( init,         0  );
    LOAD_VSSCRIPT_FUNC( finalize,     0  );
    LOAD_VSSCRIPT_FUNC( evaluateFile, 12 );
    LOAD_VSSCRIPT_FUNC( freeScript,   4  );
    LOAD_VSSCRIPT_FUNC( getOutput,    8  );
    LOAD_VSSCRIPT_FUNC( getVSApi,     0  );
#undef SYM
#undef LOAD_VSSCRIPT_FUNC
    return 0;
fail:
    FreeLibrary( hp->library );
    return -1;
}

static void close_vsscript_dll
(
    vpy_handler_t *hp
)
{
    assert( hp->library );
    if( hp->node )
        hp->vsapi->freeNode( hp->node );
    if( hp->vsscript.handle )
        hp->vsscript.func.freeScript( hp->vsscript.handle );
    hp->vsscript.func.finalize();
    FreeLibrary( hp->library );
}

static enum AVPixelFormat vs_to_av_input_pixel_format
(
    const VSFormat *format
)
{
    if( !format )
        return AV_PIX_FMT_NONE;
    VSPresetFormat vs_input_pixel_format = format->id;
    static const struct
    {
        VSPresetFormat     vs_input_pixel_format;
        enum AVPixelFormat av_input_pixel_format;
    } format_table[] =
        {
            { pfYUV420P8,  AV_PIX_FMT_YUV420P     },
            { pfYUV422P8,  AV_PIX_FMT_YUV422P     },
            { pfYUV444P8,  AV_PIX_FMT_YUV444P     },
            { pfYUV410P8,  AV_PIX_FMT_YUV410P     },
            { pfYUV411P8,  AV_PIX_FMT_YUV411P     },
            { pfYUV440P8,  AV_PIX_FMT_YUV440P     },
            { pfYUV420P9,  AV_PIX_FMT_YUV420P9LE  },
            { pfYUV422P9,  AV_PIX_FMT_YUV422P9LE  },
            { pfYUV444P9,  AV_PIX_FMT_YUV444P9LE  },
            { pfYUV420P10, AV_PIX_FMT_YUV420P10LE },
            { pfYUV422P10, AV_PIX_FMT_YUV422P10LE },
            { pfYUV444P10, AV_PIX_FMT_YUV444P10LE },
            { pfYUV420P16, AV_PIX_FMT_YUV420P16LE },
            { pfYUV422P16, AV_PIX_FMT_YUV422P16LE },
            { pfYUV444P16, AV_PIX_FMT_YUV444P16LE },
            { pfRGB24,     AV_PIX_FMT_GBRP        },
            { pfRGB27,     AV_PIX_FMT_GBRP9LE     },
            { pfRGB30,     AV_PIX_FMT_GBRP10LE    },
            { pfRGB48,     AV_PIX_FMT_GBRP16LE    },
            { pfGray8,     AV_PIX_FMT_GRAY8       },
            { pfGray16,    AV_PIX_FMT_GRAY16LE    },
            { pfNone,      AV_PIX_FMT_NONE        }
        };
    for( int i = 0; format_table[i].vs_input_pixel_format != pfNone; i++ )
        if( vs_input_pixel_format == format_table[i].vs_input_pixel_format )
            return format_table[i].av_input_pixel_format;
    return AV_PIX_FMT_NONE;
}

static int prepare_video_decoding
(
    lsmash_handler_t *h,
    video_option_t   *opt
)
{
    vpy_handler_t *hp = (vpy_handler_t *)h->video_private;
    h->video_sample_count = hp->vi->numFrames;
    h->framerate_num      = hp->vi->fpsNum;
    h->framerate_den      = hp->vi->fpsDen;
    /* Set up video rendering. */
    enum AVPixelFormat input_pixel_format = vs_to_av_input_pixel_format( hp->vi->format );
    return au_setup_video_rendering( &hp->voh, opt, &h->video_format, hp->vi->width, hp->vi->height, input_pixel_format );
}

static inline int vs_is_rgb_format
(
    int color_family
)
{
    return color_family >= cmRGB && color_family < cmRGB + 1000000 ? 1 : 0;
}

static inline void get_color_range
(
    vpy_handler_t *hp,
    const VSMap   *props
)
{
    if( hp->vsapi->propNumElements( props, "_ColorRange" ) > 0 )
        hp->av_frame->color_range = hp->vsapi->propGetInt( props, "_ColorRange", 0, NULL )
                             ? AVCOL_RANGE_MPEG
                             : AVCOL_RANGE_JPEG;
    else
        hp->av_frame->color_range = AVCOL_RANGE_UNSPECIFIED;
}

static inline void get_color_matrix_coefficients
(
    vpy_handler_t *hp,
    const VSMap   *props
)
{
    if( hp->vsapi->propNumElements( props, "_Matrix" ) > 0 )
        hp->av_frame->colorspace = hp->vsapi->propGetInt( props, "_Matrix", 0, NULL );
    else
        hp->av_frame->colorspace = AVCOL_SPC_UNSPECIFIED;
}

static inline void get_interlaced_info
(
    vpy_handler_t *hp,
    const VSMap   *props
)
{
    if( hp->vsapi->propNumElements( props, "_FieldBased" ) > 0 )
    {
        int64_t value = hp->vsapi->propGetInt( props, "_FieldBased", 0, NULL );
        hp->av_frame->interlaced_frame = (value == 2 || value == 1 ? 1 : 0);
        if( hp->av_frame->interlaced_frame )
            hp->av_frame->top_field_first = (value == 2 ? 1 : 0);
    }
    else
    {
        hp->av_frame->interlaced_frame = 0;
        hp->av_frame->top_field_first  = 0;
    }
}

static void *open_file
(
    char            *file_name,
    reader_option_t *opt
)
{
    /* Check file extension. */
    if( lw_check_file_extension( file_name, "vpy" ) < 0 )
        return NULL;
    /* Try to open the file as VapourSynth script. */
    vpy_handler_t *hp = lw_malloc_zero( sizeof(vpy_handler_t) );
    if( !hp )
        return NULL;
    if( load_vsscript_dll( hp ) < 0 )
    {
        lw_free( hp );
        return NULL;
    }
    if( hp->vsscript.func.init() == 0 )
        goto fail;
    hp->vsapi = hp->vsscript.func.getVSApi();
    if( !hp->vsapi || hp->vsscript.func.evaluateFile( &hp->vsscript.handle, file_name, efSetWorkingDir ) )
        goto fail;
    hp->node = hp->vsscript.func.getOutput( hp->vsscript.handle, 0 );
    if( !hp->node )
        goto fail;
    hp->vi = hp->vsapi->getVideoInfo( hp->node );
    return hp;
fail:
    if( hp->library )
        close_vsscript_dll( hp );
    lw_free( hp );
    return NULL;
}

static int get_video_track
(
    lsmash_handler_t *h,
    video_option_t   *opt
)
{
    vpy_handler_t *hp = (vpy_handler_t *)h->video_private;
    if( hp->vi->numFrames <= 0 )
        return -1;
    hp->av_frame = av_frame_alloc();
    if( !hp->av_frame )
        return -1;
    return prepare_video_decoding( h, opt );
}

static int read_video
(
    lsmash_handler_t *h,
    int               sample_number,
    void             *buf
)
{
    vpy_handler_t *hp = (vpy_handler_t *)h->video_private;
    const VSFrameRef *vs_frame = hp->vsapi->getFrame( sample_number, hp->node, NULL, 0 );
    if( !vs_frame )
        return 0;
    const VSFormat *format = hp->vsapi->getFrameFormat( vs_frame );
    if( !format )
    {
        hp->vsapi->freeFrame( vs_frame );
        return 0;
    }
    hp->av_frame->format = vs_to_av_input_pixel_format( format );
    hp->av_frame->width  = hp->vsapi->getFrameWidth ( vs_frame, 0 );
    hp->av_frame->height = hp->vsapi->getFrameHeight( vs_frame, 0 );
    int is_rgb = vs_is_rgb_format( format->colorFamily );
    for( int i = 0; i < format->numPlanes; i++ )
    {
        static const int component_reorder[2][3] =
            {
                { 0, 1, 2 },    /* YUV -> YUV */
                { 2, 0, 1 }     /* RGB -> GBR */
            };
        int j = component_reorder[is_rgb][i];
        hp->av_frame->data    [j] = (uint8_t *)hp->vsapi->getReadPtr( vs_frame, i );
        hp->av_frame->linesize[j] =            hp->vsapi->getStride ( vs_frame, i );
    }
    const VSMap *props = hp->vsapi->getFramePropsRO( vs_frame );
    get_color_range              ( hp, props );
    get_color_matrix_coefficients( hp, props );
    get_interlaced_info          ( hp, props );
    /* Here, update_scaler_configuration_if_needed() is required to activate the scaler. */
    if( update_scaler_configuration_if_needed( &hp->voh.scaler, NULL, hp->av_frame ) < 0 )
        return 0;
    int frame_size = convert_colorspace( &hp->voh, hp->av_frame, buf );
    hp->vsapi->freeFrame( vs_frame );
    return frame_size;
}

static void video_cleanup
(
    lsmash_handler_t *h
)
{
    vpy_handler_t *hp = (vpy_handler_t *)h->video_private;
    if( !hp )
        return;
    av_frame_free( &hp->av_frame );
    lw_cleanup_video_output_handler( &hp->voh );
}

static void close_file
(
    void *private_stuff
)
{
    vpy_handler_t *hp = (vpy_handler_t *)private_stuff;
    if( !hp )
        return;
    if( hp->library )
        close_vsscript_dll( hp );
    lw_free( hp );
}

lsmash_reader_t vpy_reader =
{
    VPY_READER,
    open_file,
    get_video_track,
    NULL,
    NULL,
    read_video,
    NULL,
    NULL,
    NULL,
    video_cleanup,
    NULL,
    close_file
};
