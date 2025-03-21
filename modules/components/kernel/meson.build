project('HorizonOS Kernel', 'cpp',
        version : '0.0.1',
        subproject_dir: 'arches',
        default_options : [
          'warning_level=3',
          'c_std=gnu11',
          'cpp_std=c++2a',
        ]
)

version = get_option('horizon_version')

horizon_arch = get_option('horizon_arch')

kernel_common_prj = subproject('kernel-common')
kernel_common_lib = kernel_common_prj.get_variable('kernel-common')

kernel_prj  = subproject('kernel-@0@'.format(horizon_arch))
kernel_bin = kernel_arch_proj.get_variable('kernel')

uacpi_subproject = subproject('uacpi')
uacpi_includes = uacpi_subproject.get_variable('includes')

kernel_common_lib.include_directories(uacpi_includes)
kernel_bin.include_directories(uacpi_includes)

linker_script_path = join_paths('..', '..', '..', 'res', horizon_arch, 'Linker.ld')
kernel_bin.link_args('-T' + linker_script_path)