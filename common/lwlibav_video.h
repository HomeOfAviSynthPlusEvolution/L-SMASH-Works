/*****************************************************************************
 * lwlibav_video.h
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
typedef lw_video_scaler_handler_t lwlibav_video_scaler_handler_t;
typedef lw_video_output_handler_t lwlibav_video_output_handler_t;

typedef struct lwlibav_video_decode_handler_tag lwlibav_video_decode_handler_t;

/*****************************************************************************
 * Enumerators
 *****************************************************************************/
typedef enum lw_field_info
{
    LW_FIELD_INFO_UNKNOWN = 0,  /* unknown */
    LW_FIELD_INFO_TOP,          /* top field first or top field coded */
    LW_FIELD_INFO_BOTTOM,       /* bottom field first or bottom field coded */
} lw_field_info_t;

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/*****************************************************************************
 * Allocators / Deallocators
 *****************************************************************************/
lwlibav_video_decode_handler_t *lwlibav_video_alloc_decode_handler
(
    void
);

lwlibav_video_output_handler_t *lwlibav_video_alloc_output_handler
(
    void
);

void lwlibav_video_free_decode_handler
(
    lwlibav_video_decode_handler_t *vdhp
);

void lwlibav_video_free_output_handler
(
    lwlibav_video_output_handler_t *vohp
);

void lwlibav_video_free_decode_handler_ptr
(
    lwlibav_video_decode_handler_t **vdhpp
);

void lwlibav_video_free_output_handler_ptr
(
    lwlibav_video_output_handler_t **vohpp
);

/*****************************************************************************
 * Setters
 *****************************************************************************/
void lwlibav_video_set_forward_seek_threshold
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        forward_seek_threshold
);

void lwlibav_video_set_seek_mode
(
    lwlibav_video_decode_handler_t *vdhp,
    int                             seek_mode
);

void lwlibav_video_set_preferred_decoder_names
(
    lwlibav_video_decode_handler_t *vdhp,
    const char                    **preferred_decoder_names
);

void lwlibav_video_set_prefer_hw_decoder
(
    lwlibav_video_decode_handler_t *vdhp,
    int                             prefer_hw_decoder
);

void lwlibav_video_set_log_handler
(
    lwlibav_video_decode_handler_t *vdhp,
    lw_log_handler_t               *lh
);

void lwlibav_video_set_get_buffer_func
(
    lwlibav_video_decode_handler_t *vdhp
);

/*****************************************************************************
 * Getters
 *****************************************************************************/
const char **lwlibav_video_get_preferred_decoder_names
(
    lwlibav_video_decode_handler_t *vdhp
);

int lwlibav_video_get_error
(
    lwlibav_video_decode_handler_t *vdhp
);

lw_log_handler_t *lwlibav_video_get_log_handler
(
    lwlibav_video_decode_handler_t *vdhp
);

AVCodecContext *lwlibav_video_get_codec_context
(
    lwlibav_video_decode_handler_t *vdhp
);

int lwlibav_video_get_max_width
(
    lwlibav_video_decode_handler_t *vdhp
);

int lwlibav_video_get_max_height
(
    lwlibav_video_decode_handler_t *vdhp
);

AVFrame *lwlibav_video_get_frame_buffer
(
    lwlibav_video_decode_handler_t *vdhp
);

/*****************************************************************************
 * Others
 *****************************************************************************/
void lwlibav_video_force_seek
(
    lwlibav_video_decode_handler_t *vdhp
);

int lwlibav_video_get_desired_track
(
    const char                     *file_path,
    lwlibav_video_decode_handler_t *vdhp,
    int                             threads
);

void lwlibav_video_setup_timestamp_info
(
    lwlibav_file_handler_t         *lwhp,
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    int64_t                        *framerate_num,
    int64_t                        *framerate_den,
    int                             apply_repeat_flag
);

void lwlibav_video_set_initial_input_format
(
    lwlibav_video_decode_handler_t *vdhp
);

int lwlibav_video_get_frame
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
);

int lwlibav_video_is_keyframe
(
    lwlibav_video_decode_handler_t *vdhp,
    lwlibav_video_output_handler_t *vohp,
    uint32_t                        frame_number
);

int lwlibav_video_find_first_valid_frame
(
    lwlibav_video_decode_handler_t *vdhp
);

enum lw_field_info lwlibav_video_get_field_info
(
    lwlibav_video_decode_handler_t *vdhp,
    uint32_t                        frame_number
);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
