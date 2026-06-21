#pragma once

#include "au2/ReaderOptions.hpp"

#include "../../common/video_output.h"

#include <windows.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace au2 {

typedef void FuncConvertColorspace(lw_video_output_handler_t* vohp, AVFrame* picture, void* buf);
typedef int FuncConvertColorspaceResult(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf);

struct VideoOutputHandler {
    int output_linesize = 0;
    uint32_t output_frame_size = 0;
    uint8_t* back_ground = nullptr;
    uint8_t* another_chroma = nullptr;
    uint32_t another_chroma_size = 0;
    AVFrame* yuv444p16 = nullptr;
    int (*convert_colorspace)(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf) = nullptr;
};

int setup_video_rendering(lw_video_output_handler_t* vohp, VideoOptions* opt, BITMAPINFOHEADER* format, int output_width,
    int output_height, enum AVPixelFormat input_pixel_format);

int convert_colorspace(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf);

} // namespace au2
