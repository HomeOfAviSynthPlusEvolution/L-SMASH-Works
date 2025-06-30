# Finddav1d.cmake - Finds the dav1d library
#
# This module finds the dav1d library and creates the imported target `dav1d::dav1d`.
#
# It first attempts to use pkg-config. If that fails, it falls back to manual
# path searching, which respects CMAKE_PREFIX_PATH.
# It respects the `dav1d_USE_STATIC_LIBS` variable to prefer static or shared libs.

include(FindPackageHandleStandardArgs)

# Guard to prevent this from running multiple times
if(TARGET dav1d::dav1d)
    return()
endif()

# 1. Try to find via pkg-config (standard for Linux/macOS)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    if(dav1d_USE_STATIC_LIBS)
        pkg_check_modules(PC_DAV1D QUIET dav1d --static)
    else()
        pkg_check_modules(PC_DAV1D QUIET dav1d)
    endif()
endif()

# 2. Find the header and library files
find_path(dav1d_INCLUDE_DIR
    NAMES dav1d/dav1d.h
    HINTS ${PC_DAV1D_INCLUDEDIR}
    PATH_SUFFIXES include
)

set(_dav1d_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")

if(dav1d_USE_STATIC_LIBS)
    # On Windows, static libs are .lib. On Unix, they are .a.
    # We put these at the front of the search list.
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a)
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
else()
    # On Windows, shared libs are .dll (found via .lib import lib).
    # On Unix, they are .so, .dylib, etc.
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib) # .lib can be an import lib
    else()
        # The default suffixes already prefer shared libs (.so, .dylib)
        # so we don't need to do much here.
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

find_library(dav1d_LIBRARY
    NAMES dav1d libdav1d
    HINTS ${PC_DAV1D_LIBDIR}
    PATH_SUFFIXES lib lib64
)

# Restore the original suffixes so we don't break other find calls
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_dav1d_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES}")
unset(_dav1d_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)

# 3. Use the standard tool to set dav1d_FOUND and handle REQUIRED/QUIET
find_package_handle_standard_args(dav1d
    FOUND_VAR dav1d_FOUND
    REQUIRED_VARS dav1d_LIBRARY dav1d_INCLUDE_DIR
    VERSION_VAR PC_DAV1D_VERSION # Use version from pkg-config if available
)

# 4. If found, create the modern imported target
if(dav1d_FOUND AND NOT TARGET dav1d::dav1d)
    # Determine if the library is static or shared
    get_filename_component(LIB_EXT ${dav1d_LIBRARY} EXT)
    if(LIB_EXT STREQUAL ".a" OR (WIN32 AND LIB_EXT STREQUAL ".lib" AND dav1d_USE_STATIC_LIBS))
        # On Windows, a .lib could be static or an import lib.
        # We use the user's preference as a strong hint.
        add_library(dav1d::dav1d STATIC IMPORTED)
    else()
        add_library(dav1d::dav1d SHARED IMPORTED)
    endif()

    # Set the include directory usage requirement for the target
    set_target_properties(dav1d::dav1d PROPERTIES
        IMPORTED_LOCATION "${dav1d_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${dav1d_INCLUDE_DIR}"
    )

    # If pkg-config found other flags, add them.
    if(PC_DAV1D_CFLAGS_OTHER)
        target_compile_options(dav1d::dav1d INTERFACE ${PC_DAV1D_CFLAGS_OTHER})
    endif()
    if(PC_DAV1D_LDFLAGS_OTHER)
        target_link_options(dav1d::dav1d INTERFACE ${PC_DAV1D_LDFLAGS_OTHER})
    endif()
endif()

# 5. Hide the internal variables from the user in the GUI
mark_as_advanced(dav1d_INCLUDE_DIR dav1d_LIBRARY)
