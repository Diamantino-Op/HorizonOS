# Kernel Flags

kernel_linker_script = join_paths(res_path, arch, 'Linker.ld')

kernel_c_flags = [
    '-DLIMINE_API_REVISION=4',
    '-pipe',
    '-nostdinc',
    '-static',
    '--target=@0@-horizonos-elf'.format(arch),
    '-ffreestanding',
    '-ffunction-sections',
    '-fdata-sections',
    '-fno-exceptions',
    '-Xanalyzer',
    '--analyze',
    '-fno-rtti',
    '-fno-omit-frame-pointer',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-Wno-unused-parameter',
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

kernel_asm_flags = [
    '-pipe',
    '-static',
    '-masm=intel',
    '--target=@0@-horizonos-elf'.format(arch),
]

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

    kernel_asm_flags += [
        '-mno-sse',
        '-mno-sse2',
        '-mno-mmx',
        '-mno-80387',
        '-m64',
        '-march=x86-64',
    ]
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
        '-finstrument-functions',
        '-DHORIZON_DEBUG',
    ]
else
    kernel_c_flags += [
        '-O3',
        '-ffast-math',
    ]
endif

kernel_cpp_flags += kernel_c_flags



# Kernel Paths

kernel_common_src_dir = join_paths('.', 'kernel-common', 'src')

kernel_src_dir = join_paths('.', 'kernel-@0@'.format(arch), 'src')



# Kernel Asm

kernel_src_asm = []

kernel_src_asm += run_command(sh, '-c', '@0@ @1@ *.s'.format(globber, kernel_common_src_dir), capture: true, check: true).stdout().strip().split('\n')

kernel_src_asm += run_command(sh, '-c', '@0@ @1@ *.s'.format(globber, kernel_src_dir), capture: true, check: true).stdout().strip().split('\n')

kernel_asm_targets = []

foreach src_file : kernel_src_asm
    if src_file == ''
        continue
    endif

    tmp = src_file.split('/')

    filename = tmp[tmp.length() - 1]
    filename_no_ext = filename.replace('.s', '')

    kernel_asm_targets += custom_target('@0@_asm'.format(filename_no_ext),
                                        command: meson.get_compiler('c', native: false).cmd_array() + kernel_asm_flags + ['-c', '-o', '@OUTPUT@', '@INPUT@'],
                                        input: src_file,
                                        output: '@0@.o'.format(filename_no_ext)
    ).to_list()
endforeach



# Kernel Common

kernel_common_inc = include_directories(kernel_common_src_dir)

kernel_common_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(globber, kernel_common_src_dir), check: true).stdout().strip().split('\n')
kernel_common_src += run_command(sh, '-c', '@0@ @1@ *.hpp'.format(globber, kernel_common_src_dir), check: true).stdout().strip().split('\n')

kernel_common_dep = declare_dependency(sources: kernel_common_src, include_directories: kernel_common_inc)



# Kernel Architecture

kernel_inc = include_directories(kernel_src_dir)

kernel_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(globber, kernel_src_dir), check: true).stdout().strip().split('\n')
kernel_src += run_command(sh, '-c', '@0@ @1@ *.hpp'.format(globber, kernel_src_dir), check: true).stdout().strip().split('\n')

kernel = executable('HorizonOS_Kernel',
                    kernel_src,
                    include_directories: kernel_inc,
                    dependencies: [
                        uacpi_dep,
                        limine_protocol_dep,
                        flanterm_dep,
                        freestnd_c_hdrs_dep,
                        freestnd_cxx_hdrs_dep,
                        npf_dep,
                        kernel_common_dep,
                        hos_base_dep,
                        hos_lib_dep,
                    ],
                    objects: kernel_asm_targets,
                    link_depends: kernel_linker_script,
                    c_args: kernel_c_flags,
                    cpp_args: kernel_cpp_flags,
                    link_args: kernel_link_flags,
                    pie: true,
                    native: false
)