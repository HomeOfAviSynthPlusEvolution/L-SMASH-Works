/*****************************************************************************
 * libavsmash_video.h
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
typedef lw_video_scaler_handler_t libavsmash_video_scaler_handler_t;
typedef lw_video_output_handler_t libavsmash_video_output_handler_t;

typedef struct libavsmash_video_decode_handler_tag libavsmash_video_decode_handler_t;

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
libavsmash_video_decode_handler_t *libavsmash_video_alloc_decode_handler
(
    void
);

libavsmash_video_output_handler_t *libavsmash_video_alloc_output_handler
(
    void
);

void libavsmash_video_free_decode_handler
(
    libavsmash_video_decode_handler_t *vdhp
);

void libavsmash_video_free_output_handler
(
    libavsmash_video_output_handler_t *vohp
);

void libavsmash_video_free_decode_handler_ptr
(
    libavsmash_video_decode_handler_t **vdhpp
);

void libavsmash_video_free_output_handler_ptr
(
    libavsmash_video_output_handler_t **vohpp
);

/*****************************************************************************
 * Setters
 *****************************************************************************/
void libavsmash_video_set_root
(
    libavsmash_video_decode_handler_t *vdhp,
    lsmash_root_t                     *root
);

void libavsmash_video_set_track_id
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           track_id
);

void libavsmash_video_set_forward_seek_threshold
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           forward_seek_threshold
);

void libavsmash_video_set_seek_mode
(
    libavsmash_video_decode_handler_t *vdhp,
    int                                seek_mode
);

void libavsmash_video_set_preferred_decoder_names
(
    libavsmash_video_decode_handler_t *vdhp,
    const char                       **preferred_decoder_names
);

void libavsmash_video_set_prefer_hw_decoder
(
    libavsmash_video_decode_handler_t *vdhp,
    int                                prefer_hw_decoder
);

void libavsmash_video_set_decoder_options
(
    libavsmash_video_decode_handler_t *vdhp,
    const char                        *ff_options
);

void libavsmash_video_set_log_handler
(
    libavsmash_video_decode_handler_t *vdhp,
    lw_log_handler_t                  *lh
);

void libavsmash_video_set_get_buffer_func
(
    libavsmash_video_decode_handler_t *vdhp
);

/*****************************************************************************
 * Getters
 *****************************************************************************/
lsmash_root_t *libavsmash_video_get_root
(
    libavsmash_video_decode_handler_t *vdhp
);

uint32_t libavsmash_video_get_track_id
(
    libavsmash_video_decode_handler_t *vdhp
);

uint32_t libavsmash_video_get_forward_seek_threshold
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_get_seek_mode
(
    libavsmash_video_decode_handler_t *vdhp
);

const char **libavsmash_video_get_preferred_decoder_names
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_get_error
(
    libavsmash_video_decode_handler_t *vdhp
);

lw_log_handler_t *libavsmash_video_get_log_handler
(
    libavsmash_video_decode_handler_t *vdhp
);

AVCodecContext *libavsmash_video_get_codec_context
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_get_max_width
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_get_max_height
(
    libavsmash_video_decode_handler_t *vdhp
);

AVFrame *libavsmash_video_get_frame_buffer
(
    libavsmash_video_decode_handler_t *vdhp
);

uint32_t libavsmash_video_get_sample_count
(
    libavsmash_video_decode_handler_t *vdhp
);

uint32_t libavsmash_video_get_media_timescale
(
    libavsmash_video_decode_handler_t *vdhp
);

uint64_t libavsmash_video_get_media_duration
(
    libavsmash_video_decode_handler_t *vdhp
);

/* This function must be called after a success of libavsmash_video_setup_timestamp_info(). */
uint64_t libavsmash_video_get_min_cts
(
    libavsmash_video_decode_handler_t *vdhp
);

/*****************************************************************************
 * Others
 *****************************************************************************/
int libavsmash_video_get_track
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           track_number
);

int libavsmash_video_initialize_decoder_configuration
(
    libavsmash_video_decode_handler_t *vdhp,
    AVFormatContext                   *format_ctx,
    int                                threads
);

int libavsmash_video_get_summaries
(
    libavsmash_video_decode_handler_t *vdhp
);

void libavsmash_video_force_seek
(
    libavsmash_video_decode_handler_t *vdhp
);

uint32_t libavsmash_video_get_coded_sample_number
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           composition_sample_number
);

int libavsmash_video_get_cts
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           coded_sample_number,
    uint64_t                          *cts
);

int libavsmash_video_get_sample_duration
(
    libavsmash_video_decode_handler_t *vdhp,
    uint32_t                           coded_sample_number,
    uint32_t                          *sample_duration
);

void libavsmash_video_clear_error
(
    libavsmash_video_decode_handler_t *vdhp
);

void libavsmash_video_close_codec_context
(
    libavsmash_video_decode_handler_t *vdhp
);

/* Setup average framerate and timestamp list.
 * This function sets an error if failed to get the minimum composition timestamp.
 * The minimum composition timestamp is used for VFR -> CFR conversion. */
int libavsmash_video_setup_timestamp_info
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    int64_t                           *framerate_num,
    int64_t                           *framerate_den
);

int libavsmash_video_get_frame
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    uint32_t                           sample_number
);

int libavsmash_video_find_first_valid_frame
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_create_keyframe_list
(
    libavsmash_video_decode_handler_t *vdhp
);

int libavsmash_video_is_keyframe
(
    libavsmash_video_decode_handler_t *vdhp,
    libavsmash_video_output_handler_t *vohp,
    uint32_t                           sample_number
);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
