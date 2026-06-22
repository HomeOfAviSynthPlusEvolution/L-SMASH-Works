#pragma once

#include <cstdint>
#include <windows.h>
#include <mmreg.h>

#include "../../common/utils.h"

#define AU2_MAKE_PITCH(bits) ((((bits) + 31) & ~31) >> 3)
#define AU2_PREFERRED_DECODER_NAMES_BUFSIZE 512

namespace au2 {

enum class ReaderType {
    None = 0,
    Libavsmash = 1,
    Lwlibav = 2,
};

enum class OutputColorspace {
    Auto = 0,
    Yuy2 = 0,
    Rgb24 = 1,
    Rgba = 2,
    Yc48 = 3,
    Lw48 = 4,
};

struct VideoOptions {
    int seek_mode = 0;
    int forward_seek_threshold = 10;
    int scaler = 0;
    int apply_repeat_flag = 1;
    int field_dominance = 0;
    struct {
        int active = 0;
        int framerate_num = 60000;
        int framerate_den = 1001;
    } vfr2cfr;
    OutputColorspace colorspace = OutputColorspace::Auto;
};

enum {
    MIX_LEVEL_INDEX_CENTER = 0,
    MIX_LEVEL_INDEX_SURROUND,
    MIX_LEVEL_INDEX_LFE,
};

struct AudioOptions {
    uint64_t channel_layout = 0;
    int sample_rate = 0;
    int mix_level[3] = { 71, 71, 0 };
};

struct ReaderOptions {
    int threads = 0;
    int av_sync = 1;
    char preferred_decoder_names_buf[AU2_PREFERRED_DECODER_NAMES_BUFSIZE] {};
    const char** preferred_decoder_names = nullptr;
    int no_create_index = 0;
    int force_video = 0;
    int force_video_index = -1;
    int force_audio = 0;
    int force_audio_index = -1;
    int libavsmash_video_media_index = 0;
    int libavsmash_audio_media_index = 0;
    VideoOptions video;
    AudioOptions audio;
};

} // namespace au2
