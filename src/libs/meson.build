# Library Path

hos_libs_path = join_paths(meson.global_source_root(), 'src', 'libs')



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

#hos_base_proj = subproject('hos_base', default_options: [ 'lib_c_flags=@0@'.format(','.join(base_library_c_flags)), 'lib_cpp_flags=@0@'.format(','.join(base_library_cpp_flags)), 'globber_path=@0@'.format(globber) ])

#hos_base_dep = hos_base_proj.get_variable('hos_base_dep')

hos_base_src_path = join_paths(hos_libs_path, 'hos-base', 'src')

hos_base_inc = include_directories(join_paths('hos-base', 'src'))

hos_base_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(globber, hos_base_src_path), check: true).stdout().strip().split('\n')

hos_base_lib = static_library('hos_base',
                              hos_base_src,
                              include_directories: hos_base_inc,
                              dependencies: [
                                  freestnd_c_hdrs_dep,
                                  freestnd_cxx_hdrs_dep
                              ],
                              c_args: base_library_c_flags,
                              cpp_args: base_library_cpp_flags,
                              pic: true,
                              native: false
)

hos_base_dep = declare_dependency(include_directories: hos_base_inc, link_with: hos_base_lib)



# Hos Lib

# hos_lib_proj = subproject('hos_lib', default_options: [ 'lib_c_flags=@0@'.format(','.join(library_c_flags)), 'lib_cpp_flags=@0@'.format(','.join(library_cpp_flags)), 'globber_path=@0@'.format(globber) ])

# hos_lib_dep = hos_lib_proj.get_variable('hos_lib_dep')

hos_libs_src_path = join_paths(hos_libs_path, 'hos-lib', 'src')

hos_lib_inc = include_directories(join_paths('hos-lib', 'src'))

hos_lib_src = run_command(sh, '-c', '@0@ @1@ *.cpp'.format(globber, hos_libs_src_path), check: true).stdout().strip().split('\n')

hos_lib_lib = static_library('hos_lib',
                             hos_lib_src,
                             include_directories: hos_lib_inc,
                             dependencies: [
                                 freestnd_c_hdrs_dep,
                                 freestnd_cxx_hdrs_dep
                             ],
                             c_args: library_c_flags,
                             cpp_args: library_cpp_flags,
                             pic: true,
                             native: false
)

hos_lib_dep = declare_dependency(include_directories: hos_lib_inc, link_with: hos_lib_lib)



# MLibC

common_libs_path = join_paths(meson.global_source_root(), 'build', 'libs')

if get_option('debug_build')
    mlibc_buildtype = 'debugoptimized'
else
    mlibc_buildtype = 'release'
endif

default_library_paths = []

linux_headers_path_mlibc = join_paths(linux_headers_path.replace(meson.global_source_root(), '../../..'), 'include')

mlibc_proj = subproject('mlibc', default_options: [ 'prefix=@0@'.format(common_libs_path), 'libdir=@0@'.format(common_libs_path), 'default_library=both', 'buildtype=@0@'.format(mlibc_buildtype), 'linux_kernel_headers=@0@'.format(linux_headers_path_mlibc) ])

static_mlibc = mlibc_proj.get_variable('libc_static')
mlibc_include_dirs = mlibc_proj.get_variable('libc_include_dirs')

mlibc_static_dep = declare_dependency(include_directories: mlibc_include_dirs, link_with: static_mlibc)