#pragma once

#include "au2/ReaderBackend.hpp"
#include "input2.h"

#include <memory>
#include <string>
#include <vector>

namespace au2 {

struct InputTrackList {
    struct Track {
        int stream_index = -1;
        int media_track_index = -1;
    };
    std::vector<Track> video;
    std::vector<Track> audio;
};

class InputSession {
public:
    static std::unique_ptr<InputSession> open(LPCWSTR path);

    ~InputSession();

    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;

    bool info(INPUT_INFO& out) const noexcept;
    int read_video(int frame, void* dst) noexcept;
    int read_audio(int start, int length, void* dst) noexcept;
    bool is_keyframe(int frame) noexcept;
    int set_track(int type, int index) noexcept;

private:
    InputSession(std::string path, ReaderOptions options, InputTrackList tracks) noexcept;

    bool rebuild_core() noexcept;
    int track_count(int type) const noexcept;

    std::string path_;
    ReaderOptions options_ {};
    InputTrackList tracks_;
    SessionCore core_ {};
    int audio_delay_ = 0;
};

} // namespace au2
