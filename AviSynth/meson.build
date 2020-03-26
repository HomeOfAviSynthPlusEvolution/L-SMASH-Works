project('L-SMASH-Works', 'c', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++14'],
  meson_version : '>=0.48.0'
)

add_project_arguments('-Wno-sign-compare', '-ffast-math', '-DXXH_INLINE_ALL', '-D_FILE_OFFSET_BITS=64', language : ['c', 'cpp'])

sources = [
  'audio_output.cpp',
  'audio_output.h',
  'libavsmash_source.cpp',
  'libavsmash_source.h',
  'lsmashsource.cpp',
  'lsmashsource.h',
  'lwlibav_source.cpp',
  'lwlibav_source.h',
  'video_output.cpp',
  'video_output.h',
  '../common/audio_output.c',
  '../common/audio_output.h',
  '../common/cpp_compat.h',
  '../common/decode.c',
  '../common/decode.h',
  '../common/libavsmash.c',
  '../common/libavsmash.h',
  '../common/libavsmash_audio.c',
  '../common/libavsmash_audio.h',
  '../common/libavsmash_audio_internal.h',
  '../common/libavsmash_video.c',
  '../common/libavsmash_video.h',
  '../common/libavsmash_video_internal.h',
  '../common/lwindex.c',
  '../common/lwindex.h',
  '../common/lwlibav_audio.c',
  '../common/lwlibav_audio.h',
  '../common/lwlibav_audio_internal.h',
  '../common/lwlibav_dec.c',
  '../common/lwlibav_dec.h',
  '../common/lwlibav_video.c',
  '../common/lwlibav_video.h',
  '../common/lwlibav_video_internal.h',
  '../common/lwsimd.c',
  '../common/lwsimd.h',
  '../common/osdep.c',
  '../common/osdep.h',
  '../common/progress.h',
  '../common/qsv.c',
  '../common/qsv.h',
  '../common/resample.c',
  '../common/resample.h',
  '../common/utils.c',
  '../common/utils.h',
  '../common/video_output.c',
  '../common/video_output.h'
]

avisynth_dep = dependency('avisynth').partial_dependency(compile_args : true, includes : true)

deps = [
  avisynth_dep,
  dependency('liblsmash'),
  dependency('libavcodec', version : '>=58.18.0'),
  dependency('libavformat', version : '>=58.12.0'),
  dependency('libavutil', version : '>=56.14.0'),
  dependency('libswresample', version : '>=3.1.0'),
  dependency('libswscale', version : '>=5.1.0')
]

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language : ['c', 'cpp'])
endif

if host_machine.system() == 'windows'
  add_project_arguments('-D__USE_MINGW_ANSI_STDIO', language : ['c', 'cpp'])
endif

shared_module('lsmashsource', sources,
  dependencies : deps,
  install : true,
  install_dir : join_paths(avisynth_dep.get_pkgconfig_variable('libdir'), 'avisynth'),
  gnu_symbol_visibility : 'hidden'
)
