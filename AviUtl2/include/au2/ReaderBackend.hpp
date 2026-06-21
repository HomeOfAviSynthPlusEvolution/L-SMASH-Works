#pragma once

#include "au2/ReaderOptions.hpp"

#include <windows.h>
#include <mmreg.h>

namespace au2 {

struct SessionCore;

struct ReaderCallbacks {
    ReaderType type = ReaderType::None;
    void* (*open_file)(char* file_name, ReaderOptions* opt) = nullptr;
    int (*get_video_track)(SessionCore* session, VideoOptions* opt) = nullptr;
    int (*get_audio_track)(SessionCore* session, AudioOptions* opt) = nullptr;
    void (*destroy_disposable)(void* private_stuff) = nullptr;
    int (*read_video)(SessionCore* session, int sample_number, void* buf) = nullptr;
    int (*read_audio)(SessionCore* session, int start, int wanted_length, void* buf) = nullptr;
    int (*is_keyframe)(SessionCore* session, int sample_number) = nullptr;
    int (*delay_audio)(SessionCore* session, int* start, int wanted_length, int audio_delay) = nullptr;
    void (*video_cleanup)(SessionCore* session) = nullptr;
    void (*audio_cleanup)(SessionCore* session) = nullptr;
    void (*close_file)(void* private_stuff) = nullptr;
};

struct SessionCore {
    void* global_private = nullptr;
    void (*close_file)(void* private_stuff) = nullptr;

    ReaderType video_reader = ReaderType::None;
    void* video_private = nullptr;
    BITMAPINFOHEADER video_format {};
    int framerate_num = 0;
    int framerate_den = 1;
    uint32_t video_sample_count = 0;
    int (*read_video)(SessionCore* session, int sample_number, void* buf) = nullptr;
    int (*is_keyframe)(SessionCore* session, int sample_number) = nullptr;
    void (*video_cleanup)(SessionCore* session) = nullptr;
    void (*close_video_file)(void* private_stuff) = nullptr;

    ReaderType audio_reader = ReaderType::None;
    void* audio_private = nullptr;
    WAVEFORMATEXTENSIBLE audio_format {};
    uint32_t audio_pcm_sample_count = 0;
    int (*read_audio)(SessionCore* session, int start, int wanted_length, void* buf) = nullptr;
    int (*delay_audio)(SessionCore* session, int* start, int wanted_length, int audio_delay) = nullptr;
    void (*audio_cleanup)(SessionCore* session) = nullptr;
    void (*close_audio_file)(void* private_stuff) = nullptr;
};

const ReaderCallbacks& libavsmash_reader() noexcept;
const ReaderCallbacks& lwlibav_reader() noexcept;

void au2_log_noop(lw_log_handler_t* lhp, lw_log_level level, const char* message);

} // namespace au2
