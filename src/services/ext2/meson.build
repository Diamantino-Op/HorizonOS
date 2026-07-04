project('ext2', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23', 'b_pie=false'])

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

sh = find_program('sh', required: true)

ext2_inc = include_directories('src')

sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot')
sysroot_inc_path = include_directories('../../../libs/sysroot/usr/include')

ext2_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(get_option('globber_path'), join_paths(meson.project_source_root(), 'src')), check: true).stdout().strip().split('\n')

ext2_exe = executable('ext2',
                      ext2_src,
                                 include_directories: [ ext2_inc, sysroot_inc_path ],
                                 c_args: [ '-Og', '-g', '-ggdb3', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                                 cpp_args: [ '-Og', '-g', '-ggdb3', '-stdlib=libc++', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                                 link_args: [ '-stdlib=libc++', '--rtlib=compiler-rt', '--unwindlib=libunwind', '-static', '-lc++abi', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                                 pie: false,
                                 native: false
)