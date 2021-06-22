project('L-SMASH-Works', 'c',
  default_options: ['buildtype=release', 'b_ndebug=if-release', 'c_std=c99'],
  meson_version: '>=0.48.0'
)

version_h = declare_dependency(
    sources: vcs_tag(
        command: ['git', 'describe', '--tags', '--long'],
        input: 'version.h.in',
        output: 'version.h'
    )
)

add_project_arguments('-DXXH_INLINE_ALL', '-D_FILE_OFFSET_BITS=64', '-DDEFAULT_CACHEDIR=' + get_option('cachedir'), language: 'c')

sources = [
  'libavsmash_source.c',
  'lsmashsource.c',
  'lsmashsource.h',
  'lwlibav_source.c',
  'video_output.c',
  'video_output.h',
  '../common/decode.c',
  '../common/decode.h',
  '../common/libavsmash.c',
  '../common/libavsmash.h',
  '../common/libavsmash_video.c',
  '../common/libavsmash_video.h',
  '../common/lwindex.c',
  '../common/lwindex.h',
  '../common/lwlibav_audio.c',
  '../common/lwlibav_audio.h',
  '../common/lwlibav_dec.c',
  '../common/lwlibav_dec.h',
  '../common/lwlibav_video.c',
  '../common/lwlibav_video.h',
  '../common/osdep.c',
  '../common/osdep.h',
  '../common/qsv.c',
  '../common/qsv.h',
  '../common/utils.c',
  '../common/utils.h',
  '../common/video_output.c',
  '../common/video_output.h'
]

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args: true, includes: true)

deps = [
  vapoursynth_dep,
  dependency('liblsmash'),
  dependency('libavcodec', version: '>=58.91.0'),
  dependency('libavformat', version: '>=58.45.0'),
  dependency('libavutil', version: '>=56.51.0'),
  dependency('libswscale', version: '>=5.7.0'),
  version_h
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

shared_module('vslsmashsource', sources,
  dependencies: deps,
  install: true,
  install_dir: join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth'),
  gnu_symbol_visibility: 'hidden'
)
