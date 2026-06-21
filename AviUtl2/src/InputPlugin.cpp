#include "au2/InputPlugin.hpp"

#include "au2/InputSession.hpp"

#include <memory>

namespace au2 {
namespace {

InputSession* from_handle(INPUT_HANDLE ih) noexcept
{
    return static_cast<InputSession*>(ih);
}

INPUT_HANDLE open_file(LPCWSTR file)
{
    try {
        auto session = InputSession::open(file);
        return session.release();
    } catch (...) {
        return nullptr;
    }
}

bool close_file(INPUT_HANDLE ih)
{
    delete from_handle(ih);
    return true;
}

bool get_info(INPUT_HANDLE ih, INPUT_INFO* iip)
{
    auto* session = from_handle(ih);
    if (!session || !iip) {
        return false;
    }
    return session->info(*iip);
}

int read_video(INPUT_HANDLE ih, int frame, void* buf)
{
    auto* session = from_handle(ih);
    return session ? session->read_video(frame, buf) : 0;
}

int read_audio(INPUT_HANDLE ih, int start, int length, void* buf)
{
    auto* session = from_handle(ih);
    return session ? session->read_audio(start, length, buf) : 0;
}

bool config(HWND, HINSTANCE)
{
    return true;
}

INPUT_PLUGIN_TABLE g_input_plugin_table {
    INPUT_PLUGIN_TABLE::FLAG_VIDEO | INPUT_PLUGIN_TABLE::FLAG_AUDIO,
    L"L-SMASH Works File Reader2",
    L"Media Files (*.mp4;*.mkv;*.mov;*.avi;*.m2ts;*.mts;*.ts;*.mpg;*.mpeg;*.webm;*.flv)\0*.mp4;*.mkv;*.mov;*.avi;*.m2ts;*.mts;*.ts;*.mpg;*.mpeg;*.webm;*.flv\0All Files (*.*)\0*.*\0",
    L"L-SMASH Works AviUtl2 source filter",
    open_file,
    close_file,
    get_info,
    read_video,
    read_audio,
    config,
    nullptr,
    nullptr,
};

} // namespace

bool initialize_plugin(DWORD) noexcept
{
    return true;
}

void uninitialize_plugin() noexcept
{
}

INPUT_PLUGIN_TABLE* input_plugin_table() noexcept
{
    return &g_input_plugin_table;
}

} // namespace au2
