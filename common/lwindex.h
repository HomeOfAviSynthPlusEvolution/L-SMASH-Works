/*****************************************************************************
 * lwindex.h
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

/* lwindex version
 * Major.Minor.Micro.Build */
#define LWINDEX_VERSION ((0 << 24) | (0 << 16) | (2 << 8) | 0)

/* index file version
 * This version is bumped when its structure changed so that the lwindex invokes
 * reindexing opened file immediately. */
#define LWINDEX_INDEX_FILE_VERSION 13

typedef struct
{
    const char *file_path;
    int         threads;
    int         av_sync;
    int         no_create_index;
    const char *index_file_path;
    int         force_video;
    int         force_video_index;
    int         force_audio;
    int         force_audio_index;
    int         apply_repeat_flag;
    int         field_dominance;
    struct
    {
        int      active;
        uint32_t fps_num;
        uint32_t fps_den;
    } vfr2cfr;
} lwlibav_option_t;

int lwlibav_construct_index
(
    lwlibav_file_handler_t         *lwhp,
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    lwlibav_audio_decode_handler_t *adhp,
    lwlibav_audio_output_handler_t *aohp,
    lw_log_handler_t               *lhp,
    lwlibav_option_t               *opt,
    progress_indicator_t           *indicator,
    progress_handler_t             *php
);

int lwlibav_import_av_index_entry
(
    lwlibav_decode_handler_t *dhp
);
