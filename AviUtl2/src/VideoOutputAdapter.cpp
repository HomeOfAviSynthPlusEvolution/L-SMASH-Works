#include "au2/VideoOutputAdapter.hpp"

#include "au2/ReaderBackend.hpp"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

#include "../../common/utils.h"

#include <cstring>

namespace au2 {
namespace {

enum : DWORD {
    OUTPUT_TAG_YUY2 = mmioFOURCC('Y', 'U', 'Y', '2'),
    OUTPUT_TAG_YC48 = mmioFOURCC('Y', 'C', '4', '8'),
    OUTPUT_TAG_LW48 = mmioFOURCC('L', 'W', '4', '8'),
};

enum : int {
    YUY2_SIZE = 2,
    RGB24_SIZE = 3,
    RGBA_SIZE = 4,
    YC48_SIZE = 6,
    LW48_SIZE = 6,
};

struct PixelLw48 {
    unsigned short y;
    unsigned short cb;
    unsigned short cr;
};

void free_video_output_handler(void* private_handler)
{
    auto* handler = static_cast<VideoOutputHandler*>(private_handler);
    if (!handler) {
        return;
    }
    lw_free(handler->back_ground);
    av_free(handler->another_chroma);
    if (handler->yuv444p16) {
        av_freep(&handler->yuv444p16->data[0]);
    }
    av_frame_free(&handler->yuv444p16);
    delete handler;
}

uint16_t read_le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void write_le16(uint8_t* p, uint16_t v) noexcept
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

int packed_sws_convert(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    static int log_priv = 0;
    static lw_log_handler_t log_handler = { "lwinput2", LW_LOG_FATAL, &log_priv, au2_log_noop };
    if (update_scaler_configuration_if_needed(&vohp->scaler, &log_handler, picture) < 0) {
        return -1;
    }

    auto* handler = static_cast<VideoOutputHandler*>(vohp->private_handler);
    uint8_t* dst_data[4] = { buf, nullptr, nullptr, nullptr };
    int dst_linesize[4] = { handler->output_linesize, 0, 0, 0 };
    sws_scale(vohp->scaler.sws_ctx, picture->data, picture->linesize, 0, picture->height, dst_data, dst_linesize);
    return handler->output_frame_size;
}

int to_yuv444p16(lw_video_output_handler_t* vohp, AVFrame* picture, AVFrame* yuv444p16)
{
    static int log_priv = 0;
    static lw_log_handler_t log_handler = { "lwinput2", LW_LOG_FATAL, &log_priv, au2_log_noop };
    if (update_scaler_configuration_if_needed(&vohp->scaler, &log_handler, picture) < 0) {
        return -1;
    }
    return sws_scale(vohp->scaler.sws_ctx, picture->data, picture->linesize, 0, picture->height, yuv444p16->data, yuv444p16->linesize);
}

void pack_yuv444p16_to_lw48(uint8_t* buf, int buf_linesize, const AVFrame* yuv444p16, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        uint8_t* dst = buf + buf_linesize * y;
        const uint8_t* src_y = yuv444p16->data[0] + yuv444p16->linesize[0] * y;
        const uint8_t* src_cb = yuv444p16->data[1] + yuv444p16->linesize[1] * y;
        const uint8_t* src_cr = yuv444p16->data[2] + yuv444p16->linesize[2] * y;
        for (int x = 0; x < width; ++x) {
            dst[0] = src_y[0];
            dst[1] = src_y[1];
            dst[2] = src_cb[0];
            dst[3] = src_cb[1];
            dst[4] = src_cr[0];
            dst[5] = src_cr[1];
            dst += LW48_SIZE;
            src_y += 2;
            src_cb += 2;
            src_cr += 2;
        }
    }
}

void pack_yuv444p16_to_yc48(uint8_t* buf, int buf_linesize, const AVFrame* yuv444p16, int width, int height, int full_range)
{
    static const uint32_t y_coef[2] = { 1197, 4770 };
    static const uint32_t y_shift[2] = { 14, 16 };
    static const uint32_t uv_coef[2] = { 4682, 4662 };
    static const uint32_t uv_offset[2] = { 32768, 589824 };

    full_range = full_range ? 1 : 0;
    for (int y = 0; y < height; ++y) {
        uint8_t* dst = buf + buf_linesize * y;
        const uint8_t* src_y = yuv444p16->data[0] + yuv444p16->linesize[0] * y;
        const uint8_t* src_cb = yuv444p16->data[1] + yuv444p16->linesize[1] * y;
        const uint8_t* src_cr = yuv444p16->data[2] + yuv444p16->linesize[2] * y;
        for (int x = 0; x < width; ++x) {
            const uint16_t yy = static_cast<uint16_t>(
                (((static_cast<int32_t>(read_le16(src_y)) * y_coef[full_range])) >> y_shift[full_range]) - 299);
            const uint16_t cb = static_cast<uint16_t>(
                ((static_cast<int32_t>(read_le16(src_cb) - 32768) * uv_coef[full_range] + uv_offset[full_range])) >> 16);
            const uint16_t cr = static_cast<uint16_t>(
                ((static_cast<int32_t>(read_le16(src_cr) - 32768) * uv_coef[full_range] + uv_offset[full_range])) >> 16);
            write_le16(dst, yy);
            write_le16(dst + 2, cb);
            write_le16(dst + 4, cr);
            dst += YC48_SIZE;
            src_y += 2;
            src_cb += 2;
            src_cr += 2;
        }
    }
}

int yuv444p16_to_lw48(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    auto* handler = static_cast<VideoOutputHandler*>(vohp->private_handler);
    if (!handler || !handler->yuv444p16) {
        return -1;
    }
    const int output_height = to_yuv444p16(vohp, picture, handler->yuv444p16);
    if (output_height <= 0) {
        return -1;
    }
    pack_yuv444p16_to_lw48(buf, handler->output_linesize, handler->yuv444p16, vohp->scaler.input_width, output_height);
    return handler->output_linesize * output_height;
}

int yuv444p16_to_yc48(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    auto* handler = static_cast<VideoOutputHandler*>(vohp->private_handler);
    if (!handler || !handler->yuv444p16) {
        return -1;
    }
    const int output_height = to_yuv444p16(vohp, picture, handler->yuv444p16);
    if (output_height <= 0) {
        return -1;
    }
    pack_yuv444p16_to_yc48(
        buf, handler->output_linesize, handler->yuv444p16, vohp->scaler.input_width, output_height, vohp->scaler.input_yuv_range);
    return handler->output_linesize * output_height;
}

int choose_output(enum AVPixelFormat input_pixel_format, enum AVPixelFormat& output_pixel_format, int& pixel_size, DWORD& compression)
{
    avoid_yuv_scale_conversion(&input_pixel_format);
    switch (input_pixel_format) {
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P:
        output_pixel_format = AV_PIX_FMT_BGRA;
        pixel_size = RGBA_SIZE;
        compression = BI_RGB;
        return 0;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_PAL8:
        output_pixel_format = AV_PIX_FMT_BGR24;
        pixel_size = RGB24_SIZE;
        compression = BI_RGB;
        return 0;
    default:
        output_pixel_format = AV_PIX_FMT_YUYV422;
        pixel_size = YUY2_SIZE;
        compression = OUTPUT_TAG_YUY2;
        return 0;
    }
}

} // namespace

int setup_video_rendering(lw_video_output_handler_t* vohp, VideoOptions* opt, BITMAPINFOHEADER* format, int output_width,
    int output_height, enum AVPixelFormat input_pixel_format)
{
    if (!vohp || !opt || !format || output_width <= 0 || output_height <= 0) {
        return -1;
    }

    auto* handler = new VideoOutputHandler();
    vohp->private_handler = handler;
    vohp->free_private_handler = free_video_output_handler;

    enum AVPixelFormat output_pixel_format = AV_PIX_FMT_YUYV422;
    int pixel_size = YUY2_SIZE;
    DWORD compression = OUTPUT_TAG_YUY2;
    choose_output(input_pixel_format, output_pixel_format, pixel_size, compression);
    if (opt->colorspace == OutputColorspace::Rgb24) {
        output_pixel_format = AV_PIX_FMT_BGR24;
        pixel_size = RGB24_SIZE;
        compression = BI_RGB;
    } else if (opt->colorspace == OutputColorspace::Rgba) {
        output_pixel_format = AV_PIX_FMT_BGRA;
        pixel_size = RGBA_SIZE;
        compression = BI_RGB;
    } else if (opt->colorspace == OutputColorspace::Yc48 || opt->colorspace == OutputColorspace::Lw48) {
        output_pixel_format = AV_PIX_FMT_YUV444P16LE;
        pixel_size = opt->colorspace == OutputColorspace::Yc48 ? YC48_SIZE : LW48_SIZE;
        compression = opt->colorspace == OutputColorspace::Yc48 ? OUTPUT_TAG_YC48 : OUTPUT_TAG_LW48;
    }

    setup_video_rendering(vohp, 1 << opt->scaler, output_width, output_height, output_pixel_format, nullptr, nullptr);

    format->biSize = sizeof(BITMAPINFOHEADER);
    format->biWidth = output_width;
    format->biHeight = output_height;
    format->biPlanes = 1;
    format->biBitCount = static_cast<WORD>(pixel_size << 3);
    format->biCompression = compression;

    handler->output_linesize = AU2_MAKE_PITCH(output_width * format->biBitCount);
    handler->output_frame_size = static_cast<uint32_t>(handler->output_linesize * output_height);
    format->biSizeImage = handler->output_frame_size;
    if (compression == OUTPUT_TAG_YC48) {
        handler->convert_colorspace = yuv444p16_to_yc48;
    } else if (compression == OUTPUT_TAG_LW48) {
        handler->convert_colorspace = yuv444p16_to_lw48;
    } else {
        handler->convert_colorspace = packed_sws_convert;
    }
    handler->back_ground = handler->output_frame_size > 0 ? static_cast<uint8_t*>(lw_malloc_zero(handler->output_frame_size)) : nullptr;
    if (!handler->back_ground) {
        return -1;
    }

    if (compression == OUTPUT_TAG_YC48 || compression == OUTPUT_TAG_LW48) {
        handler->yuv444p16 = av_frame_alloc();
        if (!handler->yuv444p16) {
            return -1;
        }
        if (av_image_alloc(handler->yuv444p16->data, handler->yuv444p16->linesize, output_width, output_height, AV_PIX_FMT_YUV444P16LE, 32)
            < 0) {
            return -1;
        }
    }

    if (compression == OUTPUT_TAG_YUY2) {
        uint8_t* pic = handler->back_ground;
        for (int y = 0; y < output_height; ++y) {
            for (int x = 0; x < handler->output_linesize; x += YUY2_SIZE) {
                pic[x] = 0;
                pic[x + 1] = 128;
            }
            pic += handler->output_linesize;
        }
    } else if (compression == OUTPUT_TAG_LW48) {
        const PixelLw48 black_pix = { 4096, 32768, 32768 };
        uint8_t* pic = handler->back_ground;
        for (int y = 0; y < output_height; ++y) {
            auto* pix = reinterpret_cast<PixelLw48*>(pic);
            for (int x = 0; x < output_width; ++x) {
                *pix++ = black_pix;
            }
            pic += handler->output_linesize;
        }
    }

    return 0;
}

int convert_colorspace(lw_video_output_handler_t* vohp, AVFrame* picture, uint8_t* buf)
{
    auto* handler = static_cast<VideoOutputHandler*>(vohp->private_handler);
    if (!handler || !handler->convert_colorspace) {
        return 0;
    }
    if (vohp->scaler.frame_prop_change_flags & (LW_FRAME_PROP_CHANGE_FLAG_WIDTH | LW_FRAME_PROP_CHANGE_FLAG_HEIGHT)) {
        std::memcpy(buf, handler->back_ground, handler->output_frame_size);
    }
    if (handler->convert_colorspace(vohp, picture, buf) < 0) {
        return 0;
    }
    return static_cast<int>(handler->output_frame_size);
}

} // namespace au2
