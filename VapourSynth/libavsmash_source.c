/*****************************************************************************
 * libavsmash_source.c
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

/* This file is available under an ISC license.
 * However, when distributing its binary file, it will be under LGPL or GPL. */

/* L-SMASH (ISC) */
#include <lsmash.h> /* Demuxer */

/* Libav (LGPL or GPL) */
#include <libavcodec/avcodec.h> /* Decoder */
#include <libavformat/avformat.h> /* Codec specific info importer */
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h> /* Colorspace converter */

#include "lsmashsource.h"
#include "video_output.h"

#include "../common/libavsmash.h"
#include "../common/libavsmash_video.h"

typedef struct {
    VSVideoInfo vi[2];
    libavsmash_video_decode_handler_t* vdhp;
    libavsmash_video_output_handler_t* vohp;
    lsmash_file_parameters_t file_param;
    AVFormatContext* format_ctx;
    char preferred_decoder_names_buf[PREFERRED_DECODER_NAMES_BUFSIZE];
    int prefer_hw;
} lsmas_handler_t;

/* Deallocate the handler of this plugin. */
static void free_handler(lsmas_handler_t** hpp)
{
    if (!hpp || !*hpp)
        return;
    lsmas_handler_t* hp = *hpp;
    lsmash_root_t* root = libavsmash_video_get_root(hp->vdhp);
    lw_free(libavsmash_video_get_preferred_decoder_names(hp->vdhp));
    libavsmash_video_free_decode_handler(hp->vdhp);
    libavsmash_video_free_output_handler(hp->vohp);
    avformat_close_input(&hp->format_ctx);
    lsmash_close_file(&hp->file_param);
    lsmash_destroy_root(root);
    lw_free(hp);
}

/* Allocate the handler of this plugin. */
static lsmas_handler_t* alloc_handler(void)
{
    lsmas_handler_t* hp = (lsmas_handler_t*)lw_malloc_zero(sizeof(lsmas_handler_t));
    if (!hp)
        return NULL;
    hp->vdhp = libavsmash_video_alloc_decode_handler();
    if (!hp->vdhp) {
        free_handler(&hp);
        return NULL;
    }
    hp->vohp = libavsmash_video_alloc_output_handler();
    if (!hp->vohp) {
        free_handler(&hp);
        return NULL;
    }
    return hp;
}

static void VS_CC vs_filter_init(VSMap* in, VSMap* out, void** instance_data, VSNode* node, VSCore* core, const VSAPI* vsapi)
{
    lsmas_handler_t* hp = (lsmas_handler_t*)*instance_data;
    AVCodecContext* ctx = libavsmash_video_get_codec_context(hp->vdhp);
    vsapi->setVideoInfo(hp->vi, (av_pix_fmt_desc_get(ctx->pix_fmt)->flags & AV_PIX_FMT_FLAG_ALPHA) ? 2 : 1, node);
}

static int get_composition_duration(
    libavsmash_video_decode_handler_t* vdhp, uint32_t composition_sample_number, uint32_t last_sample_number)
{
    uint32_t coded_sample_number = libavsmash_video_get_coded_sample_number(vdhp, composition_sample_number);
    if (composition_sample_number == last_sample_number)
        goto no_composition_duration;
    uint32_t next_coded_sample_number = libavsmash_video_get_coded_sample_number(vdhp, composition_sample_number + 1);
    uint64_t cts;
    uint64_t next_cts;
    if (libavsmash_video_get_cts(vdhp, coded_sample_number, &cts) < 0
        || libavsmash_video_get_cts(vdhp, next_coded_sample_number, &next_cts) < 0)
        goto no_composition_duration;
    if (next_cts <= cts || (next_cts - cts) > INT_MAX)
        return 0;
    return (int)(next_cts - cts);
no_composition_duration:;
    uint32_t sample_duration;
    if (libavsmash_video_get_sample_duration(vdhp, coded_sample_number, &sample_duration) < 0)
        return 0;
    return sample_duration <= INT_MAX ? sample_duration : 0;
}

static void get_sample_duration(
    libavsmash_video_decode_handler_t* vdhp, VSVideoInfo* vi, uint32_t sample_number, int64_t* duration_num, int64_t* duration_den)
{
    int sample_duration = get_composition_duration(vdhp, sample_number, vi->numFrames);
    if (sample_duration == 0) {
        *duration_num = vi->fpsDen;
        *duration_den = vi->fpsNum;
    } else {
        uint32_t media_timescale = libavsmash_video_get_media_timescale(vdhp);
        *duration_num = sample_duration;
        *duration_den = media_timescale;
    }
}

static void set_frame_properties(libavsmash_video_decode_handler_t* vdhp, VSVideoInfo* vi, AVFrame* av_frame, VSFrameRef* vs_frame,
    uint32_t sample_number, int top, int bottom, const VSAPI* vsapi, int n)
{
    int64_t duration_num;
    int64_t duration_den;
    get_sample_duration(vdhp, vi, sample_number, &duration_num, &duration_den);
    vs_set_frame_properties(av_frame, NULL, duration_num, duration_den, vs_frame, top, bottom, vsapi, n);
}

static int prepare_video_decoding(lsmas_handler_t* hp, int threads, VSMap* out, VSCore* core, const VSAPI* vsapi)
{
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    VSVideoInfo* vi = &hp->vi[0];
    /* Initialize the video decoder configuration. */
    if (libavsmash_video_initialize_decoder_configuration(vdhp, hp->format_ctx, threads) < 0) {
        set_error_on_init(out, vsapi, "lsmas: failed to initialize the decoder configuration.");
        return -1;
    }
    /* Set up output format. */
    AVCodecContext* ctx = libavsmash_video_get_codec_context(vdhp);
    vs_video_output_handler_t* vs_vohp = (vs_video_output_handler_t*)vohp->private_handler;
    vs_vohp->frame_ctx = NULL;
    vs_vohp->core = core;
    vs_vohp->vsapi = vsapi;
    int max_width = libavsmash_video_get_max_width(vdhp);
    int max_height = libavsmash_video_get_max_height(vdhp);
    if (vs_setup_video_rendering(vohp, ctx, vi, out, max_width, max_height) < 0)
        return -1;
    libavsmash_video_set_get_buffer_func(vdhp);
    /* Calculate average framerate. */
    int64_t fps_num = 25;
    int64_t fps_den = 1;
    libavsmash_video_setup_timestamp_info(vdhp, vohp, &fps_num, &fps_den);
    if (vohp->vfr2cfr) {
        if (libavsmash_video_get_error(vdhp)) {
            set_error_on_init(out, vsapi, "lsmas: failed to get the minimum CTS of video stream.");
            return -1;
        }
    } else
        libavsmash_video_clear_error(vdhp);
    /* Find the first valid video sample. */
    if (libavsmash_video_find_first_valid_frame(vdhp) < 0) {
        set_error_on_init(out, vsapi, "lsmas: failed to allocate the first valid video frame.");
        return -1;
    }
    /* Setup filter specific info. */
    hp->vi[0].fpsNum = fps_num;
    hp->vi[0].fpsDen = fps_den;
    hp->vi[0].numFrames = vohp->frame_count;
    if ((av_pix_fmt_desc_get(ctx->pix_fmt)->flags & AV_PIX_FMT_FLAG_ALPHA) && hp->vi[0].format) {
        hp->vi[1] = hp->vi[0];
        hp->vi[1].format = vsapi->registerFormat(cmGray, hp->vi[0].format->sampleType, hp->vi[0].format->bitsPerSample, 0, 0, core);
        vs_vohp->background_frame[1] = vsapi->newVideoFrame(hp->vi[1].format, hp->vi[1].width, hp->vi[1].height, NULL, core);
        if (!vs_vohp->background_frame[1]) {
            set_error_on_init(out, vsapi, "lsmas: failed to allocate memory for the alpha frame data.");
            return -1;
        }
    }
    /* Force seeking at the first reading. */
    libavsmash_video_force_seek(vdhp);
    return 0;
}

static const VSFrameRef* VS_CC vs_filter_get_frame(
    int n, int activation_reason, void** instance_data, void** frame_data, VSFrameContext* frame_ctx, VSCore* core, const VSAPI* vsapi)
{
    if (activation_reason != arInitial)
        return NULL;
    lsmas_handler_t* hp = (lsmas_handler_t*)*instance_data;
    VSVideoInfo* vi = &hp->vi[0];
    uint32_t sample_number = MIN(n + 1, vi->numFrames); /* For L-SMASH, sample_number is 1-origin. */
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    if (libavsmash_video_get_error(vdhp)) {
        vsapi->setFilterError("lsmas: failed to output a video frame.", frame_ctx);
        return NULL;
    }
    /* Set up VapourSynth error handler. */
    vs_basic_handler_t vsbh = { 0 };
    vsbh.out = NULL;
    vsbh.frame_ctx = frame_ctx;
    vsbh.vsapi = vsapi;
    lw_log_handler_t* lhp = libavsmash_video_get_log_handler(vdhp);
    lhp->priv = &vsbh;
    lhp->show_log = set_error;
    /* Get and decode the desired video frame. */
    vs_video_output_handler_t* vs_vohp = (vs_video_output_handler_t*)vohp->private_handler;
    vs_vohp->frame_ctx = frame_ctx;
    vs_vohp->core = core;
    vs_vohp->vsapi = vsapi;
    if (libavsmash_video_get_frame(vdhp, vohp, sample_number) < 0) {
        vsapi->setFilterError("lsmas: failed to output a video frame.", frame_ctx);
        return NULL;
    }
    /* Output video frame. */
    AVFrame* av_frame = libavsmash_video_get_frame_buffer(vdhp);
    int output_index = vsapi->getOutputIndex(frame_ctx);
    VSFrameRef* vs_frame = make_frame(vohp, av_frame, output_index);
    if (!vs_frame) {
        vsapi->setFilterError("lsmas: failed to output a video frame.", frame_ctx);
        return NULL;
    }
    AVCodecContext* ctx = libavsmash_video_get_codec_context(vdhp);
    if (output_index == 0 && (av_pix_fmt_desc_get(ctx->pix_fmt)->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        /* api4 compat: save alpha clip into the _Alpha property */
        VSFrameRef* vs_frame2 = make_frame(vohp, av_frame, 1);
        if (!vs_frame2) {
            vsapi->setFilterError("lsmas: failed to output an alpha video frame.", frame_ctx);
            return NULL;
        }
        VSMap* props = vsapi->getFramePropsRW(vs_frame2);
        vsapi->propSetInt(props, "_ColorRange", 0, paReplace); // alpha clip always full range
        props = vsapi->getFramePropsRW(vs_frame);
        vsapi->propSetFrame(props, "_Alpha", vs_frame2, paAppend);
        vsapi->freeFrame(vs_frame2);
    }
    int top = -1;
    if (vohp->repeat_control && vohp->repeat_requested) {
        top = (vohp->frame_order_list[n].top == vohp->frame_order_list[sample_number].top) ? vohp->frame_order_list[n - 1].top
                                                                                           : vohp->frame_order_list[n].top;
    }
    int bottom = -1;
    if (vohp->repeat_control && vohp->repeat_requested) {
        bottom = (vohp->frame_order_list[n].bottom == vohp->frame_order_list[sample_number].bottom) ? vohp->frame_order_list[n - 1].bottom
                                                                                                    : vohp->frame_order_list[n].bottom;
    }
    set_frame_properties(vdhp, vi, av_frame, vs_frame, sample_number, top, bottom, vsapi, n);
    return vs_frame;
}

static void VS_CC vs_filter_free(void* instance_data, VSCore* core, const VSAPI* vsapi)
{
    free_handler((lsmas_handler_t**)&instance_data);
}

static uint32_t open_file(lsmas_handler_t* hp, const char* source, lw_log_handler_t* lhp)
{
    lsmash_movie_parameters_t movie_param;
    lsmash_root_t* root = libavsmash_open_file(&hp->format_ctx, source, &hp->file_param, &movie_param, lhp);
    if (!root)
        return 0;
    libavsmash_video_set_root(hp->vdhp, root);
    return movie_param.number_of_tracks;
}

void VS_CC vs_libavsmashsource_create(const VSMap* in, VSMap* out, void* user_data, VSCore* core, const VSAPI* vsapi)
{
    const char* file_name = vsapi->propGetData(in, "source", 0, NULL);
    /* Allocate the handler of this plugin. */
    lsmas_handler_t* hp = alloc_handler();
    if (!hp) {
        vsapi->setError(out, "lsmas: failed to allocate the handler.");
        return;
    }
    libavsmash_video_decode_handler_t* vdhp = hp->vdhp;
    libavsmash_video_output_handler_t* vohp = hp->vohp;
    vs_video_output_handler_t* vs_vohp = vs_allocate_video_output_handler(vohp);
    if (!vs_vohp) {
        free_handler(&hp);
        vsapi->setError(out, "lsmas: failed to allocate the VapourSynth video output handler.");
        return;
    }
    /* Set up VapourSynth error handler. */
    vs_basic_handler_t vsbh = { 0 };
    vsbh.out = out;
    vsbh.frame_ctx = NULL;
    vsbh.vsapi = vsapi;
    /* Set up log handler. */
    lw_log_handler_t lh = { 0 };
    lh.level = LW_LOG_FATAL;
    lh.priv = &vsbh;
    lh.show_log = set_error;
    /* Open source file. */
    uint32_t number_of_tracks = open_file(hp, file_name, &lh);
    if (number_of_tracks == 0) {
        free_handler(&hp);
        vsapi->setError(out, "lsmas: failed to open file.");
        return;
    }
    /* Get options. */
    int64_t track_number;
    int64_t threads;
    int64_t seek_mode;
    int64_t seek_threshold;
    int64_t variable_info;
    int64_t direct_rendering;
    int64_t fps_num;
    int64_t fps_den;
    int64_t prefer_hw_decoder;
    int64_t ff_loglevel;
    const char* format;
    const char* preferred_decoder_names;
    const char* ff_options;
    set_option_int64(&track_number, 0, "track", in, vsapi);
    set_option_int64(&threads, 0, "threads", in, vsapi);
    set_option_int64(&seek_mode, 0, "seek_mode", in, vsapi);
    set_option_int64(&seek_threshold, 10, "seek_threshold", in, vsapi);
    set_option_int64(&variable_info, 0, "variable", in, vsapi);
    set_option_int64(&direct_rendering, 0, "dr", in, vsapi);
    set_option_int64(&fps_num, 0, "fpsnum", in, vsapi);
    set_option_int64(&fps_den, 1, "fpsden", in, vsapi);
    set_option_int64(&prefer_hw_decoder, 0, "prefer_hw", in, vsapi);
    set_option_int64(&ff_loglevel, 0, "ff_loglevel", in, vsapi);
    set_option_string(&format, NULL, "format", in, vsapi);
    set_option_string(&preferred_decoder_names, NULL, "decoder", in, vsapi);
    set_option_string(&ff_options, NULL, "ff_options", in, vsapi);
    set_preferred_decoder_names_on_buf(hp->preferred_decoder_names_buf, preferred_decoder_names);
    libavsmash_video_set_seek_mode(vdhp, CLIP_VALUE(seek_mode, 0, 2));
    libavsmash_video_set_forward_seek_threshold(vdhp, CLIP_VALUE(seek_threshold, 1, 999));
    libavsmash_video_set_preferred_decoder_names(vdhp, tokenize_preferred_decoder_names(hp->preferred_decoder_names_buf));
    set_prefer_hw(&hp->prefer_hw, CLIP_VALUE(prefer_hw_decoder, 0, 7));
    libavsmash_video_set_prefer_hw_decoder(vdhp, &hp->prefer_hw);
    libavsmash_video_set_decoder_options(vdhp, ff_options);
    vohp->vfr2cfr = (fps_num > 0 && fps_den > 0);
    vohp->cfr_num = (uint32_t)fps_num;
    vohp->cfr_den = (uint32_t)fps_den;
    vs_vohp->variable_info = CLIP_VALUE(variable_info, 0, 1);
    vs_vohp->direct_rendering = CLIP_VALUE(direct_rendering, 0, 1) && !format;
    vs_vohp->vs_output_pixel_format = vs_vohp->variable_info ? pfNone : get_vs_output_pixel_format(format);
    if (ff_loglevel <= 0)
        av_log_set_level(AV_LOG_QUIET);
    else if (ff_loglevel == 1)
        av_log_set_level(AV_LOG_PANIC);
    else if (ff_loglevel == 2)
        av_log_set_level(AV_LOG_FATAL);
    else if (ff_loglevel == 3)
        av_log_set_level(AV_LOG_ERROR);
    else if (ff_loglevel == 4)
        av_log_set_level(AV_LOG_WARNING);
    else if (ff_loglevel == 5)
        av_log_set_level(AV_LOG_INFO);
    else if (ff_loglevel == 6)
        av_log_set_level(AV_LOG_VERBOSE);
    else if (ff_loglevel == 7)
        av_log_set_level(AV_LOG_DEBUG);
    else
        av_log_set_level(AV_LOG_TRACE);
    if (track_number && track_number > number_of_tracks) {
        free_handler(&hp);
        set_error_on_init(out, vsapi, "lsmas: the number of tracks equals %" PRIu32 ".", number_of_tracks);
        return;
    }
    libavsmash_video_set_log_handler(vdhp, &lh);
    /* Get video track. */
    if (libavsmash_video_get_track(vdhp, track_number) < 0) {
        free_handler(&hp);
        vsapi->setError(out, "lsmas: failed to get video track.");
        return;
    }
    /* Set up decoders for this track. */
    threads = threads >= 0 ? threads : 0;
    if (prepare_video_decoding(hp, threads, out, core, vsapi) < 0) {
        free_handler(&hp);
        return;
    }
    lsmash_discard_boxes(libavsmash_video_get_root(vdhp));
    AVFrame* av_frame = libavsmash_video_get_frame_buffer(vdhp);
    if (!av_frame->data[0] && hp->prefer_hw) {
        free_handler(&hp);
        vsapi->setError(out, "lsmas: the GPU driver doesn't support this hardware decoding.");
        return;
    }
    vsapi->createFilter(
        in, out, "LibavSMASHSource", vs_filter_init, vs_filter_get_frame, vs_filter_free, fmUnordered, nfMakeLinear, hp, core);
}
