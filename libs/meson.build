linux_headers_path_mlibc = join_paths(linux_headers_path, 'include')
sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot')
common_libs_path = join_paths(sysroot_path, 'usr', 'lib')
common_includes_path = join_paths(sysroot_path, 'usr', 'include')
res_file_path = join_paths(meson.global_source_root(), 'res', arch, 'horizon-mlibc-cross.cfg')

if get_option('debug_build')
    mlibc_buildtype = 'debugoptimized'
else
    mlibc_buildtype = 'release'
endif

mlibc_path = join_paths(meson.global_source_root(), 'libs', 'mlibc')

run_target('setup_mlibc',
    command: [sh, '-c',
        'git clone --single-branch --depth 1 -b horizonos-mlibc https://github.com/Diamantino-Op/mlibc.git ' + mlibc_path
    ]
)

run_target('build_mlibc',
    command: [sh, '-c',
        'cd ' + mlibc_path +
        ' && ' +
        ' rm -rf build' +
        ' && ' +
        'meson setup --reconfigure build' +
        ' --cross-file ' + res_file_path +
        ' --prefix ' + common_libs_path +
        ' --libdir ' + common_libs_path +
        ' --includedir ' + common_includes_path +
        ' -D default_library=both' +
        ' --buildtype ' + mlibc_buildtype +
        ' -D linux_kernel_headers=' + linux_headers_path_mlibc +
        ' && ' +
        'cd build' +
        ' && ' +
        'meson compile' +
        ' && ' +
        'meson install' +
        ' && ' +
        ' cd .. ' +
        ' && ' +
        ' rm -rf build'
    ]
)