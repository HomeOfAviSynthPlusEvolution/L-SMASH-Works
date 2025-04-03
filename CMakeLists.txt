cmake_minimum_required(VERSION 3.28)

if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    endif()
endif()

project(LSMASHSource)

option(BUILD_AVS_PLUGIN "Build plugin for AviSynth" ON)
message(STATUS "Build plugin for AviSynth: ${BUILD_AVS_PLUGIN}.")

option(BUILD_VS_PLUGIN "Build plugin for VapourSynth" ON)
message(STATUS "Build plugin for VapourSynth: ${BUILD_VS_PLUGIN}.")

option(ENABLE_DAV1D "Enable dav1d AV1 decoding" ON)
message(STATUS "Enable dav1d AV1 decoding: ${ENABLE_DAV1D}.")

option(ENABLE_MFX "Enable Intel HW decoding" ON)
message(STATUS "Enable Intel HW decoding: ${ENABLE_MFX}.")

option(ENABLE_XML2 "Enable DNXHD support" ON)
message(STATUS "Enable DNXHD support: ${ENABLE_XML2}.")

option(ENABLE_VPX "Enable libvpx support" ON)
message(STATUS "Enable libvpx decoding: ${ENABLE_VPX}.")

option(ENABLE_VULKAN "Enable Vulkan support" ON)
message(STATUS "Enable Vulkan decoding: ${ENABLE_VULKAN}.")

cmake_host_system_information(RESULT sse2 QUERY HAS_SSE2)
option(ENABLE_SSE2 "Enable SSE2 support" ${sse2})
message(STATUS "Enable SSE2 support: ${ENABLE_SSE2}.")

option(BUILD_INDEXING_TOOL "Build index tool" OFF)
message(STATUS "Build index tool: ${BUILD_INDEXING_TOOL}.")

find_package(Git)

if (GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count origin/master
        OUTPUT_VARIABLE ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    message(STATUS "GIT not found")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(FFMPEG REQUIRED COMPONENTS
    avcodec
    avformat
    swscale
    swresample
    avutil
)

find_package(PkgConfig)

set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)

if (ENABLE_DAV1D)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(dav1d dav1d)
    endif (PKG_CONFIG_FOUND)
    find_library(libdav1d NAMES libdav1d.a dav1d PATHS ${dav1d_LIBRARY_DIRS})
    message(STATUS "dav1d: ${libdav1d}")
endif()

if (ENABLE_MFX)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(mfx libmfx)
    endif (PKG_CONFIG_FOUND)
    find_library(libmfx NAMES libmfx.a mfx libmfx.lib PATHS ${mfx_LIBRARY_DIRS})
    message(STATUS "mfx: ${libmfx}")
endif()

if (ENABLE_XML2)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(xml2 libxml-2.0)
    endif (PKG_CONFIG_FOUND)
    find_library(libxml2 NAMES libxml2.a xml2 libxml2s.lib PATH ${xml2_LIBRARY_DIRS})
    message(STATUS "xml2: ${libxml2}")
endif()

if (ENABLE_VPX)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(vpx vpx)
    endif (PKG_CONFIG_FOUND)
    find_library(libvpx NAMES vpx vpx.lib vpxmd.lib PATHS ${vpx_LIBRARY_DIRS})
    message(STATUS "vpx: ${libvpx}")
endif()

if (ENABLE_VULKAN)
    find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
endif()

if (WIN32)
    find_library(bcrypt NAMES bcrypt bcrypt.lib)
    message(STATUS "bcrypt: ${bcrypt}")
endif()

if (BUILD_AVS_PLUGIN OR BUILD_VS_PLUGIN)
    set(sources
        ${CMAKE_CURRENT_SOURCE_DIR}/common/cpp_compat.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/decode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/decode.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_sscanf_unrolled.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_dec.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_dec.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/osdep.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/osdep.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/progress.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/qsv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/qsv.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/utils.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/video_output.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/video_output.h
        ${CMAKE_CURRENT_SOURCE_DIR}/common/xxhash.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/xxhash.h
    )

    if (ENABLE_SSE2)
        set(sources
            ${sources}
            ${CMAKE_CURRENT_SOURCE_DIR}/common/planar_yuv_sse2.c
        )
    endif()

    if (BUILD_AVS_PLUGIN)
        set(sources
            ${sources}
            ${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.c
            ${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.h
            ${CMAKE_CURRENT_SOURCE_DIR}/common/resample.c
            ${CMAKE_CURRENT_SOURCE_DIR}/common/resample.h
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/audio_output.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/audio_output.h
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/libavsmash_source.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/libavsmash_source.h
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lsmashsource.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lsmashsource.h
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lwlibav_source.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lwlibav_source.h
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/video_output.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/video_output.h
        )
    endif()

    if (BUILD_VS_PLUGIN)
        set(sources
            ${sources}
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/libavsmash_source.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lsmashsource.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lsmashsource.h
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lwlibav_source.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/video_output.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/video_output.h
        )
    endif()
    
    if (WIN32)
        configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        )
    
        set(sources ${sources} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    endif()

    add_library(LSMASHSource SHARED ${sources})

    target_include_directories(LSMASHSource PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ZLIB_INCLUDE_DIRS}
    )

    if (APPLE)
        target_include_directories(LSMASHSource PRIVATE /usr/local/include)
    endif()

    if (PKG_CONFIG_FOUND)
        pkg_check_modules(xxhash libxxhash)
        target_include_directories(LSMASHSource PRIVATE ${xxhash_INCLUDE_DIRS})
    endif (PKG_CONFIG_FOUND)
    find_library(libxxhash NAMES libxxhash.a xxhash xxhash.lib PATHS ${xxhash_LIBRARY_DIRS})
    message(STATUS "xxhash: ${libxxhash}")

    find_path(libopurse_include NAMES obuparse.h)
    target_include_directories(LSMASHSource PRIVATE ${libopurse_include})
    find_library(libobuparse NAMES libobuparse.a obuparse obuparse.lib)
    message(STATUS "obuparse: ${libobuparse}")

    if (PKG_CONFIG_FOUND)
        pkg_check_modules(lsmash liblsmash)
        target_include_directories(LSMASHSource PRIVATE ${lsmash_INCLUDE_DIRS})
    endif (PKG_CONFIG_FOUND)
    find_library(liblsmash NAMES liblsmash.a lsmash liblsmash.lib PATHS ${lsmash_LIBRARY_DIRS})
    message(STATUS "lsmash: ${liblsmash}")

    target_link_libraries(LSMASHSource PRIVATE
        FFMPEG::avcodec
        FFMPEG::avformat
        FFMPEG::swscale
        FFMPEG::swresample
        FFMPEG::avutil
        ZLIB::ZLIB
        ${libxxhash}
        ${liblsmash}
        ${libobuparse}
    )

    if (ENABLE_DAV1D)
        if (PKG_CONFIG_FOUND)
            target_include_directories(LSMASHSource PRIVATE ${dav1d_INCLUDE_DIRS})
        endif()
        target_link_libraries(LSMASHSource PRIVATE ${libdav1d})
    endif()

    if (ENABLE_MFX)
        if (PKG_CONFIG_FOUND)
            target_include_directories(LSMASHSource PRIVATE ${mfx_INCLUDE_DIRS})
        endif()
        target_link_libraries(LSMASHSource PRIVATE ${libmfx})
    endif()

    if (ENABLE_XML2)
        if (PKG_CONFIG_FOUND)
            target_include_directories(LSMASHSource PRIVATE ${xml2_INCLUDE_DIRS})
        endif()
        target_link_libraries(LSMASHSource PRIVATE ${libxml2})

        if (WIN32)
            target_compile_definitions(LSMASHSource PRIVATE "-DLIBXML_STATIC")
        endif()
    endif()

    if (ENABLE_VPX)
        if (PKG_CONFIG_FOUND)
            target_include_directories(LSMASHSource PRIVATE ${vpx_INCLUDE_DIRS})
        endif()
        target_link_libraries(LSMASHSource PRIVATE ${libvpx})
    endif()

    if (ENABLE_VULKAN)
        target_include_directories(LSMASHSource PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(LSMASHSource PRIVATE
            Vulkan::Vulkan
            Vulkan::shaderc_combined
        )
    endif()

    if (MINGW)
        target_link_libraries(LSMASHSource PRIVATE ws2_32)
    endif()

    if (WIN32)
        target_link_libraries(LSMASHSource PRIVATE ${bcrypt})
    endif()

    target_compile_features(LSMASHSource PRIVATE cxx_std_17)

    if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
        string(TOLOWER ${CMAKE_BUILD_TYPE} build_type)
        if (build_type STREQUAL debug)
            target_compile_definitions(LSMASHSource PRIVATE DEBUG_BUILD)
        else (build_type STREQUAL release)
            target_compile_definitions(LSMASHSource PRIVATE RELEASE_BUILD)
        endif()

        message(STATUS "Build type - ${CMAKE_BUILD_TYPE}")
    endif()

    if (${sse2})
        target_compile_definitions(LSMASHSource PRIVATE SSE2_ENABLED=1)
    endif()

    if (WIN32)
        set_target_properties(LSMASHSource PROPERTIES
            PREFIX ""
            OUTPUT_NAME "LSMASHSource"
        )
    else()
        if (GIT_FOUND)
            set_target_properties(LSMASHSource PROPERTIES OUTPUT_NAME "lsmashsource.${ver}")
        endif()

        include(GNUInstallDirs)

        if (BUILD_AVS_PLUGIN)
            INSTALL(TARGETS LSMASHSource LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/avisynth")
        endif()

        if (BUILD_VS_PLUGIN)
            INSTALL(TARGETS LSMASHSource LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/vapoursynth")
        endif()

        # uninstall target
        if(NOT TARGET uninstall)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
            IMMEDIATE @ONLY)

        add_custom_target(uninstall
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
        endif()
    endif()
endif()

if (BUILD_INDEXING_TOOL)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cli)
endif()
