cmake_minimum_required(VERSION 3.28)

add_executable(LSMASHSource_indexing)

target_sources(LSMASHSource_indexing PRIVATE
    "${PROJECT_SOURCE_DIR}/cli/index.c"
    "${PROJECT_SOURCE_DIR}/common/audio_output.c"
    "${PROJECT_SOURCE_DIR}/common/audio_output.h"
    "${PROJECT_SOURCE_DIR}/common/decode.c"
    "${PROJECT_SOURCE_DIR}/common/decode.h"
    "${PROJECT_SOURCE_DIR}/common/lwindex.c"
    "${PROJECT_SOURCE_DIR}/common/lwindex.h"
    "${PROJECT_SOURCE_DIR}/common/lwindex_sscanf_unrolled.h"
    "${PROJECT_SOURCE_DIR}/common/lwindex_utils.c"
    "${PROJECT_SOURCE_DIR}/common/lwindex_utils.h"
    "${PROJECT_SOURCE_DIR}/common/lwindex_parser.c"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_audio.c"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_audio.h"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_audio_internal.h"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_dec.c"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_dec.h"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_video.c"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_video.h"
    "${PROJECT_SOURCE_DIR}/common/lwlibav_video_internal.h"
    "${PROJECT_SOURCE_DIR}/common/osdep.c"
    "${PROJECT_SOURCE_DIR}/common/osdep.h"
    "${PROJECT_SOURCE_DIR}/common/resample.c"
    "${PROJECT_SOURCE_DIR}/common/resample.h"
    "${PROJECT_SOURCE_DIR}/common/qsv.c"
    "${PROJECT_SOURCE_DIR}/common/qsv.h"
    "${PROJECT_SOURCE_DIR}/common/utils.c"
    "${PROJECT_SOURCE_DIR}/common/utils.h"
    "${PROJECT_SOURCE_DIR}/common/video_output.c"
    "${PROJECT_SOURCE_DIR}/common/video_output.h"
)

if (WIN32)
    configure_file(
        "${PROJECT_SOURCE_DIR}/cli/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version_indexing.rc"
    )
    target_sources(LSMASHSource_indexing PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version_indexing.rc)
endif()

target_include_directories(LSMASHSource_indexing PRIVATE "${PROJECT_SOURCE_DIR}/common")

target_compile_definitions(LSMASHSource_indexing PRIVATE
    XXH_INLINE_ALL
    _FILE_OFFSET_BITS=64
)

if (WIN32)
    if (MINGW)
        target_compile_definitions(LSMASHSource_indexing PRIVATE __USE_MINGW_ANSI_STDIO)
    endif()
endif()

target_link_libraries(LSMASHSource_indexing PRIVATE
    FFMPEG::avcodec
    FFMPEG::avformat
    FFMPEG::swscale
    FFMPEG::swresample
    FFMPEG::avutil
    ZLIB::ZLIB
    xxHash::xxhash
)

if (ENABLE_DAV1D)
    target_link_libraries(LSMASHSource_indexing PRIVATE dav1d::dav1d)
endif()

if (ENABLE_MFX)
    target_link_libraries(LSMASHSource_indexing PRIVATE MFX::mfx)
endif()

if (ENABLE_XML2)
    target_link_libraries(LSMASHSource_indexing PRIVATE LibXml2::LibXml2)
endif()

if (ENABLE_VPX)
    target_link_libraries(LSMASHSource_indexing PRIVATE VPX::vpx)
endif()

if (ENABLE_VULKAN)
    target_link_libraries(LSMASHSource_indexing PRIVATE
        Vulkan::Vulkan
        Vulkan::shaderc_combined
    )
endif()

if (MINGW)
    target_link_libraries(LSMASHSource_indexing PRIVATE ws2_32)
endif()

if (WIN32)
    target_link_libraries(LSMASHSource_indexing PRIVATE bcrypt)
endif()
