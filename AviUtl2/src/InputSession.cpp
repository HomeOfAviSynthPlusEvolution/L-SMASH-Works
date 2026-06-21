#include "au2/InputSession.hpp"

#include "au2/Path.hpp"

#include "../../common/utils.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace au2 {
namespace {

int auto_threads() noexcept
{
    const char* processors = std::getenv("NUMBER_OF_PROCESSORS");
    if (!processors) {
        return 0;
    }
    const int value = std::atoi(processors);
    return std::clamp(value, 0, 16);
}

void debug_log(const char* message) noexcept
{
    const char* path = std::getenv("LWINPUT2_DEBUG_LOG");
    if (!path || !*path) {
        return;
    }
    try {
        std::ofstream out(path, std::ios::app);
        out << message << '\n';
    } catch (...) {
    }
}

void debug_probe_avformat(const char* path) noexcept
{
    const char* log_path = std::getenv("LWINPUT2_DEBUG_LOG");
    if (!log_path || !*log_path) {
        return;
    }
    AVFormatContext* format = nullptr;
    const int ret = avformat_open_input(&format, path, nullptr, nullptr);
    char error[AV_ERROR_MAX_STRING_SIZE] {};
    av_strerror(ret, error, sizeof(error));
    try {
        std::ofstream out(log_path, std::ios::app);
        out << "avformat_open_input probe ret=" << ret << " error=" << error << '\n';
    } catch (...) {
    }
    if (format) {
        avformat_close_input(&format);
    }
}

ReaderOptions default_reader_options()
{
    ReaderOptions options;
    options.threads = auto_threads();
    options.preferred_decoder_names = nullptr;
    return options;
}

void close_core(SessionCore& core) noexcept
{
    if (core.video_cleanup) {
        core.video_cleanup(&core);
    }
    if (core.audio_cleanup) {
        core.audio_cleanup(&core);
    }
    if (core.close_file) {
        core.close_file(core.global_private);
    } else {
        if (core.close_video_file) {
            core.close_video_file(core.video_private);
        }
        if (core.close_audio_file && core.audio_private != core.video_private) {
            core.close_audio_file(core.audio_private);
        }
    }
    core = {};
}

bool try_reader(SessionCore& core, const ReaderCallbacks& reader, char* path, ReaderOptions& options)
{
    debug_log(reader.type == ReaderType::Libavsmash ? "try libavsmash" : "try lwlibav");
    void* private_stuff = reader.open_file ? reader.open_file(path, &options) : nullptr;
    if (!private_stuff) {
        debug_log("open_file failed");
        return false;
    }
    debug_log("open_file succeeded");

    bool video_none = true;
    bool audio_none = true;
    if (!core.video_private) {
        core.video_private = private_stuff;
        if (reader.get_video_track && reader.get_video_track(&core, &options.video) == 0) {
            core.video_reader = reader.type;
            core.read_video = reader.read_video;
            core.is_keyframe = reader.is_keyframe;
            core.video_cleanup = reader.video_cleanup;
            core.close_video_file = reader.close_file;
            video_none = false;
            debug_log("video track selected");
        } else {
            core.video_private = nullptr;
            debug_log("video track unavailable");
        }
    }

    if (!core.audio_private) {
        core.audio_private = private_stuff;
        if (reader.get_audio_track && reader.get_audio_track(&core, &options.audio) == 0) {
            core.audio_reader = reader.type;
            core.read_audio = reader.read_audio;
            core.delay_audio = reader.delay_audio;
            core.audio_cleanup = reader.audio_cleanup;
            core.close_audio_file = reader.close_file;
            audio_none = false;
            debug_log("audio track selected");
        } else {
            core.audio_private = nullptr;
            debug_log("audio track unavailable");
        }
    }

    if (video_none && audio_none) {
        debug_log("reader had no selected streams");
        if (reader.close_file) {
            reader.close_file(private_stuff);
        }
        return false;
    }

    if (reader.destroy_disposable) {
        reader.destroy_disposable(private_stuff);
    }
    return true;
}

} // namespace

void au2_log_noop(lw_log_handler_t*, lw_log_level, const char*)
{
}

InputSession::InputSession(SessionCore core) noexcept
    : core_(core)
{
}

InputSession::~InputSession()
{
    close_core(core_);
}

std::unique_ptr<InputSession> InputSession::open(LPCWSTR path)
{
    if (path == nullptr || has_script_extension(path)) {
        debug_log("path rejected");
        return nullptr;
    }

    std::string utf8_path = wide_to_utf8(path);
    if (utf8_path.empty()) {
        debug_log("path conversion produced empty path");
        return nullptr;
    }
    debug_log("path converted");
    debug_probe_avformat(utf8_path.c_str());

    ReaderOptions options = default_reader_options();
    SessionCore core;

    try_reader(core, libavsmash_reader(), utf8_path.data(), options);
    if (core.video_reader == ReaderType::None || core.audio_reader == ReaderType::None) {
        try_reader(core, lwlibav_reader(), utf8_path.data(), options);
    }

    if (core.video_reader == core.audio_reader && core.video_reader != ReaderType::None) {
        core.global_private = core.video_private;
        core.close_file = core.close_video_file;
        core.close_video_file = nullptr;
        core.close_audio_file = nullptr;
    }

    if (core.video_reader == ReaderType::None && core.audio_reader == ReaderType::None) {
        debug_log("no streams selected");
        close_core(core);
        return nullptr;
    }

    debug_log("session opened");
    return std::unique_ptr<InputSession>(new InputSession(core));
}

bool InputSession::info(INPUT_INFO& out) const noexcept
{
    out = {};
    if (core_.video_reader != ReaderType::None) {
        out.flag |= INPUT_INFO::FLAG_VIDEO;
        out.rate = core_.framerate_num;
        out.scale = core_.framerate_den;
        out.n = static_cast<int>(std::min<uint32_t>(core_.video_sample_count, static_cast<uint32_t>(INT_MAX)));
        out.format = const_cast<BITMAPINFOHEADER*>(&core_.video_format);
        out.format_size = static_cast<int>(core_.video_format.biSize);
    }
    if (core_.audio_reader != ReaderType::None) {
        out.flag |= INPUT_INFO::FLAG_AUDIO;
        const uint64_t samples = static_cast<uint64_t>(core_.audio_pcm_sample_count) + static_cast<uint64_t>(std::max(audio_delay_, 0));
        out.audio_n = static_cast<int>(std::min<uint64_t>(samples, static_cast<uint64_t>(INT_MAX)));
        out.audio_format = const_cast<WAVEFORMATEX*>(&core_.audio_format.Format);
        out.audio_format_size = sizeof(WAVEFORMATEX) + core_.audio_format.Format.cbSize;
    }
    return out.flag != 0;
}

int InputSession::read_video(int frame, void* dst) noexcept
{
    if (!dst || frame < 0 || !core_.read_video) {
        return 0;
    }
    return core_.read_video(&core_, frame, dst);
}

int InputSession::read_audio(int start, int length, void* dst) noexcept
{
    if (!dst || start < 0 || length <= 0 || !core_.read_audio) {
        return 0;
    }
    int adjusted_start = start;
    if (core_.delay_audio && core_.delay_audio(&core_, &adjusted_start, length, audio_delay_)) {
        return core_.read_audio(&core_, adjusted_start, length, dst);
    }
    const uint8_t silence = core_.audio_format.Format.wBitsPerSample == 8 ? 128 : 0;
    std::memset(dst, silence, static_cast<size_t>(length) * core_.audio_format.Format.nBlockAlign);
    return length;
}

bool InputSession::is_keyframe(int frame) noexcept
{
    if (frame < 0 || frame >= static_cast<int>(core_.video_sample_count)) {
        return false;
    }
    return core_.is_keyframe ? core_.is_keyframe(&core_, frame) != 0 : true;
}

} // namespace au2
