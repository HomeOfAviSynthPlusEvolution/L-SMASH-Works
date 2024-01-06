cmake_minimum_required(VERSION 3.1)

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

option(ENABLE_XML2S "Enable DNXHD support" ON)
message(STATUS "Enable DNXHD support: ${ENABLE_XML2S}.")

set(sources
    ${CMAKE_CURRENT_SOURCE_DIR}/common/decode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_dec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/lwsimd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/osdep.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/qsv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/video_output.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/xxhash.c
)

cmake_host_system_information(RESULT sse2 QUERY HAS_SSE2)
if (${sse2})
    set(sources
        ${sources}
        ${CMAKE_CURRENT_SOURCE_DIR}/common/planar_yuv_sse2.c
    )
    message(STATUS "SSE2 enabled.")
else()
    message(STATUS "SSE2 disabled.")
endif()

if (BUILD_AVS_PLUGIN)
    set(sources
        ${sources}
        ${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/resample.c
        ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/audio_output.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/libavsmash_source.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lsmashsource.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lwlibav_source.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/video_output.cpp
    )
endif()

if (BUILD_VS_PLUGIN)
    set(sources
        ${sources}
        ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/libavsmash_source.c
        ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lsmashsource.c
        ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lwlibav_source.c
        ${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/video_output.c
    )
endif()

find_package(Git)

if (GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count origin/master
        OUTPUT_VARIABLE ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if (WIN32)
        configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        )

        set(sources ${sources} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    endif()
else()
    message(STATUS "GIT not found")
endif()

add_library(LSMASHSource SHARED ${sources})

target_include_directories(LSMASHSource PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (APPLE)
    target_include_directories(LSMASHSource PRIVATE /usr/local/include)
endif()

if (WIN32)
    find_library(bcrypt bcrypt)
    message(STATUS "bcrypt: ${bcrypt}")
    target_link_libraries(LSMASHSource PRIVATE ${bcrypt})
endif()

if (MSVC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "")
endif()

find_library(libavutil NAMES avutil libavutil.a)
message(STATUS "avutil: ${libavutil}")

find_library(libavcodec NAMES avcodec libavcodec.a)
message(STATUS "avcodec: ${libavcodec}")

find_library(libavformat NAMES avformat libavformat.a)
message(STATUS "avformat: ${libavformat}")

find_library(libswscale NAMES swscale libswscale.a)
message(STATUS "swscale: ${libswscale}")

find_library(libswresample NAMES swresample libswresample.a)
message(STATUS "swresample: ${libswresample}")

find_library(libzlib NAMES z zlib.lib)
message(STATUS "zlib: ${libzlib}")

find_library(libxxhash NAMES xxhash xxhash.lib)
message(STATUS "xxhash: ${libxxhash}")

find_library(libobuparse NAMES obuparse obuparse.lib)
message(STATUS "obuparse: ${libobuparse}")

find_library(liblsmash NAMES lsmash lsmash.lib)
message(STATUS "lsmash: ${liblsmash}")

target_link_libraries(LSMASHSource PRIVATE
    ${libavutil}
    ${libavcodec}
    ${libavformat}
    ${libswscale}
    ${libswresample}
    ${libzlib}
    ${libxxhash}
    ${libobuparse}
    ${liblsmash}
)

if (ENABLE_DAV1D)
    find_library(libdav1d NAMES dav1d libdav1d.a)
    message(STATUS "dav1d: ${libdav1d}")
    target_link_libraries(LSMASHSource PRIVATE ${libdav1d})
endif()

if (ENABLE_MFX)
    find_library(libmfx NAMES mfx libmfx.lib)
    message(STATUS "mfx: ${libmfx}")
    target_link_libraries(LSMASHSource PRIVATE ${libmfx})
endif()

if (ENABLE_XML2S)
    find_library(libxml2s NAMES xml2s libxml2s.lib)
    message(STATUS "xml2s: ${libxml2s}")
    target_link_libraries(LSMASHSource PRIVATE ${libxml2s})

    if (WIN32)
        target_compile_definitions(LSMASHSource PRIVATE "-DLIBXML_STATIC")
    endif()
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
