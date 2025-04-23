project('L-SMASH-Works', 'c',
  default_options: ['buildtype=release', 'b_ndebug=if-release', 'c_std=c99'],
  meson_version: '>=0.48.0'
)

add_project_arguments('-Wno-deprecated-declarations', '-DXXH_INLINE_ALL', '-D_FILE_OFFSET_BITS=64', language: 'c')

incdir = include_directories('../common')

sources = [
  'index.c',
  '../common/audio_output.c',
  '../common/audio_output.h',
  '../common/decode.c',
  '../common/decode.h',
  '../common/lwindex.c',
  '../common/lwindex.h',
  '../common/lwindex_sscanf_unrolled.h',
  '../common/lwindex_utils.c',
  '../common/lwindex_utils.h',
  '../common/lwindex_parser.c',
  '../common/lwlibav_audio.c',
  '../common/lwlibav_audio.h',
  '../common/lwlibav_dec.c',
  '../common/lwlibav_dec.h',
  '../common/lwlibav_video.c',
  '../common/lwlibav_video.h',
  '../common/osdep.c',
  '../common/osdep.h',
  '../common/resample.c',
  '../common/resample.h',
  '../common/qsv.c',
  '../common/qsv.h',
  '../common/utils.c',
  '../common/utils.h',
  '../common/video_output.c',
  '../common/video_output.h'
]

deps = [
  dependency('libavcodec', version: '>=58.91.0'),
  dependency('libavformat', version: '>=58.45.0'),
  dependency('libavutil', version: '>=56.51.0'),
  dependency('libswscale', version: '>=5.7.0')
]

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language: 'c')
endif

if host_machine.system() == 'windows'
  add_project_arguments('-D__USE_MINGW_ANSI_STDIO', language: 'c')
  if host_machine.cpu_family() == 'x86'
    add_project_arguments('-static-libgcc', language: 'c')
    add_global_link_arguments('-static-libgcc', language: 'c')
    add_global_link_arguments('-Wl,--add-stdcall-alias', language: 'c')
  endif
endif

executable('lwindex', sources,
  include_directories: incdir,
  dependencies: deps,
  gnu_symbol_visibility: 'hidden'
)
