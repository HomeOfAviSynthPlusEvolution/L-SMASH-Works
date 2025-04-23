/*****************************************************************************
 * lwinput.h
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

#include <inttypes.h>
#include <mmreg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "input.h"

#include "../common/utils.h"
#include "lwcolor.h"

#define MAKE_AVIUTL_PITCH(x) ((((x) + 31) & ~31) >> 3)
#define PREFERRED_DECODER_NAMES_BUFSIZE 512

#define MESSAGE_BOX_DESKTOP(uType, ...)                   \
    do {                                                  \
        char temp[256];                                   \
        wsprintf(temp, __VA_ARGS__);                      \
        MessageBox(HWND_DESKTOP, temp, "lwinput", uType); \
    } while (0)

/* Macros for debug */
#if defined(DEBUG_VIDEO) || defined(DEBUG_AUDIO)
#define DEBUG_MESSAGE_BOX_DESKTOP(uType, ...) MESSAGE_BOX_DESKTOP(uType, __VA_ARGS__)
#else
#define DEBUG_MESSAGE_BOX_DESKTOP(uType, ...)
#endif

#ifdef DEBUG_VIDEO
#define DEBUG_VIDEO_MESSAGE_BOX_DESKTOP(uType, ...) DEBUG_MESSAGE_BOX_DESKTOP(uType, __VA_ARGS__)
#else
#define DEBUG_VIDEO_MESSAGE_BOX_DESKTOP(uType, ...)
#endif

#ifdef DEBUG_AUDIO
#define DEBUG_AUDIO_MESSAGE_BOX_DESKTOP(uType, ...) DEBUG_MESSAGE_BOX_DESKTOP(uType, __VA_ARGS__)
#else
#define DEBUG_AUDIO_MESSAGE_BOX_DESKTOP(uType, ...)
#endif

typedef enum {
    READER_NONE = 0,
    LIBAVSMASH_READER = 1,
    AVS_READER = 2,
    VPY_READER = 3,
    LIBAV_READER = 4,
    DUMMY_READER = 5,
} reader_type;

typedef struct {
    int seek_mode;
    int forward_seek_threshold;
    int scaler;
    int apply_repeat_flag;
    int field_dominance;
    struct {
        int active;
        int framerate_num;
        int framerate_den;
    } vfr2cfr;
    output_colorspace_index colorspace;
    struct {
        int width;
        int height;
        int framerate_num;
        int framerate_den;
        output_colorspace_index colorspace;
    } dummy;
    struct {
        int bit_depth;
    } avs;
} video_option_t;

enum {
    MIX_LEVEL_INDEX_CENTER = 0,
    MIX_LEVEL_INDEX_SURROUND,
    MIX_LEVEL_INDEX_LFE,
};

typedef struct {
    uint64_t channel_layout;
    int sample_rate;
    int mix_level[3]; /* { Center, Surround, LFE } */
} audio_option_t;

typedef struct {
    int threads;
    int av_sync;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
    const char** preferred_decoder_names;
    /* for libav reader */
    int no_create_index;
    int force_video;
    int force_video_index;
    int force_audio;
    int force_audio_index;
    /* for video stream */
    video_option_t video_opt;
    /* for audio stream */
    audio_option_t audio_opt;
} reader_option_t;

typedef struct lsmash_handler_tag lsmash_handler_t;

typedef struct {
    reader_type type;
    void* (*open_file)(char* file_name, reader_option_t* opt);
    int (*get_video_track)(lsmash_handler_t* h, video_option_t* opt);
    int (*get_audio_track)(lsmash_handler_t* h, audio_option_t* opt);
    void (*destroy_disposable)(void* private_stuff);
    int (*read_video)(lsmash_handler_t* h, int sample_number, void* buf);
    int (*read_audio)(lsmash_handler_t* h, int start, int wanted_length, void* buf);
    int (*is_keyframe)(lsmash_handler_t* h, int sample_number);
    int (*delay_audio)(lsmash_handler_t* h, int* start, int wanted_length, int audio_delay);
    void (*video_cleanup)(lsmash_handler_t* h);
    void (*audio_cleanup)(lsmash_handler_t* h);
    void (*close_file)(void* private_stuff);
} lsmash_reader_t;

struct lsmash_handler_tag {
    void* global_private;
    void (*close_file)(void* private_stuff);
    /* Video stuff */
    reader_type video_reader;
    void* video_private;
    BITMAPINFOHEADER video_format;
    int framerate_num;
    int framerate_den;
    uint32_t video_sample_count;
    int (*read_video)(lsmash_handler_t* h, int sample_number, void* buf);
    int (*is_keyframe)(lsmash_handler_t* h, int sample_number);
    void (*video_cleanup)(lsmash_handler_t* h);
    void (*close_video_file)(void* private_stuff);
    /* Audio stuff */
    reader_type audio_reader;
    void* audio_private;
    WAVEFORMATEXTENSIBLE audio_format;
    uint32_t audio_pcm_sample_count;
    int (*read_audio)(lsmash_handler_t* h, int start, int wanted_length, void* buf);
    int (*delay_audio)(lsmash_handler_t* h, int* start, int wanted_length, int audio_delay);
    void (*audio_cleanup)(lsmash_handler_t* h);
    void (*close_audio_file)(void* private_stuff);
};

typedef void func_get_output(uint8_t* out_data, int out_linesize, uint8_t** in_data, int in_linesize, int height, int full_range);

void au_message_box_desktop(lw_log_handler_t* lhp, lw_log_level level, const char* message);
