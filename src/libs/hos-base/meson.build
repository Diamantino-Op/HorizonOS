project('hos_base', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'])

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

sh = find_program('sh', required: true)

deps_proj = subproject('horizon_deps')

freestnd_c_hdrs_dep = deps_proj.get_variable('freestnd_c_hdrs_dep')
freestnd_cxx_hdrs_dep = deps_proj.get_variable('freestnd_cxx_hdrs_dep')

hos_base_inc = include_directories('src')

hos_base_src = run_command(sh, '-c', '@0@ @1@'.format(get_option('globber_path'), join_paths(meson.project_source_root(), 'src')), check: true).stdout().strip().split('\n')

hos_base_lib = static_library('hos_base',
                              hos_base_src,
                              include_directories: hos_base_inc,
                              dependencies: [
                                  freestnd_c_hdrs_dep,
                                  freestnd_cxx_hdrs_dep
                              ],
                              c_args: get_option('lib_c_flags'),
                              cpp_args: get_option('lib_cpp_flags'),
                              pic: true
)

hos_base_dep = declare_dependency(include_directories: hos_base_inc, link_with: hos_base_lib)