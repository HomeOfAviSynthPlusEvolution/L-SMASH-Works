#include "au2/InputSession.hpp"

#include "au2/Path.hpp"

#include "../../common/utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

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

bool has_decoder(const AVCodecParameters* codecpar) noexcept
{
    return codecpar && avcodec_find_decoder(codecpar->codec_id) != nullptr;
}

InputTrackList probe_tracks(const char* path) noexcept
{
    InputTrackList tracks;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path, nullptr, nullptr) != 0) {
        return tracks;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return tracks;
    }
    for (unsigned int i = 0; i < format->nb_streams; ++i) {
        const AVCodecParameters* codecpar = format->streams[i]->codecpar;
        if (!has_decoder(codecpar)) {
            continue;
        }
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            tracks.video.push_back({ static_cast<int>(i), static_cast<int>(tracks.video.size()) });
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            tracks.audio.push_back({ static_cast<int>(i), static_cast<int>(tracks.audio.size()) });
        }
    }
    avformat_close_input(&format);
    return tracks;
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

bool build_core(SessionCore& core, std::string& path, ReaderOptions& options) noexcept
{
    SessionCore next;

    try_reader(next, libavsmash_reader(), path.data(), options);
    if (next.video_reader == ReaderType::None || next.audio_reader == ReaderType::None) {
        try_reader(next, lwlibav_reader(), path.data(), options);
    }

    if (next.video_reader == next.audio_reader && next.video_reader != ReaderType::None) {
        next.global_private = next.video_private;
        next.close_file = next.close_video_file;
        next.close_video_file = nullptr;
        next.close_audio_file = nullptr;
    }

    if (next.video_reader == ReaderType::None && next.audio_reader == ReaderType::None) {
        debug_log("no streams selected");
        close_core(next);
        return false;
    }

    close_core(core);
    core = next;
    next = {};
    return true;
}

} // namespace

void au2_log_noop(lw_log_handler_t*, lw_log_level, const char*)
{
}

InputSession::InputSession(std::string path, ReaderOptions options, InputTrackList tracks) noexcept
    : path_(std::move(path))
    , options_(options)
    , tracks_(std::move(tracks))
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
    InputTrackList tracks = probe_tracks(utf8_path.c_str());
    std::unique_ptr<InputSession> session(new InputSession(std::move(utf8_path), options, std::move(tracks)));
    if (!session->rebuild_core()) {
        return nullptr;
    }

    debug_log("session opened");
    return session;
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

int InputSession::set_track(int type, int index) noexcept
{
    if (index == -1) {
        return track_count(type);
    }
    ReaderOptions previous_options = options_;
    if (type == INPUT_PLUGIN_TABLE::TRACK_TYPE_VIDEO) {
        if (index < 0 || index >= static_cast<int>(tracks_.video.size())) {
            return -1;
        }
        options_.force_video = 1;
        const auto& track = tracks_.video[static_cast<size_t>(index)];
        options_.force_video_index = track.stream_index;
        options_.libavsmash_video_media_index = track.media_track_index;
    } else if (type == INPUT_PLUGIN_TABLE::TRACK_TYPE_AUDIO) {
        if (index < 0 || index >= static_cast<int>(tracks_.audio.size())) {
            return -1;
        }
        options_.force_audio = 1;
        const auto& track = tracks_.audio[static_cast<size_t>(index)];
        options_.force_audio_index = track.stream_index;
        options_.libavsmash_audio_media_index = track.media_track_index;
    } else {
        return -1;
    }
    if (rebuild_core()) {
        return index;
    }
    options_ = previous_options;
    return -1;
}

bool InputSession::rebuild_core() noexcept
{
    return build_core(core_, path_, options_);
}

int InputSession::track_count(int type) const noexcept
{
    if (type == INPUT_PLUGIN_TABLE::TRACK_TYPE_VIDEO) {
        return static_cast<int>(tracks_.video.size());
    }
    if (type == INPUT_PLUGIN_TABLE::TRACK_TYPE_AUDIO) {
        return static_cast<int>(tracks_.audio.size());
    }
    return -1;
}

} // namespace au2
