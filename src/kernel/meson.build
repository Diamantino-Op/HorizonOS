# Kernel Flags

kernel_linker_script = join_paths(res_path, arch, 'Linker.ld')

kernel_c_flags = [
    '-DLIMINE_API_REVISION=4',
    '-pipe',
    '-nostdinc',
    '-static',
    '-masm=intel',
    '--target=@0@-none-elf'.format(arch),
    '-ffreestanding',
    '-ffunction-sections',
    '-fdata-sections',
    '-fno-exceptions',
    '-fno-rtti',
    '-fno-omit-frame-pointer',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-Wno-enum-enum-conversion',
    '-Wno-unused-function',
    '-Wno-error=constant-logical-operand',
    '-Wno-error=tautological-undefined-compare',
    '-Wno-error=unused-variable',
    '-Wno-error=unused-function',
    '-Wno-error=unused-parameter',
    '-Wno-error=unused-private-field',
    '-Wno-error=cast-function-type-mismatch',
    '-Wno-error=deprecated-enum-enum-conversion',
    '-Wno-unused-command-line-argument'
]

kernel_cpp_flags = []

kernel_link_flags = [
    '-T@0@'.format(kernel_linker_script),
    '-static',
    '-nostdlib',
    '-Wl,-gc-sections',
    '-Wl,--ignore-data-address-equality',
    '-Wl,--ignore-function-address-equality',
    '-Wl,--icf=all',
]

if arch == 'x86_64'
    kernel_c_flags += [
        '-mno-red-zone',
        '-mno-sse',
        '-mno-sse2',
        '-mno-mmx',
        '-mno-80387',
        '-m64',
        '-march=x86-64',
    ]

    kernel_cpp_flags += []
elif arch == 'riscv64'
    kernel_c_flags += []

    kernel_cpp_flags += []
elif arch == 'aarch64'
    kernel_c_flags += []

    kernel_cpp_flags += []
endif

if get_option('debug_build')
    kernel_c_flags += [
        '-Og',
        '-g',
        '-ggdb3',
        '-fsanitize=undefined',
        '-DHORIZON_DEBUG',
    ]
else
    kernel_c_flags += [
        '-O3',
        '-ffast-math',
    ]
endif

kernel_cpp_flags += kernel_c_flags



# Kernel Common

kernel_common_src_dir = join_paths('.', 'kernel-common', 'src')

kernel_common_inc = include_directories(kernel_common_src_dir)

kernel_common_src = run_command(sh, '-c', '@0@ @1@'.format(globber, kernel_common_src_dir), check: true).stdout().strip().split('\n')

kernel_common_dep = declare_dependency(sources: kernel_common_src, include_directories: kernel_common_inc)



# Kernel Architecture

kernel_src_dir = join_paths('.', 'kernel-@0@'.format(arch), 'src')

kernel_inc = include_directories(kernel_src_dir)

kernel_src = run_command(sh, '-c', '@0@ @1@'.format(globber, kernel_src_dir), check: true).stdout().strip().split('\n')

kernel = executable('HorizonOS_Kernel',
                    kernel_src,
                    include_directories: kernel_inc,
                    dependencies: [
                        uacpi_lib_dep,
                        limine_protocol_dep,
                        flanterm_dep,
                        freestnd_c_hdrs_dep,
                        freestnd_cxx_hdrs_dep,
                        npf_dep,
                        kernel_common_dep,
                        hos_base_dep,
                        hos_lib_dep,
                    ],
                    link_depends: kernel_linker_script,
                    c_args: kernel_c_flags,
                    cpp_args: kernel_cpp_flags,
                    link_args: kernel_link_flags,
                    pie: true,
                    native: false
)