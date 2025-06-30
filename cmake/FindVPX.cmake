# FindVPX.cmake - Finds the libvpx library
#
# This module finds the libvpx library and creates the imported target `VPX::vpx`.
# It respects the `VPX_USE_STATIC_LIBS` variable to prefer static or shared libs.
#
# It first attempts to use pkg-config. If that fails or is unavailable, it falls
# back to manual path searching, which respects CMAKE_PREFIX_PATH.

include(FindPackageHandleStandardArgs)

# Guard to prevent this from running multiple times
if(TARGET VPX::vpx)
    return()
endif()

# 1. Try to find via pkg-config (standard for Linux/macOS)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    if(VPX_USE_STATIC_LIBS)
        # The --static flag tells pkg-config to return static linking information
        pkg_check_modules(PC_VPX QUIET vpx --static)
    else()
        pkg_check_modules(PC_VPX QUIET vpx)
    endif()
endif()

# 2. Find the header and library files
find_path(VPX_INCLUDE_DIR
    NAMES vpx/vpx_decoder.h
    HINTS ${PC_VPX_INCLUDEDIR}
    PATH_SUFFIXES include
)

# Temporarily modify the library search suffixes to respect user's preference
set(_VPX_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
if(VPX_USE_STATIC_LIBS)
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a)
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
else()
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib) # .lib can be an import lib for a .dll
    else()
        # Default suffixes already prefer shared libs (.so, .dylib)
    endif()
endif()

find_library(VPX_LIBRARY
    NAMES vpx libvpx vpxmd vpxmt
    HINTS ${PC_VPX_LIBDIR}
    PATH_SUFFIXES lib lib64
)

# Restore the original suffixes so we don't break other find calls
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_VPX_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES}")
unset(_VPX_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)

# 3. Use the standard tool to set VPX_FOUND and handle REQUIRED/QUIET
find_package_handle_standard_args(VPX
    FOUND_VAR VPX_FOUND
    REQUIRED_VARS VPX_LIBRARY VPX_INCLUDE_DIR
    VERSION_VAR PC_VPX_VERSION
)

# 4. If found, create the modern imported target
if(VPX_FOUND AND NOT TARGET VPX::vpx)
    # Determine if the library is static or shared
    get_filename_component(LIB_EXT ${VPX_LIBRARY} EXT)
    if(LIB_EXT STREQUAL ".a" OR (WIN32 AND LIB_EXT STREQUAL ".lib" AND VPX_USE_STATIC_LIBS))
        add_library(VPX::vpx STATIC IMPORTED)
    else()
        add_library(VPX::vpx SHARED IMPORTED)
    endif()

    set_target_properties(VPX::vpx PROPERTIES
        IMPORTED_LOCATION "${VPX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${VPX_INCLUDE_DIR}"
    )

    # libvpx requires linking to Threads
    find_package(Threads QUIET)
    if(Threads_FOUND)
        target_link_libraries(VPX::vpx INTERFACE Threads::Threads)
    endif()
endif()

# 5. Hide the internal variables from the user in the GUI
mark_as_advanced(VPX_INCLUDE_DIR VPX_LIBRARY)
