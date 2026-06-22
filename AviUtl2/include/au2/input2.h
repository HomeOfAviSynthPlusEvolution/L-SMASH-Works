#pragma once

#include <windows.h>
#include <mmsystem.h>

struct INPUT_INFO {
    int flag;
    static constexpr int FLAG_VIDEO = 1;
    static constexpr int FLAG_AUDIO = 2;
    static constexpr int FLAG_TIME_TO_FRAME = 16;
    int rate;
    int scale;
    int n;
    BITMAPINFOHEADER* format;
    int format_size;
    int audio_n;
    WAVEFORMATEX* audio_format;
    int audio_format_size;
};

typedef void* INPUT_HANDLE;

struct INPUT_PLUGIN_TABLE {
    int flag;
    static constexpr int FLAG_VIDEO = 1;
    static constexpr int FLAG_AUDIO = 2;
    static constexpr int FLAG_CONCURRENT = 16;
    static constexpr int FLAG_MULTI_TRACK = 32;
    LPCWSTR name;
    LPCWSTR filefilter;
    LPCWSTR information;
    INPUT_HANDLE (*func_open)(LPCWSTR file);
    bool (*func_close)(INPUT_HANDLE ih);
    bool (*func_info_get)(INPUT_HANDLE ih, INPUT_INFO* iip);
    int (*func_read_video)(INPUT_HANDLE ih, int frame, void* buf);
    int (*func_read_audio)(INPUT_HANDLE ih, int start, int length, void* buf);
    bool (*func_config)(HWND hwnd, HINSTANCE dll_hinst);
    int (*func_set_track)(INPUT_HANDLE ih, int type, int index);
    static constexpr int TRACK_TYPE_VIDEO = 0;
    static constexpr int TRACK_TYPE_AUDIO = 1;
    int (*func_time_to_frame)(INPUT_HANDLE ih, double time);
};
