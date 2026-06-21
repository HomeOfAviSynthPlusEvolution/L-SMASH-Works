#pragma once

#include "au2/ReaderOptions.hpp"

#include "../../common/audio_output.h"

#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace au2 {

int setup_audio_rendering(lw_audio_output_handler_t* aohp, AVCodecContext* ctx, AudioOptions* opt, WAVEFORMATEX* format);

} // namespace au2
