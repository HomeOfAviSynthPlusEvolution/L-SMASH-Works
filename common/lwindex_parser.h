/*****************************************************************************
 * lwindex_parser.h
 *****************************************************************************
 * Copyright (C) 2012-2025 L-SMASH Works project
 *
 * Authors: Xinyue Lu <i@7086.in>
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

#ifndef LWINDEX_PARSER_H
#define LWINDEX_PARSER_H

#include "cpp_compat.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LINE_LENGTH 8192
#define MAX_TAG_LENGTH 64
#define MAX_VALUE_LENGTH 6144
#define MAX_FILE_PATH_LENGTH 6144
#define MAX_INDEX_ENTRIES (1 << 31)
#define INIT_INDEX_ENTRIES (1 << 18)
#define MAX_STREAM_ID 256
#define MAX_EXTRA_DATA_LIST 64
#define FORMAT_LENGTH 64

enum av_stream_type {
    AV_STREAM_TYPE_VIDEO = 0,
    AV_STREAM_TYPE_AUDIO = 1,
};

enum index_entry_scope {
    INDEX_ENTRY_SCOPE_INVALID,
    INDEX_ENTRY_SCOPE_GLOBAL,
    INDEX_ENTRY_SCOPE_STREAM,
};

typedef struct {
    int num;
    int den;
} rational_t;

typedef struct {
    int64_t pos;
    int64_t ts;
    int32_t size;
    int32_t flags : 4;
    int32_t distance : 28;
} stream_index_entry_t; // Extensively used, 24 bytes

typedef struct {
    uint32_t stream_index : 8;
    uint32_t codec_type : 2; // 0 for type0, 1 for type1
    uint32_t codec;
    rational_t time_base;
    char format[FORMAT_LENGTH];
    int32_t bits_per_sample;
    int64_t stream_duration;

    stream_index_entry_t* stream_index_entries;
    uint32_t num_stream_index_entries;

    union {
        struct {
            int32_t width;
            int32_t height;
            int32_t color_space;
        } type0;
        struct {
            uint64_t layout;
            int32_t channels;
            int32_t sample_rate;
        } type1;
    } data;
} stream_info_entry_t;

typedef struct {
    uint32_t size;
    uint32_t codec;
    uint32_t fourcc;
    char format[FORMAT_LENGTH];
    int32_t bits_per_sample;

    union {
        struct {
            int32_t width;
            int32_t height;
            int32_t color_space;
        } type0;
        struct {
            uint64_t layout;
            int32_t sample_rate;
            int32_t block_align;
        } type1;
    } data;
    char* binary_data;
} extra_data_entry_t;

typedef struct {
    uint32_t stream_index : 8;
    uint32_t codec_type : 2; // 0 for type0, 1 for type1
    uint32_t entry_count;
    extra_data_entry_t* entries;
} extra_data_list_t;

typedef struct {
    int64_t pts;
    int64_t dts;
    int64_t pos;
    uint32_t stream_index : 8;
    uint32_t codec_type : 2; // 0 for type0, 1 for type1
    uint32_t edi : 6;

    union {
        struct {
            uint32_t key : 1;
            uint32_t super : 1;
            uint32_t repeat : 3;
            uint32_t field : 2;
            uint32_t pic : 3;
            int32_t poc : 22;
        } type0;
        struct {
            uint32_t length;
        } type1;
    } data;
} index_entry_t; // Extensively used, 32 bytes

typedef struct {
    char lsmash_works_index_version[16];
    int libav_reader_index_file;
    char input_file_path[MAX_FILE_PATH_LENGTH];
    uint64_t file_size;
    int64_t file_last_modification_time;
    uint64_t file_hash;
    char format_name[256];
    int format_flags;
    int raw_demuxer;
    int active_video_stream_index;
    int active_audio_stream_index;
    int default_audio_stream_index;
    int fill_audio_gaps;
    stream_info_entry_t* stream_info;
    int num_streams;

    index_entry_t* index_entries;
    int num_index_entries;
    extra_data_list_t* extra_data_list;
    int num_extra_data_list;
    int consistent_field_and_repeat;
    int64_t active_video_stream_index_pos;
    int64_t active_audio_stream_index_pos;
} lwindex_data_t;

lwindex_data_t* lwindex_parse(FILE* index, int include_video, int include_audio);
void lwindex_free(lwindex_data_t* data);

#endif // LWINDEX_PARSER_H
