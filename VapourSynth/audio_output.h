/*****************************************************************************
 * audio_output.h
 *****************************************************************************/

#ifndef VS_AUDIO_OUTPUT_H
#define VS_AUDIO_OUTPUT_H

#include <VapourSynth4.h>

#include "../common/audio_output.h"

int vs_setup_audio_rendering(lw_audio_output_handler_t* aohp, AVCodecContext* ctx, const char* channel_layout, int sample_rate,
    VSAudioInfo* ai, VSCore* core, const VSAPI* vsapi);

VSFrame* vs_interleaved_audio_frame(
    const lw_audio_output_handler_t* aohp, const uint8_t* interleaved, int sample_count, VSCore* core, const VSAPI* vsapi);

#endif // !VS_AUDIO_OUTPUT_H
