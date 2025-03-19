project('hos-base', 'cpp',
        version : '0.0.1',
        default_options : [
            'warning_level=3',
            'c_std=gnu11',
            'cpp_std=c++2a',
        ]
)

hos_base_src = run_command(file_grabber_path, meson.source_root(), check: true).stdout().strip().split('\n')

hos_base = executable('hos-base', hos_base_src,
                    include_directories : include_directories('src'),
                    link_language : 'cpp',
)