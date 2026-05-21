project('uacpi', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23', 'b_pie=false'])

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

sh = find_program('sh', required: true)
#cc = meson.get_compiler('cpp')

#tcmalloc_lib_dir = join_paths(meson.global_source_root(), 'libs', 'tcmalloc', 'out')

#tcmalloc_lib = cc.find_library('tcmalloc',
#                               dirs : tcmalloc_lib_dir,
#                               static : true
#)

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
                       dependencies: [ uacpi_dep ],
                        #dependencies: [ uacpi_dep, tcmalloc_lib ],
                       c_args: [ '-DUACPI_NATIVE_ALLOC_ZEROED', '-Og', '-g', '-ggdb3', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       cpp_args: [ '-DUACPI_NATIVE_ALLOC_ZEROED', '-Og', '-g', '-ggdb3', '-stdlib=libc++', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       link_args: [ '-stdlib=libc++', '--rtlib=compiler-rt', '--unwindlib=libunwind', '-static', '-lc++abi', '-Wl,--hash-style=gnu', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                       pie: false,
                       native: false
)