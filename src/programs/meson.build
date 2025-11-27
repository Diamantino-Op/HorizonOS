# Programs Path

hos_programs_path = join_paths(meson.global_source_root(), 'src', 'programs')



# Hello World

#hello_world_proj = subproject('hello_world', default_options: [ 'globber_path=@0@'.format(globber) ])

#hello_world_exe = hello_world_proj.get_variable('hello_world_exe')

hello_world_src_path = join_paths(hos_programs_path, 'hello-world', 'src')

hello_world_inc = include_directories(join_paths('hello-world', 'src'))

hello_world_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(globber, hello_world_src_path), check: true).stdout().strip().split('\n')

hello_world_exe = executable('hello_world',
                             hello_world_src,
                             include_directories: hello_world_inc,
                             dependencies: [
                                 mlibc_static_dep
                             ],
                             c_args: [ '-Og', '-g', '-ggdb3' ],
                             cpp_args: [ '-Og', '-g', '-ggdb3' ],
                             pie: true,
                             native: false
)