/*****************************************************************************
 * libavsmash_audio.h
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

/* This file is available under an ISC license. */

/*****************************************************************************
 * Opaque Handlers
 *****************************************************************************/
typedef lw_audio_output_handler_t libavsmash_audio_output_handler_t;

typedef struct libavsmash_audio_decode_handler_tag libavsmash_audio_decode_handler_t;

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
libavsmash_audio_decode_handler_t *libavsmash_audio_alloc_decode_handler
(
    void
);

libavsmash_audio_output_handler_t *libavsmash_audio_alloc_output_handler
(
    void
);

void libavsmash_audio_free_decode_handler
(
    libavsmash_audio_decode_handler_t *adhp
);

void libavsmash_audio_free_output_handler
(
    libavsmash_audio_output_handler_t *aohp
);

void libavsmash_audio_free_decode_handler_ptr
(
    libavsmash_audio_decode_handler_t **adhpp
);

void libavsmash_audio_free_output_handler_ptr
(
    libavsmash_audio_output_handler_t **aohpp
);

/*****************************************************************************
 * Setters
 *****************************************************************************/
void libavsmash_audio_set_root
(
    libavsmash_audio_decode_handler_t *adhp,
    lsmash_root_t                     *root
);

void libavsmash_audio_set_track_id
(
    libavsmash_audio_decode_handler_t *adhp,
    uint32_t                           track_id
);

void libavsmash_audio_set_preferred_decoder_names
(
    libavsmash_audio_decode_handler_t *adhp,
    const char                       **preferred_decoder_names
);

void libavsmash_audio_set_drc
(
    libavsmash_audio_decode_handler_t* adhp,
    const double                       drc
);

void libavsmash_audio_set_decoder_options
(
    libavsmash_audio_decode_handler_t *adhp,
    const char                        *ff_options
);

/*****************************************************************************
 * Getters
 *****************************************************************************/
lsmash_root_t *libavsmash_audio_get_root
(
    libavsmash_audio_decode_handler_t *adhp
);

uint32_t libavsmash_audio_get_track_id
(
    libavsmash_audio_decode_handler_t *adhp
);

AVCodecContext *libavsmash_audio_get_codec_context
(
    libavsmash_audio_decode_handler_t *adhp
);

const char **libavsmash_audio_get_preferred_decoder_names
(
    libavsmash_audio_decode_handler_t *adhp
);

int libavsmash_audio_get_error
(
    libavsmash_audio_decode_handler_t *adhp
);

uint64_t libavsmash_audio_get_best_used_channel_layout
(
    libavsmash_audio_decode_handler_t *adhp
);

enum AVSampleFormat libavsmash_audio_get_best_used_sample_format
(
    libavsmash_audio_decode_handler_t *adhp
);

int libavsmash_audio_get_best_used_sample_rate
(
    libavsmash_audio_decode_handler_t *adhp
);

int libavsmash_audio_get_best_used_bits_per_sample
(
    libavsmash_audio_decode_handler_t *adhp
);

lw_log_handler_t *libavsmash_audio_get_log_handler
(
    libavsmash_audio_decode_handler_t *adhp
);

uint32_t libavsmash_audio_get_sample_count
(
    libavsmash_audio_decode_handler_t *adhp
);

uint32_t libavsmash_audio_get_media_timescale
(
    libavsmash_audio_decode_handler_t *adhp
);

uint64_t libavsmash_audio_get_media_duration
(
    libavsmash_audio_decode_handler_t *adhp
);

/* Return UINT64_MAX if failed. */
uint64_t libavsmash_audio_get_min_cts
(
    libavsmash_audio_decode_handler_t *adhp
);

/*****************************************************************************
 * Others
 *****************************************************************************/
int libavsmash_audio_get_track
(
    libavsmash_audio_decode_handler_t *adhp,
    uint32_t                           track_number
);

int libavsmash_audio_initialize_decoder_configuration
(
    libavsmash_audio_decode_handler_t *adhp,
    AVFormatContext                   *format_ctx,
    int                                threads
);

int libavsmash_audio_get_summaries
(
    libavsmash_audio_decode_handler_t *adhp
);

void libavsmash_audio_force_seek
(
    libavsmash_audio_decode_handler_t *adhp
);

void libavsmash_audio_clear_error
(
    libavsmash_audio_decode_handler_t *adhp
);

void libavsmash_audio_close_codec_context
(
    libavsmash_audio_decode_handler_t *adhp
);

void libavsmash_audio_apply_delay
(
    libavsmash_audio_decode_handler_t *adhp,
    int64_t                            delay
);

void libavsmash_audio_set_implicit_preroll
(
    libavsmash_audio_decode_handler_t *adhp
);

uint64_t libavsmash_audio_count_overall_pcm_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    int                                output_sample_rate,
    uint64_t                           start_time
);

uint64_t libavsmash_audio_get_pcm_samples
(
    libavsmash_audio_decode_handler_t *adhp,
    libavsmash_audio_output_handler_t *aohp,
    void                              *buf,
    int64_t                            start,
    int64_t                            wanted_length
);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
