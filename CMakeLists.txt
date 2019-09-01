cmake_minimum_required (VERSION 3.1)
project (LSMASHSource)
file(GLOB AVSSOURCES "AviSynth/*.cpp")
file(GLOB VPYSOURCES "VapourSynth/*.c")
file(GLOB LSWSOURCES "common/*.c")
set_source_files_properties(${VPYSOURCES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${LSWSOURCES} PROPERTIES LANGUAGE CXX)

add_library(LSMASHSource SHARED ${AVSSOURCES} ${VPYSOURCES} ${LSWSOURCES} version.rc)
set_target_properties(LSMASHSource PROPERTIES LINKER_LANGUAGE CXX)

set (VERSION "r935+34")
configure_file (
  "${PROJECT_SOURCE_DIR}/version.rc.in"
  "${PROJECT_SOURCE_DIR}/version.rc"
)
include_directories(include)
include_directories("${PROJECT_SOURCE_DIR}/vendor/include")
target_link_libraries(LSMASHSource 
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/liblsmash.lib
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/zlib.lib
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/libavcodec.a
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/libavformat.a
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/libavutil.a
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/libswscale.a
  ${PROJECT_SOURCE_DIR}/vendor/lib-${_DIR}/libswresample.a
  bcrypt.lib
)
add_definitions("-DBUILD_SINGLE_BINARY")
if (MSVC)
  add_definitions("/utf-8")
  add_definitions("/GL")
endif()

add_custom_command(
  TARGET LSMASHSource POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LSMASHSource> "../Release_${VERSION}/${_DIR}/$<TARGET_FILE_NAME:LSMASHSource>"
)
