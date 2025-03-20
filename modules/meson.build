project('HorizonOS Modules', 'cpp',
        version : get_option('horizon_version'),
        subproject_dir: 'components',
        default_options : [
            'warning_level=3',
            'c_std=gnu11',
            'cpp_std=c++2a',
        ]
)

kernel_prj = subproject('kernel', default_options : ['horizon_arch=@0@'.format(horizon_arch), 'horizon_version=@0@'.format(horizon_version)])
libs_prj = subproject('libs', default_options : ['horizon_arch=@0@'.format(horizon_arch), 'horizon_version=@0@'.format(horizon_version)])