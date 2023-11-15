/*****************************************************************************
 * lwlibav_audio.h
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
typedef lw_audio_output_handler_t lwlibav_audio_output_handler_t;

typedef struct lwlibav_audio_decode_handler_tag lwlibav_audio_decode_handler_t;

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
lwlibav_audio_decode_handler_t *lwlibav_audio_alloc_decode_handler
(
    void
);

lwlibav_audio_output_handler_t *lwlibav_audio_alloc_output_handler
(
    void
);

void lwlibav_audio_free_decode_handler
(
    lwlibav_audio_decode_handler_t *adhp
);

void lwlibav_audio_free_output_handler
(
    lwlibav_audio_output_handler_t *aohp
);

void lwlibav_audio_free_decode_handler_ptr
(
    lwlibav_audio_decode_handler_t **adhpp
);

void lwlibav_audio_free_output_handler_ptr
(
    lwlibav_audio_output_handler_t **aohpp
);

/*****************************************************************************
 * Setters
 *****************************************************************************/
void lwlibav_audio_set_preferred_decoder_names
(
    lwlibav_audio_decode_handler_t *adhp,
    const char                    **preferred_decoder_names
);

void lwlibav_audio_set_decoder_options
(
    lwlibav_audio_decode_handler_t *adhp,
    const char                     *ff_options
);

/*****************************************************************************
 * Getters
 *****************************************************************************/
const char **lwlibav_audio_get_preferred_decoder_names
(
    lwlibav_audio_decode_handler_t *adhp
);

lw_log_handler_t *lwlibav_audio_get_log_handler
(
    lwlibav_audio_decode_handler_t *adhp
);

AVCodecContext *lwlibav_audio_get_codec_context
(
    lwlibav_audio_decode_handler_t *adhp
);

/*****************************************************************************
 * Others
 *****************************************************************************/
void lwlibav_audio_force_seek
(
    lwlibav_audio_decode_handler_t *adhp
);

int lwlibav_audio_get_desired_track
(
    const char                     *file_path,
    lwlibav_audio_decode_handler_t *adhp,
    int                             threads
);

uint64_t lwlibav_audio_count_overall_pcm_samples
(
    lwlibav_audio_decode_handler_t *adhp,
    int                             output_sample_rate
);

uint64_t lwlibav_audio_get_pcm_samples
(
    lwlibav_audio_decode_handler_t *adhp,
    lwlibav_audio_output_handler_t *aohp,
    void                           *buf,
    int64_t                         start,
    int64_t                         wanted_length
);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
