cmake_minimum_required (VERSION 3.1)

project (LSMASHSource)

option(BUILD_AVS_PLUGIN "Build plugin for AviSynth" ON)
option(BUILD_VS_PLUGIN "Build plugin for VapourSynth" ON)
option(ENABLE_DAV1D "Enable dav1d AV1 decoding" ON)
option(ENABLE_MFX "Enable Intel HW decoding" ON)
option(ENABLE_XML2S "Enable DNXHD support" ON)

set(sources
    ${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/common/resample.c
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
endif()

if (BUILD_AVS_PLUGIN)
    set(sources
        ${sources}
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

find_package (Git)

if (GIT_FOUND)
    execute_process (COMMAND ${GIT_EXECUTABLE} rev-list --count upstream/master
        OUTPUT_VARIABLE ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if (WIN32)
        configure_file (
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        )

        set(sources ${sources} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    endif()
else()
    message (STATUS "GIT not found")
endif()

add_library (LSMASHSource SHARED ${sources})

target_include_directories(LSMASHSource PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (MSVC)
    target_link_libraries (LSMASHSource PRIVATE bcrypt.lib)
endif()

if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    endif()

    string(TOLOWER ${CMAKE_BUILD_TYPE} build_type)
    if (build_type STREQUAL debug)
        target_compile_definitions(LSMASHSource PRIVATE DEBUG_BUILD)
    else (build_type STREQUAL release)
        target_compile_definitions(LSMASHSource PRIVATE RELEASE_BUILD)
    endif()

    message(STATUS "Build type - ${CMAKE_BUILD_TYPE}")
endif()

target_link_libraries(LSMASHSource PRIVATE
    avutil
    avcodec
    avformat
    swscale
    swresample
    zlib
    xxhash
    obuparse
    lsmash
)

if (ENABLE_DAV1D)
    target_link_libraries(LSMASHSource PRIVATE dav1d)
endif()

if (ENABLE_MFX)
    target_link_libraries(LSMASHSource PRIVATE mfx)
endif()

if (ENABLE_XML2S)
    target_link_libraries(LSMASHSource PRIVATE xml2s)

    if (WIN32)
        target_compile_definitions(LSMASHSource PRIVATE "-DLIBXML_STATIC")
    endif()
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
