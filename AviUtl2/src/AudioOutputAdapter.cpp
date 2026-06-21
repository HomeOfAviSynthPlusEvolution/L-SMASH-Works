#include "au2/AudioOutputAdapter.hpp"

extern "C" {
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cmath>

namespace au2 {
namespace {

enum AVSampleFormat decide_audio_output_sample_format(enum AVSampleFormat input_sample_format, int input_bits_per_sample)
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
        if (input_bits_per_sample == 0) {
            return AV_SAMPLE_FMT_S32;
        }
        if (input_bits_per_sample <= 8) {
            return AV_SAMPLE_FMT_U8;
        }
        if (input_bits_per_sample <= 16) {
            return AV_SAMPLE_FMT_S16;
        }
        return AV_SAMPLE_FMT_S32;
    }
}

void set_mix_level(SwrContext* swr_ctx, const char* opt, int value)
{
    av_opt_set_double(swr_ctx, opt, value == 71 ? M_SQRT1_2 : (value / 100.0), 0);
}

} // namespace

int setup_audio_rendering(lw_audio_output_handler_t* aohp, AVCodecContext* ctx, AudioOptions* opt, WAVEFORMATEX* format)
{
    if (!aohp || !ctx || !opt || !format) {
        return -1;
    }

    if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&ctx->ch_layout, ctx->ch_layout.nb_channels);
    }
    if (opt->channel_layout != 0) {
        av_channel_layout_uninit(&aohp->output_channel_layout);
        av_channel_layout_from_mask(&aohp->output_channel_layout, opt->channel_layout);
    } else if (aohp->output_channel_layout.order == AV_CHANNEL_ORDER_UNSPEC || aohp->output_channel_layout.nb_channels == 0) {
        av_channel_layout_copy(&aohp->output_channel_layout, &ctx->ch_layout);
    }

    if (opt->sample_rate > 0) {
        aohp->output_sample_rate = opt->sample_rate;
    }
    if (aohp->output_sample_rate <= 0) {
        aohp->output_sample_rate = ctx->sample_rate;
    }

    aohp->output_sample_format = decide_audio_output_sample_format(aohp->output_sample_format, aohp->output_bits_per_sample);
    if (aohp->output_sample_format == AV_SAMPLE_FMT_S32 && (aohp->output_bits_per_sample == 0 || aohp->output_bits_per_sample == 24)) {
        aohp->s24_output = 1;
        aohp->output_bits_per_sample = 24;
    } else {
        aohp->output_bits_per_sample = av_get_bytes_per_sample(aohp->output_sample_format) * 8;
    }

    const int input_channels = ctx->ch_layout.nb_channels;
    if (av_sample_fmt_is_planar(ctx->sample_fmt)) {
        aohp->input_planes = input_channels;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt);
    } else {
        aohp->input_planes = 1;
        aohp->input_block_align = av_get_bytes_per_sample(ctx->sample_fmt) * input_channels;
    }

    const int output_channels = aohp->output_channel_layout.nb_channels;
    aohp->output_block_align = (output_channels * aohp->output_bits_per_sample) / 8;

    SwrContext* swr_ctx = swr_alloc();
    if (!swr_ctx) {
        return -1;
    }
    aohp->swr_ctx = swr_ctx;
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &ctx->ch_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", ctx->sample_fmt, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", ctx->sample_rate, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &aohp->output_channel_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", aohp->output_sample_format, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", aohp->output_sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "internal_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
    set_mix_level(swr_ctx, "center_mix_level", opt->mix_level[MIX_LEVEL_INDEX_CENTER]);
    set_mix_level(swr_ctx, "surround_mix_level", opt->mix_level[MIX_LEVEL_INDEX_SURROUND]);
    set_mix_level(swr_ctx, "lfe_mix_level", opt->mix_level[MIX_LEVEL_INDEX_LFE]);
    if (swr_init(swr_ctx) < 0) {
        return -1;
    }

    format->nChannels = static_cast<WORD>(output_channels);
    format->nSamplesPerSec = static_cast<DWORD>(aohp->output_sample_rate);
    format->wBitsPerSample = static_cast<WORD>(aohp->output_bits_per_sample);
    format->nBlockAlign = static_cast<WORD>(aohp->output_block_align);
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->cbSize = 0;
    return 0;
}

} // namespace au2
