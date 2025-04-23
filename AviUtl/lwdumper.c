/*****************************************************************************
 * lwdumper.c
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

/* This file is available under an ISC license. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* L-SMASH */
#include <lsmash.h>

#include "filter.h"

#include "config.h"

/* Macros for debug */
#ifdef DEBUG
#define DEBUG_MESSAGE_BOX_DESKTOP(uType, ...)             \
    do {                                                  \
        char temp[512];                                   \
        wsprintf(temp, __VA_ARGS__);                      \
        MessageBox(HWND_DESKTOP, temp, "lwmuxer", uType); \
    } while (0)
#else
#define DEBUG_MESSAGE_BOX_DESKTOP(uType, ...)
#endif

/* File filter */
#define DUMP_FILE_EXT "Dump File (*.txt)\0*.txt\0"
#define TIMECODE_FILE_EXT "Timecode v2 File (*.tmc)\0*.tmc\0"

FILTER_DLL filter = {
    FILTER_FLAG_EXPORT | FILTER_FLAG_NO_CONFIG | FILTER_FLAG_ALWAYS_ACTIVE | FILTER_FLAG_PRIORITY_LOWEST | FILTER_FLAG_EX_INFORMATION, 0,
    0, /* Size of configuration window */
    "L-SMASH Works Dumper", /* Name of filter plugin */
    0, /* Number of trackbars */
    NULL, /* Pointer to group of names of trackbar */
    NULL, /* Pointer to group of initial values of trackbar */
    NULL, /* Minimum of trackbar */
    NULL, /* Maximum of trackbar */
    0, /* Number of checkboxes */
    NULL, /* Pointer to group of names of checkbox */
    NULL, /* Pointer to group of initial values of checkbox */
    NULL, /* Pointer to filter process function (If NULL, won't be called.) */
    NULL, /* Pointer to function called when beginning (If NULL, won't be called.) */
    NULL, /* Pointer to function called when ending (If NULL, won't be called.) */
    NULL, /* Pointer to function called when its configuration is updated (If NULL, won't be called.) */
    func_WndProc, /* Pointer to function called when window message comes on configuration window (If NULL, won't be called.) */
    NULL, /* Pointer to group of set points of trackbar */
    NULL, /* Pointer to group of set points of checkbox */
    NULL, /* Pointer to extended data region (Valid only if FILTER_FLAG_EX_DATA is enabled.) */
    0, /* Size of extended data (Valid only if FILTER_FLAG_EX_DATA is enabled.) */
    "L-SMASH Works Dumper r" LSMASHWORKS_REV "\0", /* Information of filter plugin */
};

EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable(void)
{
    return &filter;
}

EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTableYUY2(void)
{
    return &filter;
}

static int check_extension(char* file_name, char* ext)
{
    int ext_length = strlen(ext);
    int file_name_length = strlen(file_name);
    if (file_name_length < ext_length)
        return 0;
    return !memcmp(file_name + file_name_length - ext_length, ext, ext_length);
}

static uint32_t get_first_video_track_ID(lsmash_root_t* root)
{
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters(&movie_param);
    lsmash_get_movie_parameters(root, &movie_param);
    uint32_t number_of_tracks = movie_param.number_of_tracks;
    if (number_of_tracks == 0)
        return 0;
    uint32_t track_ID = 0;
    uint32_t i;
    for (i = 1; i <= number_of_tracks; i++) {
        track_ID = lsmash_get_track_ID(root, i);
        if (track_ID == 0)
            return 0;
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters(&media_param);
        if (lsmash_get_media_parameters(root, track_ID, &media_param)) {
            DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get media parameters.");
            return 0;
        }
        if (media_param.handler_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK)
            break;
    }
    if (i > number_of_tracks) {
        DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to find video track.");
        return 0;
    }
    return track_ID;
}

static int get_media_timestamps(lsmash_root_t* root, uint32_t track_ID, lsmash_media_ts_list_t* ts_list)
{
    if (lsmash_get_media_timestamps(root, track_ID, ts_list)) {
        DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get media timestamps.");
        return -1;
    }
    lsmash_destruct_timeline(root, track_ID);
    uint32_t composition_sample_delay;
    if (lsmash_get_max_sample_delay(ts_list, &composition_sample_delay)) {
        DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get composition delay.");
        lsmash_delete_media_timestamps(ts_list);
        return -1;
    }
    if (composition_sample_delay)
        lsmash_sort_timestamps_composition_order(ts_list);
    return 0;
}

static int output_timecodes(char* file_name, lsmash_media_ts_list_t* ts_list, uint32_t media_timescale)
{
    FILE* tmc = fopen(file_name, "wb");
    if (!tmc)
        return -1;
    fprintf(tmc, "# timecode format v2\n");
    for (uint32_t i = 0; i < ts_list->sample_count; i++) {
        double timecode = ((double)(ts_list->timestamp[i].cts - ts_list->timestamp[0].cts) / media_timescale) * 1e3;
        fprintf(tmc, "%.6f\n", timecode);
    }
    fflush(tmc);
    fclose(tmc);
    return 0;
}

BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, void* editp, FILTER* fp)
{
    if (!fp->exfunc->is_editing(editp))
        return FALSE;
    if (message != WM_FILTER_EXPORT)
        return FALSE;
    /* Get the input file info. */
    FRAME_STATUS fs;
    if (!fp->exfunc->get_frame_status(editp, 0, &fs)) {
        MessageBox(HWND_DESKTOP, "Failed to get the status of the first frame.", "lwdumper", MB_ICONERROR | MB_OK);
        return -1;
    }
    int source_file_id;
    int source_video_number;
    if (!fp->exfunc->get_source_video_number(editp, fs.video, &source_file_id, &source_video_number)) {
        MessageBox(HWND_DESKTOP, "Failed to get the number of the source video.", "lwdumper", MB_ICONERROR | MB_OK);
        return -1;
    }
    FILE_INFO fi;
    if (!fp->exfunc->get_source_file_info(editp, &fi, source_file_id)) {
        MessageBox(HWND_DESKTOP, "Failed to get the information of the source file.", "lwdumper", MB_ICONERROR | MB_OK);
        return -1;
    }
    /* Get the output file name. */
    char file_name[MAX_PATH];
    if (!fp->exfunc->dlg_get_save_name((LPSTR)file_name, DUMP_FILE_EXT TIMECODE_FILE_EXT, NULL))
        return FALSE;
    /* Open the input file. */
    lsmash_root_t* root = lsmash_create_root();
    if (!root) {
        fprintf(stderr, "Failed to create a ROOT.\n");
        return -1;
    }
    lsmash_file_parameters_t file_param = { 0 };
    if (lsmash_open_file(fi.name, 1, &file_param) < 0) {
        MessageBox(HWND_DESKTOP, "Failed to open an input file.", "lwdumper", MB_ICONERROR | MB_OK);
        goto fail;
    }
    if (check_extension(file_name, ".txt"))
        file_param.mode |= LSMASH_FILE_MODE_DUMP;
    else if (!check_extension(file_name, ".tmc")) {
        MessageBox(HWND_DESKTOP, "Failed to decide the output file format.", "lwdumper", MB_ICONERROR | MB_OK);
        goto fail;
    }
    lsmash_file_t* file = lsmash_set_file(root, &file_param);
    if (!file) {
        MessageBox(HWND_DESKTOP, "Failed to add a file into a ROOT.", "lwdumper", MB_ICONERROR | MB_OK);
        goto fail;
    }
    if (lsmash_read_file(file, &file_param) < 0) {
        MessageBox(HWND_DESKTOP, "Failed to read a file.", "lwdumper", MB_ICONERROR | MB_OK);
        goto fail;
    }
    if (file_param.mode & LSMASH_FILE_MODE_DUMP) {
        /* Open the output file to dump the input file. */
        if (lsmash_print_movie(root, file_name)) {
            MessageBox(HWND_DESKTOP, "Failed to dump the box structure.", "lwdumper", MB_ICONERROR | MB_OK);
            goto fail;
        }
    } else {
        /* Output timecode v2 file. */
        uint32_t track_ID = get_first_video_track_ID(root);
        if (track_ID == 0) {
            MessageBox(HWND_DESKTOP, "Failed to get video track_ID.", "lwdumper", MB_ICONERROR | MB_OK);
            goto fail;
        }
        uint32_t media_timescale = lsmash_get_media_timescale(root, track_ID);
        if (media_timescale == 0) {
            MessageBox(HWND_DESKTOP, "Failed to get video timescale.", "lwdumper", MB_ICONERROR | MB_OK);
            goto fail;
        }
        if (lsmash_construct_timeline(root, track_ID)) {
            MessageBox(HWND_DESKTOP, "Failed to construct timeline.", "lwdumper", MB_ICONERROR | MB_OK);
            goto fail;
        }
        lsmash_discard_boxes(root);
        lsmash_media_ts_list_t ts_list;
        if (get_media_timestamps(root, track_ID, &ts_list)) {
            MessageBox(HWND_DESKTOP, "Failed to get media timestamps.", "lwdumper", MB_ICONERROR | MB_OK);
            goto fail;
        }
        if (output_timecodes(file_name, &ts_list, media_timescale))
            MessageBox(HWND_DESKTOP, "Failed to open the output file.", "lwdumper", MB_ICONERROR | MB_OK);
        lsmash_delete_media_timestamps(&ts_list);
    }
fail:
    lsmash_close_file(&file_param);
    lsmash_destroy_root(root);
    return FALSE;
}
