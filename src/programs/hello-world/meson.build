project('hello_world', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'])

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

sh = find_program('sh', required: true)

# MLibC Start

mlibc_proj = subproject('mlibc')

static_mlibc = mlibc_proj.get_variable('libc_static')
mlibc_include_dirs = mlibc_proj.get_variable('libc_include_dirs')

mlibc_dep = declare_dependency(include_directories: mlibc_include_dirs, link_with: static_mlibc)

# MLibC End

hello_world_inc = include_directories('src')

hello_world_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(get_option('globber_path'), join_paths(meson.project_source_root(), 'src')), check: true).stdout().strip().split('\n')

hello_world_exe = executable('hello_world',
                             hello_world_src,
                             include_directories: hello_world_inc,
                             dependencies: [
                                 mlibc_dep
                             ],
                             c_args: [ '-Og', '-g', '-ggdb3' ],
                             cpp_args: [ '-Og', '-g', '-ggdb3' ],
                             pie: true,
                             native: false
)