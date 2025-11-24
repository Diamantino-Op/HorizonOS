# Library Flags

base_library_c_flags = []

library_c_flags = [
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

base_library_cpp_flags = []

library_cpp_flags = []

if arch == 'x86_64'
    library_c_flags += [
        '-m64',
        '-march=x86-64',
    ]

    base_library_c_flags += library_c_flags

    base_library_c_flags += [
        '-mno-red-zone',
        '-mno-sse',
        '-mno-sse2',
        '-mno-mmx',
        '-mno-80387',
    ]

    library_cpp_flags += []

    base_library_cpp_flags += library_cpp_flags
elif arch == 'riscv64'
    library_c_flags += []

    base_library_c_flags += library_c_flags

    library_cpp_flags += []

    base_library_cpp_flags += library_cpp_flags
elif arch == 'aarch64'
    library_c_flags += []

    base_library_c_flags += library_c_flags

    library_cpp_flags += []

    base_library_cpp_flags += library_cpp_flags
endif

if get_option('debug_build')
    library_c_flags += [
        '-Og',
        '-g',
        '-ggdb3',
    ]
else
    library_c_flags += [
        '-O3',
        '-ffast-math',
    ]
endif

library_cpp_flags += library_c_flags

base_library_cpp_flags += base_library_c_flags



# Hos Base

hos_base_proj = subproject('hos_base', default_options: [ 'lib_c_flags=@0@'.format(','.join(base_library_c_flags)), 'lib_cpp_flags=@0@'.format(','.join(base_library_cpp_flags)), 'globber_path=@0@'.format(globber) ])

hos_base_dep = hos_base_proj.get_variable('hos_base_dep')



# Hos Lib

hos_lib_proj = subproject('hos_lib', default_options: [ 'lib_c_flags=@0@'.format(','.join(library_c_flags)), 'lib_cpp_flags=@0@'.format(','.join(library_cpp_flags)), 'globber_path=@0@'.format(globber) ])

hos_lib_dep = hos_lib_proj.get_variable('hos_lib_dep')



# MLibC

if get_option('debug_build')
    mlibc_buildtype = 'debugoptimized'
else
    mlibc_buildtype = 'release'
endif

mlibc_proj = subproject('mlibc', default_options: [ 'prefix=@0@'.format(iso_libs_path), 'default_library=both', 'buildtype=@0@'.format(mlibc_buildtype) ])

static_libc = mlibc_proj.get_variable('libc_static')
shared_libc = mlibc_proj.get_variable('libc_shared')