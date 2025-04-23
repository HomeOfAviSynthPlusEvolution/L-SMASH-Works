/*****************************************************************************
 * dummy_input.c
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
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "lwinput.h"

typedef struct {
    uint8_t* dummy_data;
    int dummy_size;
} dummy_handler_t;

static void* open_file(char* file_name, reader_option_t* opt)
{
    return lw_malloc_zero(sizeof(dummy_handler_t));
}

static int get_first_video_track(lsmash_handler_t* h, video_option_t* opt)
{
    if (h->audio_pcm_sample_count == 0 || h->audio_format.Format.nSamplesPerSec == 0)
        return -1; /* Only available if audio stream is present. */
    if (opt->dummy.framerate_den == 0)
        return -1;
    h->framerate_num = opt->dummy.framerate_num;
    h->framerate_den = opt->dummy.framerate_den;
    h->video_sample_count = ((uint64_t)h->framerate_num * h->audio_pcm_sample_count - 1)
            / ((uint64_t)h->framerate_den * h->audio_format.Format.nSamplesPerSec)
        + 1;
    static const struct {
        int pixel_size;
        output_colorspace_tag compression;
    } colorspace_table[3] = { { YUY2_SIZE, OUTPUT_TAG_YUY2 }, { RGB24_SIZE, OUTPUT_TAG_RGB }, { YC48_SIZE, OUTPUT_TAG_YC48 } };
    int linesize = MAKE_AVIUTL_PITCH(opt->dummy.width * (colorspace_table[opt->dummy.colorspace].pixel_size << 3));
    dummy_handler_t* hp = (dummy_handler_t*)h->video_private;
    hp->dummy_size = linesize * opt->dummy.height;
    if (hp->dummy_size <= 0)
        return -1;
    hp->dummy_data = lw_malloc_zero(hp->dummy_size);
    if (!hp->dummy_data)
        return -1;
    uint8_t* pic = hp->dummy_data;
    switch (colorspace_table[opt->dummy.colorspace].compression) {
    case OUTPUT_TAG_YC48:
    case OUTPUT_TAG_RGB:
        break;
    case OUTPUT_TAG_YUY2:
        for (int i = 0; i < opt->dummy.height; i++) {
            for (int j = 0; j < linesize; j += 2) {
                pic[j] = 0;
                pic[j + 1] = 128;
            }
            pic += linesize;
        }
        break;
    default:
        lw_freep(&hp->dummy_data);
        return -1;
    }
    /* BITMAPINFOHEADER */
    h->video_format.biSize = sizeof(BITMAPINFOHEADER);
    h->video_format.biWidth = opt->dummy.width;
    h->video_format.biHeight = opt->dummy.height;
    h->video_format.biBitCount = colorspace_table[opt->dummy.colorspace].pixel_size << 3;
    h->video_format.biCompression = colorspace_table[opt->dummy.colorspace].compression;
    return 0;
}

static int read_video(lsmash_handler_t* h, int sample_number, void* buf)
{
    /* Generate a black video frame. */
    dummy_handler_t* hp = (dummy_handler_t*)h->video_private;
    memcpy(buf, hp->dummy_data, hp->dummy_size);
    return hp->dummy_size;
}

static void close_file(void* private_stuff)
{
    dummy_handler_t* hp = (dummy_handler_t*)private_stuff;
    if (!hp)
        return;
    lw_free(hp->dummy_data);
    lw_free(hp);
}

lsmash_reader_t dummy_reader
    = { DUMMY_READER, open_file, get_first_video_track, NULL, NULL, read_video, NULL, NULL, NULL, NULL, NULL, close_file };
