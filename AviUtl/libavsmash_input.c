/*****************************************************************************
 * libavsmash_input.c
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

/* L-SMASH */
#include <lsmash.h> /* Demuxer */

/* Libav
 * The binary file will be LGPLed or GPLed. */
#include <libavcodec/avcodec.h> /* Decoder */
#include <libavformat/avformat.h> /* Codec specific info importer */
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h> /* Audio resampler */
#include <libswscale/swscale.h> /* Colorspace converter */

#include "audio_output.h"
#include "lwinput.h"
#include "video_output.h"

#include "../common/libavsmash.h"
#include "../common/libavsmash_audio.h"
#include "../common/libavsmash_video.h"

typedef struct {
    uint32_t media_timescale;
    uint64_t skip_duration;
    int64_t start_pts;
} libavsmash_video_info_handler_t;

typedef struct {
    uint32_t media_timescale;
    int64_t start_pts;
} libavsmash_audio_info_handler_t;

typedef struct libavsmash_handler_tag {
    /* Global stuff */
    UINT uType;
    lsmash_root_t* root;
    lsmash_file_parameters_t file_param;
    lsmash_movie_parameters_t movie_param;
    uint32_t number_of_tracks;
    AVFormatContext* format_ctx;
    int threads;
    /* Video stuff */
    libavsmash_video_info_handler_t vih;
    libavsmash_video_decode_handler_t* vdhp;
    libavsmash_video_output_handler_t* vohp;
    /* Audio stuff */
    libavsmash_audio_info_handler_t aih;
    libavsmash_audio_decode_handler_t* adhp;
    libavsmash_audio_output_handler_t* aohp;
    int64_t av_gap;
    int av_sync;
} libavsmash_handler_t;

/* Deallocate the handler of this plugin. */
static void free_handler(libavsmash_handler_t** hpp)
{
    if (!hpp || !*hpp)
        return;
    libavsmash_handler_t* hp = *hpp;
    libavsmash_video_free_decode_handler(hp->vdhp);
    libavsmash_video_free_output_handler(hp->vohp);
    libavsmash_audio_free_decode_handler(hp->adhp);
    libavsmash_audio_free_output_handler(hp->aohp);
    lw_freep(hpp);
}

/* Allocate the handler of this plugin. */
static libavsmash_handler_t* alloc_handler(void)
{
    libavsmash_handler_t* hp = lw_malloc_zero(sizeof(libavsmash_handler_t));
    if (!hp)
        return NULL;
    if (!(hp->vdhp = libavsmash_video_alloc_decode_handler()) || !(hp->vohp = libavsmash_video_alloc_output_handler())
        || !(hp->adhp = libavsmash_audio_alloc_decode_handler()) || !(hp->aohp = libavsmash_audio_alloc_output_handler())) {
        free_handler(&hp);
        return NULL;
    }
    return hp;
}

static int get_first_track_of_type(lsmash_handler_t* h, uint32_t type)
{
    int ret;
    lw_log_handler_t* lhp;
    if (type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK) {
        libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
        lhp = libavsmash_video_get_log_handler(hp->vdhp);
        libavsmash_video_set_root(hp->vdhp, hp->root);
        ret = libavsmash_video_get_track(hp->vdhp, 0);
    } else {
        libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
        lhp = libavsmash_audio_get_log_handler(hp->adhp);
        libavsmash_audio_set_root(hp->adhp, hp->root);
        ret = libavsmash_audio_get_track(hp->adhp, 0);
    }
    lhp->level = LW_LOG_WARNING;
    return ret;
}

static int get_ctd_shift(lsmash_root_t* root, uint32_t track_id, uint32_t* ctd_shift)
{
    if (lsmash_get_composition_to_decode_shift_from_media_timeline(root, track_id, ctd_shift)) {
        DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get the timeline shift.");
        return -1;
    }
    return 0;
}

static uint64_t get_empty_duration(lsmash_root_t* root, uint32_t track_id, uint32_t movie_timescale, uint32_t media_timescale)
{
    /* Consider empty duration if the first edit is an empty edit. */
    lsmash_edit_t edit;
    if (lsmash_get_explicit_timeline_map(root, track_id, 1, &edit))
        return 0;
    if (edit.duration && edit.start_time == ISOM_EDIT_MODE_EMPTY)
        return av_rescale_q(edit.duration, (AVRational) { 1, movie_timescale }, (AVRational) { 1, media_timescale });
    return 0;
}

static int64_t get_start_time(lsmash_root_t* root, uint32_t track_id)
{
    /* Consider start time of this media if any non-empty edit is present. */
    uint32_t edit_count = lsmash_count_explicit_timeline_map(root, track_id);
    for (uint32_t edit_number = 1; edit_number <= edit_count; edit_number++) {
        lsmash_edit_t edit;
        if (lsmash_get_explicit_timeline_map(root, track_id, edit_number, &edit))
            return 0;
        if (edit.duration == 0)
            return 0; /* no edits */
        if (edit.start_time >= 0)
            return edit.start_time;
    }
    return 0;
}

static int prepare_video_decoding(lsmash_handler_t* h, video_option_t* opt)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    /* Initialize the video decoder configuration. */
    if (libavsmash_video_initialize_decoder_configuration(vdhp, hp->format_ctx, hp->threads) < 0) {
        DEBUG_VIDEO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to initialize the decoder configuration.");
        return -1;
    }
    AVCodecContext* ctx = libavsmash_video_get_codec_context(vdhp);
    if (!ctx)
        return 0;
    /* Set up video rendering. */
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    int max_width = libavsmash_video_get_max_width(vdhp);
    int max_height = libavsmash_video_get_max_height(vdhp);
    if (au_setup_video_rendering(vohp, opt, &h->video_format, max_width, max_height, ctx->pix_fmt) < 0)
        return -1;
    /* Set up timestamp info. */
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    libavsmash_video_setup_timestamp_info(vdhp, vohp, &fps_num, &fps_den);
    if (libavsmash_video_get_error(vdhp)) {
        DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get the minimum CTS of video stream.");
        return -1;
    }
    /* Create keyframe list.
     * This requires frame output order, therefore, shall be called after libavsmash_video_setup_timestamp_info(). */
    if (libavsmash_video_create_keyframe_list(vdhp) < 0) {
        DEBUG_VIDEO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to create keyframe list.");
        return -1;
    }
#ifndef DEBUG_VIDEO
    lw_log_handler_t* lhp = libavsmash_video_get_log_handler(vdhp);
    lhp->level = LW_LOG_FATAL;
#endif
    /* Find the first valid video frame. */
    if (libavsmash_video_find_first_valid_frame(vdhp) < 0)
        return -1;
    /* Setup the reader specific info. */
    uint32_t media_timescale = libavsmash_video_get_media_timescale(vdhp);
    if (hp->av_sync) {
        uint32_t track_id = libavsmash_video_get_track_id(vdhp);
        uint32_t ctd_shift;
        if (get_ctd_shift(hp->root, track_id, &ctd_shift) < 0)
            return -1;
        uint64_t min_cts = libavsmash_video_get_min_cts(vdhp);
        uint32_t movie_timescale = hp->movie_param.timescale;
        hp->vih.start_pts = min_cts + ctd_shift + get_empty_duration(hp->root, track_id, movie_timescale, media_timescale);
        hp->vih.skip_duration = ctd_shift + get_start_time(hp->root, track_id);
    }
    hp->vih.media_timescale = media_timescale;
    h->framerate_num = (int)fps_num;
    h->framerate_den = (int)fps_den;
    h->video_sample_count = vohp->frame_count;
    /* Force seeking at the first reading. */
    libavsmash_video_force_seek(vdhp);
    return 0;
}

static int prepare_audio_decoding(lsmash_handler_t* h, audio_option_t* opt)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
    libavsmash_audio_decode_handler_t* adhp = hp->adhp;
    /* Initialize the audio decoder configuration. */
    if (libavsmash_audio_initialize_decoder_configuration(adhp, hp->format_ctx, hp->threads) < 0) {
        DEBUG_VIDEO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to initialize the decoder configuration.");
        return -1;
    }
    AVCodecContext* ctx = libavsmash_audio_get_codec_context(adhp);
    if (!ctx)
        return 0;
    libavsmash_audio_output_handler_t* aohp = hp->aohp;
    aohp->output_channel_layout = libavsmash_audio_get_best_used_channel_layout(adhp);
    aohp->output_sample_format = libavsmash_audio_get_best_used_sample_format(adhp);
    aohp->output_sample_rate = libavsmash_audio_get_best_used_sample_rate(adhp);
    aohp->output_bits_per_sample = libavsmash_audio_get_best_used_bits_per_sample(adhp);
    /* */
#ifndef DEBUG_AUDIO
    lw_log_handler_t* lhp = libavsmash_audio_get_log_handler(adhp);
    lhp->level = LW_LOG_FATAL;
#endif
    if (au_setup_audio_rendering(aohp, ctx, opt, &h->audio_format.Format) < 0)
        return -1;
    /* Setup the reader specific info.
     * Note that this settings affects with the number of output PCM samples, therefore do before its counting and A/V sync settings. */
    uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
    uint64_t start_time = 0;
    if (hp->av_sync) {
        uint64_t min_cts = libavsmash_audio_get_min_cts(adhp);
        if (min_cts == UINT64_MAX) {
            DEBUG_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "Failed to get the minimum CTS of audio stream.");
            return -1;
        }
        uint32_t track_id = libavsmash_audio_get_track_id(adhp);
        uint32_t ctd_shift;
        if (get_ctd_shift(hp->root, track_id, &ctd_shift) < 0)
            return -1;
        uint32_t movie_timescale = hp->movie_param.timescale;
        hp->aih.start_pts = min_cts + ctd_shift + get_empty_duration(hp->root, track_id, movie_timescale, media_timescale);
        start_time = ctd_shift + get_start_time(hp->root, track_id);
    }
    hp->aih.media_timescale = media_timescale;
    /* Count the number of PCM audio samples.
     * TODO: Give start time before calling libavsmash_audio_count_overall_pcm_samples() and remove the 3rd argument. */
    h->audio_pcm_sample_count = libavsmash_audio_count_overall_pcm_samples(adhp, aohp->output_sample_rate, start_time);
    if (h->audio_pcm_sample_count == 0) {
        DEBUG_AUDIO_MESSAGE_BOX_DESKTOP(MB_ICONERROR | MB_OK, "No valid audio frame.");
        return -1;
    }
    if (hp->av_sync && libavsmash_video_get_track_id(hp->vdhp)) {
        AVRational audio_sample_base = (AVRational) { 1, aohp->output_sample_rate };
        hp->av_gap = av_rescale_q(hp->aih.start_pts, (AVRational) { 1, hp->aih.media_timescale }, audio_sample_base)
            - av_rescale_q(hp->vih.start_pts - hp->vih.skip_duration, (AVRational) { 1, hp->vih.media_timescale }, audio_sample_base);
        h->audio_pcm_sample_count += hp->av_gap;
        uint32_t media_timescale = libavsmash_audio_get_media_timescale(adhp);
        hp->aohp->skip_decoded_samples = av_rescale(start_time, aohp->output_sample_rate, media_timescale);
        libavsmash_audio_apply_delay(adhp, hp->av_gap);
    }
    /* Force seeking at the first reading. */
    libavsmash_audio_force_seek(adhp);
    return 0;
}

static void* open_file(char* file_name, reader_option_t* opt)
{
    libavsmash_handler_t* hp = alloc_handler();
    if (!hp)
        return NULL;
    /* Set up the log handlers. */
    hp->uType = MB_ICONERROR | MB_OK;
    lw_log_handler_t* vlhp = libavsmash_video_get_log_handler(hp->vdhp);
    lw_log_handler_t* alhp = libavsmash_audio_get_log_handler(hp->adhp);
    vlhp->priv = &hp->uType;
    vlhp->level = LW_LOG_QUIET;
    vlhp->show_log = au_message_box_desktop;
    *alhp = *vlhp;
    /* Open file. */
    hp->root = libavsmash_open_file(&hp->format_ctx, file_name, &hp->file_param, &hp->movie_param, vlhp);
    if (!hp->root) {
        free_handler(&hp);
        return NULL;
    }
    hp->number_of_tracks = hp->movie_param.number_of_tracks;
    hp->threads = opt->threads;
    hp->av_sync = opt->av_sync;
    libavsmash_video_set_preferred_decoder_names(hp->vdhp, opt->preferred_decoder_names);
    libavsmash_audio_set_preferred_decoder_names(hp->adhp, opt->preferred_decoder_names);
    *alhp = *vlhp;
    return hp;
}

static int get_first_video_track(lsmash_handler_t* h, video_option_t* opt)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
    if (get_first_track_of_type(h, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK) != 0) {
        uint32_t track_id = libavsmash_video_get_track_id(hp->vdhp);
        lsmash_destruct_timeline(hp->root, track_id);
        libavsmash_video_close_codec_context(hp->vdhp);
        return -1;
    }
    /* Set video options. */
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    libavsmash_video_set_seek_mode(vdhp, opt->seek_mode);
    libavsmash_video_set_forward_seek_threshold(vdhp, opt->forward_seek_threshold);
    vohp->vfr2cfr = opt->vfr2cfr.active;
    vohp->cfr_num = opt->vfr2cfr.framerate_num;
    vohp->cfr_den = opt->vfr2cfr.framerate_den;
    /* TODO: Maybe, the number of output frames should be set up here. */
    return prepare_video_decoding(h, opt);
}

static int get_first_audio_track(lsmash_handler_t* h, audio_option_t* opt)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
    if (get_first_track_of_type(h, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK) != 0) {
        uint32_t track_id = libavsmash_audio_get_track_id(hp->adhp);
        lsmash_destruct_timeline(hp->root, track_id);
        libavsmash_audio_close_codec_context(hp->adhp);
        return -1;
    }
    return prepare_audio_decoding(h, opt);
}

static void destroy_disposable(void* private_stuff)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)private_stuff;
    lsmash_discard_boxes(hp->root);
}

static int read_video(lsmash_handler_t* h, int sample_number, void* buf)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    if (libavsmash_video_get_error(vdhp))
        return 0;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    ++sample_number; /* For L-SMASH, sample_number is 1-origin. */
    if (sample_number == 1) {
        au_video_output_handler_t* au_vohp = (au_video_output_handler_t*)vohp->private_handler;
        memcpy(buf, au_vohp->back_ground, au_vohp->output_frame_size);
    }
    int ret = libavsmash_video_get_frame(vdhp, vohp, sample_number);
    if (ret != 0 && !(ret == 1 && sample_number == 1))
        /* Skip writing frame data into AviUtl's frame buffer.
         * Apparently, AviUtl clears the frame buffer at the first frame.
         * Therefore, don't skip in that case. */
        return 0;
    AVFrame* av_frame = libavsmash_video_get_frame_buffer(vdhp);
    return convert_colorspace(vohp, av_frame, buf);
}

static int read_audio(lsmash_handler_t* h, int start, int wanted_length, void* buf)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
    return libavsmash_audio_get_pcm_samples(hp->adhp, hp->aohp, buf, start, wanted_length);
}

static int is_keyframe(lsmash_handler_t* h, int sample_number)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
    return libavsmash_video_is_keyframe(hp->vdhp, hp->vohp, sample_number + 1);
}

static int delay_audio(lsmash_handler_t* h, int* start, int wanted_length, int audio_delay)
{
    /* Even if start become negative, its absolute value shall be equal to wanted_length or smaller. */
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
    int end = *start + wanted_length;
    audio_delay += hp->av_gap;
    if (*start < audio_delay && end <= audio_delay) {
        libavsmash_audio_force_seek(hp->adhp); /* Force seeking at the next access for valid audio frame. */
        return 0;
    }
    *start -= audio_delay;
    return 1;
}

static void video_cleanup(lsmash_handler_t* h)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->video_private;
    if (!hp)
        return;
    /* Free and then set nullptr since other functions might reference the pointer later. */
    libavsmash_video_free_decode_handler_ptr(&hp->vdhp);
    libavsmash_video_free_output_handler_ptr(&hp->vohp);
}

static void audio_cleanup(lsmash_handler_t* h)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)h->audio_private;
    if (!hp)
        return;
    /* Free and then set nullptr since other functions might reference the pointer later. */
    libavsmash_audio_free_decode_handler_ptr(&hp->adhp);
    libavsmash_audio_free_output_handler_ptr(&hp->aohp);
}

static void close_file(void* private_stuff)
{
    libavsmash_handler_t* hp = (libavsmash_handler_t*)private_stuff;
    if (!hp)
        return;
    avformat_close_input(&hp->format_ctx);
    lsmash_close_file(&hp->file_param);
    lsmash_destroy_root(hp->root);
    lw_free(hp);
}

lsmash_reader_t libavsmash_reader = { LIBAVSMASH_READER, open_file, get_first_video_track, get_first_audio_track, destroy_disposable,
    read_video, read_audio, is_keyframe, delay_audio, video_cleanup, audio_cleanup, close_file };
