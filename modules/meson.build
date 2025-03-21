project('HorizonOS Modules', 'cpp',
        version : '0.0.1',
        subproject_dir: 'components',
        default_options : [
            'warning_level=3',
            'c_std=gnu11',
            'cpp_std=c++2a',
        ]
)

version = get_option('horizon_version')

horizon_arch = get_option('horizon_arch')

kernel_prj = subproject('kernel', default_options : ['horizon_arch=@0@'.format(horizon_arch)])
libs_prj = subproject('libs', default_options : ['horizon_arch=@0@'.format(horizon_arch)])