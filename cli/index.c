/*****************************************************************************
 * index.c
 *****************************************************************************
 * Copyright (C) 2022 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *          Adam Fontenot <adam.m.fontenot@gmail.com>
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwindex.h"
#include "lwlibav_audio.h"
#include "lwlibav_dec.h"
#include "lwlibav_video.h"
#include "progress.h"
#include "utils.h"

#define PREFERRED_DECODER_NAMES_BUFSIZE 512

typedef struct {
    lwlibav_file_handler_t lwh;
    lwlibav_video_decode_handler_t* vdhp;
    lwlibav_video_output_handler_t* vohp;
    lwlibav_audio_decode_handler_t* adhp;
    lwlibav_audio_output_handler_t* aohp;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
} lwlibav_handler_t;

/* Deallocate the handler of this plugin. */
static void free_handler(lwlibav_handler_t** hpp)
{
    if (!hpp || !*hpp)
        return;
    lwlibav_handler_t* hp = *hpp;
    lw_free(lwlibav_video_get_preferred_decoder_names(hp->vdhp));
    lwlibav_video_free_decode_handler(hp->vdhp);
    lwlibav_video_free_output_handler(hp->vohp);
    lwlibav_audio_free_decode_handler(hp->adhp);
    lwlibav_audio_free_output_handler(hp->aohp);
    lw_free(hp->lwh.file_path);
    lw_free(hp);
}

/* Allocate the handler of this plugin. */
static lwlibav_handler_t* alloc_handler(void)
{
    lwlibav_handler_t* hp = (lwlibav_handler_t*)lw_malloc_zero(sizeof(lwlibav_handler_t));
    if (!hp)
        return NULL;
    if (!(hp->vdhp = lwlibav_video_alloc_decode_handler()) || !(hp->vohp = lwlibav_video_alloc_output_handler())
        || !(hp->adhp = lwlibav_audio_alloc_decode_handler()) || !(hp->aohp = lwlibav_audio_alloc_output_handler())) {
        free_handler(&hp);
        return NULL;
    }
    return hp;
}

static int update_indicator(progress_handler_t* php, const char* message, int percent)
{
    static int last_percent = -1;
    if (!strcmp(message, "Creating Index file") && last_percent != percent) {
        last_percent = percent;
        fprintf(stderr, "Creating lwi index file %d%%\r", percent);
        fflush(stderr);
    }
    return 0;
}

static void close_indicator(progress_handler_t* php)
{
    fprintf(stderr, "\n");
}

int main(const int argc, const char* argv[])
{
    bool has_index_path = false;
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s file.mkv [index.lwi]\n", argv[0]);
        return 1;
    } else if (argc == 3) {
        has_index_path = true;
    }

    /* Allocate the handler of this filter function. */
    lwlibav_handler_t* hp = alloc_handler();
    if (!hp) {
        fprintf(stderr, "Failed to allocate the LW-Libav handler.");
        return 1;
    }
    lwlibav_file_handler_t* lwhp = &hp->lwh;
    lwlibav_video_decode_handler_t* vdhp = hp->vdhp;
    lwlibav_video_output_handler_t* vohp = hp->vohp;
    /* Get options. */
    lwlibav_option_t opt;
    opt.file_path = argv[1];
    opt.cache_dir = "";
    opt.no_create_index = 0;
    opt.index_file_path = has_index_path ? argv[2] : NULL;
    opt.threads = 0;
    opt.force_video = 0;
    opt.force_video_index = -1;
    opt.force_audio = 0;
    opt.force_audio_index = -2;
    /* Set up progress indicator. */
    progress_indicator_t indicator;
    indicator.open = NULL;
    indicator.update = update_indicator;
    indicator.close = close_indicator;
    /* Construct index. */
    int ret = lwlibav_construct_index(lwhp, vdhp, vohp, hp->adhp, hp->aohp, NULL, &opt, &indicator, NULL);
    free_handler(&hp);
    if (ret < 0) {
        fprintf(stderr, "lsmas: failed to construct index for %s.", opt.file_path);
        return 1;
    }
    return 0;
}
