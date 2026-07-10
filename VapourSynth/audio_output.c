/*****************************************************************************
 * audio_output.c
 *****************************************************************************/

#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <string.h>

#include "audio_output.h"

static enum AVSampleFormat decide_audio_output_sample_format(enum AVSampleFormat input_sample_format)
{
    switch (input_sample_format) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return AV_SAMPLE_FMT_U8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return AV_SAMPLE_FMT_S16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return AV_SAMPLE_FMT_S32;
    default:
        return AV_SAMPLE_FMT_FLT;
    }
}

int vs_setup_audio_rendering(lw_audio_output_handler_t* aohp, AVCodecContext* ctx, const char* channel_layout, int sample_rate,
    VSAudioInfo* ai, VSCore* core, const VSAPI* vsapi)
{
    if (!aohp || !ctx || !ai || !core || !vsapi)
        return -1;
    if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&ctx->ch_layout, ctx->ch_layout.nb_channels);
    if (channel_layout) {
        av_channel_layout_uninit(&aohp->output_channel_layout);
        if (av_channel_layout_from_string(&aohp->output_channel_layout, channel_layout) < 0)
            return -1;
    } else if (aohp->output_channel_layout.order == AV_CHANNEL_ORDER_UNSPEC || aohp->output_channel_layout.nb_channels == 0) {
        if (av_channel_layout_copy(&aohp->output_channel_layout, &ctx->ch_layout) < 0)
            return -1;
    }
    if (aohp->output_channel_layout.order != AV_CHANNEL_ORDER_NATIVE || !aohp->output_channel_layout.u.mask)
        return -1;
    if (sample_rate > 0)
        aohp->output_sample_rate = sample_rate;
    if (aohp->output_sample_rate <= 0)
        aohp->output_sample_rate = ctx->sample_rate;

    aohp->output_sample_format = decide_audio_output_sample_format(aohp->output_sample_format);
    aohp->s24_output = 0;
    aohp->output_bits_per_sample = av_get_bytes_per_sample(aohp->output_sample_format) * 8;
    if (aohp->output_bits_per_sample <= 0)
        return -1;

    int input_channels = ctx->ch_layout.nb_channels;
    if (av_sample_fmt_is_planar(ctx->sample_fmt)) {
        aohp->input_planes = input_channels;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt);
    } else {
        aohp->input_planes = 1;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt) * input_channels;
    }
    aohp->output_block_align = aohp->output_channel_layout.nb_channels * av_get_bytes_per_sample(aohp->output_sample_format);
    av_channel_layout_uninit(&aohp->input_channel_layout);
    if (av_channel_layout_copy(&aohp->input_channel_layout, &ctx->ch_layout) < 0)
        return -1;
    aohp->input_sample_format = ctx->sample_fmt;
    aohp->input_sample_rate = ctx->sample_rate;

    swr_free(&aohp->swr_ctx);
    aohp->swr_ctx = swr_alloc();
    if (!aohp->swr_ctx)
        return -1;
    av_opt_set_chlayout(aohp->swr_ctx, "in_chlayout", &ctx->ch_layout, 0);
    av_opt_set_sample_fmt(aohp->swr_ctx, "in_sample_fmt", ctx->sample_fmt, 0);
    av_opt_set_int(aohp->swr_ctx, "in_sample_rate", ctx->sample_rate, 0);
    av_opt_set_chlayout(aohp->swr_ctx, "out_chlayout", &aohp->output_channel_layout, 0);
    av_opt_set_sample_fmt(aohp->swr_ctx, "out_sample_fmt", aohp->output_sample_format, 0);
    av_opt_set_int(aohp->swr_ctx, "out_sample_rate", aohp->output_sample_rate, 0);
    av_opt_set_sample_fmt(aohp->swr_ctx, "internal_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
    if (swr_init(aohp->swr_ctx) < 0)
        return -1;

    memset(ai, 0, sizeof(*ai));
    int sample_type = aohp->output_sample_format == AV_SAMPLE_FMT_FLT ? stFloat : stInteger;
    if (!vsapi->queryAudioFormat(
            &ai->format, sample_type, aohp->output_bits_per_sample, aohp->output_channel_layout.u.mask, core))
        return -1;
    ai->sampleRate = aohp->output_sample_rate;
    return 0;
}

VSFrame* vs_interleaved_audio_frame(
    const lw_audio_output_handler_t* aohp, const uint8_t* interleaved, int sample_count, VSCore* core, const VSAPI* vsapi)
{
    if (!aohp || !interleaved || sample_count <= 0 || !core || !vsapi)
        return NULL;
    const int channels = aohp->output_channel_layout.nb_channels;
    const int bytes_per_sample = av_get_bytes_per_sample(aohp->output_sample_format);
    if (channels <= 0 || bytes_per_sample <= 0)
        return NULL;

    VSAudioFormat format;
    int sample_type = aohp->output_sample_format == AV_SAMPLE_FMT_FLT ? stFloat : stInteger;
    if (!vsapi->queryAudioFormat(
            &format, sample_type, bytes_per_sample * 8, aohp->output_channel_layout.u.mask, core))
        return NULL;
    VSFrame* frame = vsapi->newAudioFrame(&format, sample_count, NULL, core);
    if (!frame)
        return NULL;
    for (int channel = 0; channel < channels; channel++) {
        uint8_t* dst = vsapi->getWritePtr(frame, channel);
        for (int sample = 0; sample < sample_count; sample++)
            memcpy(dst + (size_t)sample * bytes_per_sample,
                interleaved + ((size_t)sample * channels + channel) * bytes_per_sample, bytes_per_sample);
    }
    return frame;
}
