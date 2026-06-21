#pragma once

#include "au2/ReaderBackend.hpp"
#include "input2.h"

#include <memory>

namespace au2 {

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

private:
    explicit InputSession(SessionCore core) noexcept;

    SessionCore core_ {};
    int audio_delay_ = 0;
};

} // namespace au2
