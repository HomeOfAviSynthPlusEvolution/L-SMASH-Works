cmake_minimum_required(VERSION 3.28)

add_executable(LSMASHSource_indexing)

target_sources(LSMASHSource_indexing PRIVATE
    ${PROJECT_SOURCE_DIR}/cli/index.c
    ${PROJECT_SOURCE_DIR}/common/audio_output.c
    ${PROJECT_SOURCE_DIR}/common/audio_output.h
    ${PROJECT_SOURCE_DIR}/common/decode.c
    ${PROJECT_SOURCE_DIR}/common/decode.h
    ${PROJECT_SOURCE_DIR}/common/lwindex.c
    ${PROJECT_SOURCE_DIR}/common/lwindex.h
    ${PROJECT_SOURCE_DIR}/common/lwindex_sscanf_unrolled.h
    ${PROJECT_SOURCE_DIR}/common/lwlibav_audio.c
    ${PROJECT_SOURCE_DIR}/common/lwlibav_audio.h
    ${PROJECT_SOURCE_DIR}/common/lwlibav_audio_internal.h
    ${PROJECT_SOURCE_DIR}/common/lwlibav_dec.c
    ${PROJECT_SOURCE_DIR}/common/lwlibav_dec.h
    ${PROJECT_SOURCE_DIR}/common/lwlibav_video.c
    ${PROJECT_SOURCE_DIR}/common/lwlibav_video.h
    ${PROJECT_SOURCE_DIR}/common/lwlibav_video_internal.h
    ${PROJECT_SOURCE_DIR}/common/osdep.c
    ${PROJECT_SOURCE_DIR}/common/osdep.h
    ${PROJECT_SOURCE_DIR}/common/resample.c
    ${PROJECT_SOURCE_DIR}/common/resample.h
    ${PROJECT_SOURCE_DIR}/common/qsv.c
    ${PROJECT_SOURCE_DIR}/common/qsv.h
    ${PROJECT_SOURCE_DIR}/common/utils.c
    ${PROJECT_SOURCE_DIR}/common/utils.h
    ${PROJECT_SOURCE_DIR}/common/video_output.c
    ${PROJECT_SOURCE_DIR}/common/video_output.h
)

if (GIT_FOUND)
    if (WIN32)
        configure_file(
            "${PROJECT_SOURCE_DIR}/cli/version.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/version_indexing.rc"
        )
        target_sources(LSMASHSource_indexing PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version_indexing.rc)
    endif()
endif()

target_include_directories(LSMASHSource_indexing PRIVATE ${PROJECT_SOURCE_DIR}/common)

if (PKG_CONFIG_FOUND)
    target_include_directories(LSMASHSource_indexing PRIVATE ${zlib_INCLUDE_DIRS})
endif()

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
    ${libzlib}
)

if (ENABLE_DAV1D)
    if (PKG_CONFIG_FOUND)
        target_include_directories(LSMASHSource_indexing PRIVATE ${dav1d_INCLUDE_DIRS})
    endif()
    target_link_libraries(LSMASHSource_indexing PRIVATE ${libdav1d})
endif()

if (ENABLE_MFX)
    if (PKG_CONFIG_FOUND)
        target_include_directories(LSMASHSource_indexing PRIVATE ${mfx_INCLUDE_DIRS})
    endif()
    target_link_libraries(LSMASHSource_indexing PRIVATE ${libmfx})
endif()

if (ENABLE_XML2)
    if (PKG_CONFIG_FOUND)
        target_include_directories(LSMASHSource_indexing PRIVATE ${xml2_INCLUDE_DIRS})
    endif()
    target_link_libraries(LSMASHSource_indexing PRIVATE ${libxml2})
    if (WIN32)
        target_compile_definitions(LSMASHSource_indexing PRIVATE "-DLIBXML_STATIC")
    endif()
endif()

if (ENABLE_VPX)
    if (PKG_CONFIG_FOUND)
        target_include_directories(LSMASHSource_indexing PRIVATE ${vpx_INCLUDE_DIRS})
    endif()
    target_link_libraries(LSMASHSource_indexing PRIVATE ${libvpx})
endif()

if (MINGW)
    target_link_libraries(LSMASHSource_indexing PRIVATE ws2_32)
endif()

if (WIN32)
    target_link_libraries(LSMASHSource_indexing PRIVATE ${bcrypt})
endif()
