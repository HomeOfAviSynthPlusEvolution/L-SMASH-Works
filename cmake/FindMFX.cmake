# FindMFX.cmake - Finds the Intel Media SDK Dispatcher (libmfx)
#
# This module acts as a "smart dispatcher":
# 1. It first tries to find an official mfx-config.cmake file.
# 2. If that fails, it falls back to a manual search for headers and libraries.
#
# It always produces the imported target `MFX::mfx`.

include(FindPackageHandleStandardArgs)

# Guard to prevent this from running multiple times
if(TARGET MFX::mfx)
    return()
endif()

# --- Stage 1: Try to find the official CMake package files first ---
# We use find_package in CONFIG mode with NO_MODULE to prevent this Find
# script from calling itself in an infinite loop.
find_package(MFX CONFIG QUIET NO_MODULE)

# If the above command succeeded, it will have created the MFX::mfx target.
# If it already exists, our job is done.
if(TARGET MFX::mfx)
    message(STATUS "Found libmfx via official CMake package files.")
    # The official package should set MFX_FOUND. We don't need to do anything else.
    return()
endif()

# --- Stage 2: Official package not found, fall back to manual search ---
message(STATUS "Official libmfx CMake package not found. Falling back to manual search.")

find_path(MFX_INCLUDE_DIR
    NAMES mfx/mfxvideo.h
    PATH_SUFFIXES include
)
find_library(MFX_LIBRARY
    NAMES libmfx mfx
    PATH_SUFFIXES lib lib64
)

# Call find_package_handle_standard_args with the correct REQUIRED_VARS.
# This call will set MFX_FOUND to TRUE if both variables are found.
find_package_handle_standard_args(MFX
    FOUND_VAR MFX_FOUND
    REQUIRED_VARS MFX_LIBRARY MFX_INCLUDE_DIR
    FAIL_MESSAGE "Could not find the Intel Media SDK Dispatcher (libmfx) via manual search. Please specify its location with CMAKE_PREFIX_PATH."
)

if(MFX_FOUND)
    add_library(MFX::mfx STATIC IMPORTED)
    set_target_properties(MFX::mfx PROPERTIES
        IMPORTED_LOCATION "${MFX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MFX_INCLUDE_DIR}"
    )
    if(WIN32)
        target_link_libraries(MFX::mfx INTERFACE ole32 uuid)
    elseif(UNIX)
        target_link_libraries(MFX::mfx INTERFACE dl)
    endif()
endif()

mark_as_advanced(MFX_INCLUDE_DIR MFX_LIBRARY)
