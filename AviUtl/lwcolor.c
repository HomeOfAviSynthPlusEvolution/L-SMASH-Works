/*****************************************************************************
 * lwcolor.c
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

/* This file is available under an ISC license. */

#include <windows.h>

#include "../common/lwsimd.h"

#include "color.h"

#include "lwcolor.h"
#include "lwcolor_simd.h"
#include "config.h"

static void convert_lw48_to_yuy2( int thread_id, int thread_num, void *param1, void *param2 );
static void convert_lw48_to_rgb24( int thread_id, int thread_num, void *param1, void *param2 );

static MULTI_THREAD_FUNC func_convert_lw48_to_yuy2  = NULL;
static MULTI_THREAD_FUNC func_convert_lw48_to_rgb24 = NULL;

COLOR_PLUGIN_TABLE color_plugin_table =
{
    0,                                      /* flags */
    "LW ColorSpace",                        /* Name of plugin */
    "L-SMASH Works Color Space Converter"   /* Information of plugin */
    " r" LSMASHWORKS_REV "\0",
    func_init,                              /* Pointer to function called when opening DLL (If NULL, won't be called.) */
    NULL,                                   /* Pointer to function called when closing DLL (If NULL, won't be called.) */
    func_pixel2yc,                          /* Convert DIB format image into PIXEL_YC format image (If NULL, won't be called.) */
    func_yc2pixel,                          /* Convert PIXEL_YC format image into DIB format image (If NULL, won't be called.) */
};

EXTERN_C COLOR_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetColorPluginTable( void )
{
    return &color_plugin_table;
}

EXTERN_C COLOR_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetColorPluginTableYUY2( void )
{
    return &color_plugin_table;
}

BOOL func_init( void )
{
    if( lw_check_sse41() )
    {
        func_convert_lw48_to_yuy2  = convert_lw48_to_yuy2_sse41;
        func_convert_lw48_to_rgb24 = convert_lw48_to_rgb24_sse41;
    }
    else
    {
        func_convert_lw48_to_yuy2  = convert_lw48_to_yuy2;
        func_convert_lw48_to_rgb24 = convert_lw48_to_rgb24;
    }
    return TRUE;
}

/****************************************************************************
 * INPUT
 ****************************************************************************/

BOOL func_pixel2yc( COLOR_PROC_INFO *cpip )
{
    if( cpip->format != OUTPUT_TAG_LW48 )
        return FALSE;
    /* LW48 -> LW48 */
    BYTE *ycp    = (BYTE *)cpip->ycp;
    BYTE *pixelp = (BYTE *)cpip->pixelp;
    int linesize = LW48_SIZE * cpip->w;
    int stridesize = (linesize + 3) & ~3;
    for( int y = 0; y < cpip->h; y++ )
    {
        memcpy( ycp, pixelp, linesize );
        ycp    += cpip->line_size;
        pixelp += stridesize;
    }
    return TRUE;
}

/****************************************************************************
 * OUTPUT
 ****************************************************************************/

static void convert_lw48_to_yuy2( int thread_id, int thread_num, void *param1, void *param2 )
{
    /* LW48 -> YUY2 */
    COLOR_PROC_INFO *cpip = (COLOR_PROC_INFO *)param1;
    int start = (cpip->h *  thread_id     ) / thread_num;
    int end   = (cpip->h * (thread_id + 1)) / thread_num;
    BYTE *src = (BYTE *)cpip->ycp    + start * cpip->line_size;
    BYTE *dst = (BYTE *)cpip->pixelp + start * cpip->w * 2;
    for( int y = start; y < end; y++ )
    {
        PIXEL_LW48 *ycp = (PIXEL_LW48 *)src;
        for( int x = 0; x < cpip->w; x += 2 )
        {
            dst[0] = ycp->y  >> 8;
            dst[1] = ycp->cb >> 8;
            dst[3] = ycp->cr >> 8;
            ++ycp;
            dst[2] = ycp->y  >> 8;
            ++ycp;
            dst += 4;
        }
        src += cpip->line_size;
    }
}

#define CLIP_BYTE( value ) ((value) > 255 ? 255 : (value) < 0 ? 0 : (value))

static void convert_lw48_to_rgb24( int thread_id, int thread_num, void *param1, void *param2 )
{
    /* LW48 -> RGB24 */
    COLOR_PROC_INFO *cpip = (COLOR_PROC_INFO *)param1;
    int start = (cpip->h *  thread_id     ) / thread_num;
    int end   = (cpip->h * (thread_id + 1)) / thread_num;
    int rgb_linesize = (cpip->w * 3 + 3) & ~3;
    BYTE *src_line = (BYTE *)cpip->ycp + (end - 1) * cpip->line_size;
    BYTE *dst_line = (BYTE *)cpip->pixelp + (cpip->h - end) * rgb_linesize;
    for( int y = start; y < end; y++ )
    {
        PIXEL_LW48 *ycp     = (PIXEL_LW48 *)src_line;
        BYTE       *rgb_ptr = dst_line;
        for( int x = 0; x < cpip->w; x++ )
        {
            int _y  = (ycp->y - 4096) * 9539;
            int _cb = ycp->cb - 32768;
            int _cr = ycp->cr - 32768;
            ++ycp;
            int r = (_y               + 13074 * _cr + (1<<20)) >> 21;
            int g = (_y -  3203 * _cb -  6808 * _cr + (1<<20)) >> 21;
            int b = (_y + 16531 * _cb               + (1<<20)) >> 21;
            rgb_ptr[0] = CLIP_BYTE( b );
            rgb_ptr[1] = CLIP_BYTE( g );
            rgb_ptr[2] = CLIP_BYTE( r );
            rgb_ptr += 3;
        }
        src_line -= cpip->line_size;
        dst_line += rgb_linesize;
    }
}

BOOL func_yc2pixel( COLOR_PROC_INFO *cpip )
{
    switch( cpip->format )
    {
        case OUTPUT_TAG_LW48 :
        {
            /* LW48 -> LW48 */
            BYTE *ycp    = (BYTE *)cpip->ycp;
            BYTE *pixelp = (BYTE *)cpip->pixelp;
            int linesize = LW48_SIZE * cpip->w;
            for( int y = 0; y < cpip->h; y++ )
            {
                memcpy( pixelp, ycp, linesize );
                ycp    += cpip->line_size;
                pixelp += linesize;
            }
            return TRUE;
        }
        case OUTPUT_TAG_YUY2 :
            /* LW48 -> YUY2 */
            cpip->exec_multi_thread_func( func_convert_lw48_to_yuy2, (void *)cpip, NULL );
            return TRUE;
        case OUTPUT_TAG_RGB :
            /* LW48 -> RGB24 */
            cpip->exec_multi_thread_func( func_convert_lw48_to_rgb24, (void *)cpip, NULL );
            return TRUE;
        default :
            return FALSE;
    }
}
