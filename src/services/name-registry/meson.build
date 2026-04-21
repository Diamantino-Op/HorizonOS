project('name_registry', 'c', default_options: ['warning_level=2', 'c_std=gnu2x', 'b_pie=false'])

add_languages('c', native: true, required: true)
add_languages('c', native: false, required: true)

sh = find_program('sh', required: true)

name_registry_inc = include_directories('src')

sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot')
sysroot_inc_path = include_directories('../../../libs/sysroot/usr/include')

name_registry_src = run_command(sh, '-c', '@0@ @1@ *.c'.format(get_option('globber_path'), join_paths(meson.project_source_root(), 'src')), check: true).stdout().strip().split('\n')

name_registry_exe = executable('name_registry',
                               name_registry_src,
                               include_directories: [name_registry_inc, sysroot_inc_path],
                               c_args: [ '-Og', '-g', '-ggdb3', '--rtlib=compiler-rt', '--unwindlib=libunwind', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                               link_args: [ '--rtlib=compiler-rt', '--unwindlib=libunwind', '-static', '--target=@0@-horizonos-elf'.format(get_option('arch')) ],
                               pie: false,
                               native: false
)