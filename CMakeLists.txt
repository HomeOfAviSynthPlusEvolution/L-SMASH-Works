cmake_minimum_required(VERSION 3.28)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
endif()

project(LSMASHSource VERSION 1266)

option(BUILD_AVS_PLUGIN "Build plugin for AviSynth" ON)
message(STATUS "Build plugin for AviSynth: ${BUILD_AVS_PLUGIN}.")

option(BUILD_VS_PLUGIN "Build plugin for VapourSynth" ON)
message(STATUS "Build plugin for VapourSynth: ${BUILD_VS_PLUGIN}.")

option(ENABLE_DAV1D "Enable dav1d AV1 decoding" ON)
message(STATUS "Enable dav1d AV1 decoding: ${ENABLE_DAV1D}.")
option(dav1d_USE_STATIC_LIBS "Look for static dav1d libraries" ON)
message(STATUS "Look for static dav1d libraries: ${dav1d_USE_STATIC_LIBS}.")

option(ENABLE_MFX "Enable Intel HW decoding" ON)
message(STATUS "Enable Intel HW decoding: ${ENABLE_MFX}.")

option(ENABLE_XML2 "Enable DNXHD support" ON)
message(STATUS "Enable DNXHD support: ${ENABLE_XML2}.")

option(ENABLE_VPX "Enable libvpx support" ON)
message(STATUS "Enable libvpx decoding: ${ENABLE_VPX}.")
option(VPX_USE_STATIC_LIBS "Look for static libvpx libraries" ON)
message(STATUS "Look for static libvpx libraries: ${VPX_USE_STATIC_LIBS}.")

cmake_host_system_information(RESULT sse2 QUERY HAS_SSE2)
option(ENABLE_SSE2 "Enable SSE2 support" ${sse2})
message(STATUS "Enable SSE2 support: ${ENABLE_SSE2}.")

option(BUILD_INDEXING_TOOL "Build index tool" OFF)
message(STATUS "Build index tool: ${BUILD_INDEXING_TOOL}.")

option(ENABLE_VULKAN "Enable Vulkan support" ON)
message(STATUS "Enable Vulkan decoding: ${ENABLE_VULKAN}.")

option(ZLIB_USE_STATIC_LIBS "Look for static zlib libraries" ON)
message(STATUS "Look for static zlib libraries: ${ZLIB_USE_STATIC_LIBS}.")

# In case we use old obuparse without cmake build system.
#option(OBUParse_USE_STATIC_LIBS "Look for static obuparse libraries" ON)
#message(STATUS "Look for static obuparse libraries: ${OBUParse_USE_STATIC_LIBS}.")

# In case we use old lsmash without cmake build system.
#option(LSMASH_USE_STATIC_LIBS "Look for static lsmash libraries" ON)
#message(STATUS "Look for static lsmash libraries: ${LSMASH_USE_STATIC_LIBS}.")

option(BUILD_SHARED_LIBS "Build shared dependencies libraries" OFF)
message(STATUS "Build shared dependencies libraries: ${BUILD_SHARED_LIBS}.")

# xxHash options
option(XXHASH_BUILD_XXHSUM "Build the xxhsum binary" OFF)
option(XXHASH_BUNDLED_MODE "" OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/xxHash/cmake_unofficial")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(FFMPEG REQUIRED COMPONENTS
    avcodec
    avformat
    swscale
    swresample
    avutil
)
find_package(ZLIB REQUIRED)

if (ENABLE_DAV1D)
    find_package(dav1d REQUIRED)
endif()

if (ENABLE_MFX)
    find_package(MFX REQUIRED)
endif()

if (ENABLE_XML2)
    find_package(LibXml2 REQUIRED)
endif()

if (ENABLE_VPX)
    find_package(VPX REQUIRED)
endif()

if (ENABLE_VULKAN)
    find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
endif()

if (WIN32)
    find_package(Git QUIET)

    if(GIT_FOUND)
        # Run git to get the revision count.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE ver
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE git_result
        )

        if(NOT git_result EQUAL 0)
            message(STATUS "Could not determine git revision. Not a git repository or git error.")
            set(ver "${PROJECT_VERSION}") # Set a default if git failed
        endif()
    else()
        message(STATUS "Git not found. Using default version '${PROJECT_VERSION}'.")
        set(ver "${PROJECT_VERSION}") # Set a default if git isn't installed
    endif()
endif()

if (BUILD_AVS_PLUGIN OR BUILD_VS_PLUGIN)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/obuparse")

    # l-smash options
    option(LSMASH_BUILD_TOOLS "Build the l-smash tools" OFF)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/l-smash")

    add_library(LSMASHSource MODULE
        "${CMAKE_CURRENT_SOURCE_DIR}/common/cpp_compat.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/decode.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/decode.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_audio_internal.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/libavsmash_video_internal.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_sscanf_unrolled.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_utils.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_utils.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_parser.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwindex_parser.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_audio_internal.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_dec.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_dec.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/lwlibav_video_internal.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/osdep.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/osdep.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/progress.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/qsv.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/qsv.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/utils.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/utils.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/video_output.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/video_output.h"
    )

    if (ENABLE_SSE2)
        target_sources(LSMASHSource PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/common/planar_yuv_sse2.c")
    endif()

    if (BUILD_AVS_PLUGIN)
        target_sources(LSMASHSource PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/common/audio_output.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/common/resample.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/common/resample.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/audio_output.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/audio_output.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/libavsmash_source.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/libavsmash_source.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lsmashsource.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lsmashsource.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lwlibav_source.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/lwlibav_source.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/video_output.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/AviSynth/video_output.h"
        )
    endif()

    if (BUILD_VS_PLUGIN)
        target_sources(LSMASHSource PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/libavsmash_source.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lsmashsource.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lsmashsource.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/lwlibav_source.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/video_output.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/VapourSynth/video_output.h"
        )
    endif()

    if (BUILD_VS_PLUGIN AND BUILD_AVS_PLUGIN)
        target_compile_definitions(LSMASHSource PRIVATE BUILD_SINGLE_BINARY)
    endif()

    target_include_directories(LSMASHSource PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

    if (APPLE)
        target_include_directories(LSMASHSource PRIVATE "/usr/local/include")
    endif()

    target_link_libraries(LSMASHSource PRIVATE
        FFMPEG::avcodec
        FFMPEG::avformat
        FFMPEG::swscale
        FFMPEG::swresample
        FFMPEG::avutil
        ZLIB::ZLIB
        xxHash::xxhash
    )

    if (BUILD_SHARED_LIBS)
        target_link_libraries(LSMASHSource PRIVATE
        OBUParse::shared
        LSMASH::shared)
    else()
        target_link_libraries(LSMASHSource PRIVATE
        OBUParse::static
        LSMASH::static)
    endif()

    if (ENABLE_DAV1D)
        target_link_libraries(LSMASHSource PRIVATE dav1d::dav1d)
    endif()

    if (ENABLE_MFX)
        target_link_libraries(LSMASHSource PRIVATE MFX::mfx)
    endif()

    if (ENABLE_XML2)
        target_link_libraries(LSMASHSource PRIVATE LibXml2::LibXml2)
    endif()

    if (ENABLE_VPX)
        target_link_libraries(LSMASHSource PRIVATE VPX::vpx)
    endif()

    if (ENABLE_VULKAN)
        target_link_libraries(LSMASHSource PRIVATE
            Vulkan::Vulkan
            Vulkan::shaderc_combined
        )
    endif()

    if (MINGW)
        target_link_libraries(LSMASHSource PRIVATE ws2_32)
    endif()

    if (WIN32)
        target_link_libraries(LSMASHSource PRIVATE bcrypt)
    endif()

    target_compile_features(LSMASHSource PRIVATE cxx_std_17)

    target_compile_definitions(LSMASHSource PRIVATE
        $<$<CONFIG:Debug>:DEBUG_BUILD>
        $<$<CONFIG:Release>:RELEASE_BUILD>
        $<$<CONFIG:RelWithDebInfo>:RELEASE_BUILD>
        $<$<CONFIG:MinSizeRel>:RELEASE_BUILD>
    )

    if (${sse2})
        target_compile_definitions(LSMASHSource PRIVATE SSE2_ENABLED=1)
    endif()

    if (WIN32)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        )
        target_sources(LSMASHSource PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
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
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/cli")
endif()
