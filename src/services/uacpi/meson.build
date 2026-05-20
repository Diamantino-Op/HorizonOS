project('uacpi', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23', 'b_pie=false'])

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

sh = find_program('sh', required: true)

bazel = find_program('bazel', required: true)

tcmalloc_repo = join_paths(meson.project_source_root(), '..', '..', '..', 'libs', 'tcmalloc')
tcmalloc_bazel_label = '//tcmalloc:libtcmalloc'
tcmalloc_bazel_artifact = 'bazel-bin/tcmalloc/libtcmalloc.a'

clang = find_program('/mnt/d/HorizonOS/toolchain/bin/clang', required: true)
clangxx = find_program('/mnt/d/HorizonOS/toolchain/bin/clang++', required: true)
llvm_ar = find_program('/mnt/d/HorizonOS/toolchain/bin/llvm-ar', required: true)

bazel_env = environment()
bazel_env.set('CC', clang.full_path())
bazel_env.set('CXX', clangxx.full_path())
bazel_env.set('AR', llvm_ar.full_path())
bazel_env.set('CXXFLAGS', '-stdlib=libc++ -std=c++23')
bazel_env.set('LDFLAGS', '-stdlib=libc++ --rtlib=compiler-rt --unwindlib=libunwind -static -lc++abi')

tcmalloc_archive = custom_target(
    'tcmalloc_bazel_build',
    output: 'libtcmalloc.a',
    command: [
        sh, '-c',
        'cd "$1" && "$2" build --compilation_mode=opt "$3" && cp "$4" "$5"',
        'meson-bazel',
        tcmalloc_repo,
        bazel,
        tcmalloc_bazel_label,
        tcmalloc_bazel_artifact,
        '@OUTPUT@',
    ],
    env: bazel_env,
    build_by_default: true,
    console: true,
)

tcmalloc_inc = include_directories('../../../libs/tcmalloc')

tcmalloc_dep = declare_dependency(
    include_directories: [tcmalloc_inc],
    sources: [tcmalloc_archive],
    link_args: [tcmalloc_archive.full_path()],
)

deps_proj = subproject('horizon_deps',
                       default_options: [
                           'globber_path=@0@'.format(get_option('globber_path'))
                       ]
)

uacpi_dep = deps_proj.get_variable('uacpi_dep')

uacpi_inc = include_directories('src')

sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot')
sysroot_inc_path = include_directories('../../../libs/sysroot/usr/include')

uacpi_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(get_option('globber_path'), join_paths(meson.project_source_root(), 'src')), check: true).stdout().strip().split('\n')

uacpi_exe = executable('uacpi',
                       uacpi_src,
                       include_directories: [ uacpi_inc, sysroot_inc_path ],
                       dependencies: [ uacpi_dep, tcmalloc_dep ],
                       c_args: [ '-DUACPI_NATIVE_ALLOC_ZEROED', '-Og', '-g', '-ggdb3', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       cpp_args: [ '-DUACPI_NATIVE_ALLOC_ZEROED', '-Og', '-g', '-ggdb3', '-stdlib=libc++', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       link_args: [ '-stdlib=libc++', '--rtlib=compiler-rt', '--unwindlib=libunwind', '-static', '-lc++abi', '-Wl,--hash-style=gnu', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       pie: false,
                       native: false
)