# Programs Path

hos_programs_path = join_paths(meson.global_source_root(), 'src', 'programs')



# Hello World

hello_world_proj = subproject('hello_world', default_options: [ 'globber_path=@0@'.format(globber) ])

hello_world_exe = hello_world_proj.get_variable('hello_world_exe')