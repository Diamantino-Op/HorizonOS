project('kernel_common', 'cpp',
        version : '0.0.1',
        default_options : [
            'warning_level=3',
            'c_std=gnu11',
            'cpp_std=c++2a',
        ]
)

kernel_common_src = run_command(file_grabber_path, meson.source_root(), check: true).stdout().strip().split('\n')

kernel_common = static_library('kernel_common',
                               kernel_common_src,
                               uacpi.get_variable('sources'),
                               include_directories : include_directories('src'),
                               link_language : 'cpp',
)