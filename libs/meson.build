if get_option('debug_build')
    mlibc_buildtype = 'debugoptimized'
else
    mlibc_buildtype = 'release'
endif

llvm_libc_script = join_paths(meson.global_source_root(), 'deps', 'build_llvm_libc.sh')

run_target('setup_mlibc',
    command: [sh, '-c',
        'sh @0@ setup-repos @1@'.format(llvm_libc_script, get_option('llvm_branch'))
    ]
)

run_target('build_mlibc',
    command: [sh, '-c',
        'sh @0@ --skip-repo-check build-libc @1@ @2@ @3@'.format(llvm_libc_script, arch, get_option('llvm_branch'), mlibc_buildtype)
    ]
)