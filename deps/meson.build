project('horizon_deps', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'])

# Utils

deps_dir = '.'

mkdir = find_program('cp', required: true)
git = find_program('git', required: true)
sh = find_program('sh', required: true)
fs = import('fs', required: true)
globber = get_option('globber_path')

arch = host_machine.cpu_family()



# UAcpi

uacpi_dep_path = join_paths(deps_dir, 'uacpi')

uacpi = subproject('uacpi')

uacpi_src = uacpi.get_variable('sources')
uacpi_inc = uacpi.get_variable('includes')

uacpi_dep = declare_dependency(sources: uacpi_src, include_directories: uacpi_inc)



# Limine

limine_path = join_paths(deps_dir, 'limine')

run_command(sh, '-c', 'cd @0@ && make'.format(limine_path))



# Limine Protocl

limine_protocol_path = join_paths(deps_dir, 'limine_protocol')

limine_protocol_inc = include_directories(join_paths(limine_protocol_path, 'include'))

limine_protocol_dep = declare_dependency(include_directories: limine_protocol_inc)



# Flanterm

flanterm_path = join_paths(deps_dir, 'flanterm')

flanterm_src_dir = join_paths(flanterm_path, 'src')

flanterm_inc = include_directories(flanterm_src_dir)

flanterm_src = run_command(sh, '-c', '@0@ @1@ *.c *.cpp'.format(globber, flanterm_src_dir), check: true).stdout().strip().split('\n')

flanterm_dep = declare_dependency(sources: flanterm_src, include_directories: flanterm_inc)



# C Headers

chdrs_path = join_paths(deps_dir, 'chdrs')

chdrs_inc = include_directories(join_paths(chdrs_path, arch, 'include'))

freestnd_c_hdrs_dep = declare_dependency(include_directories: chdrs_inc)



# C++ Headers

cxxhdrs_path = join_paths(deps_dir, 'cxxhdrs')

cxxhdrs_inc = include_directories(join_paths(cxxhdrs_path, arch, 'include'))

freestnd_cxx_hdrs_dep = declare_dependency(include_directories: cxxhdrs_inc)



# OVMF

ovmf_path = join_paths(deps_dir, 'ovmf', arch)

ovmf_file = join_paths(ovmf_path, 'OVMF.fd')
ovmf_vars_file = join_paths(ovmf_path, 'OVMF_VARS.fd')

get_ovmf_script_path = join_paths(deps_dir, 'GetOVMFLinks.sh')

run_command(sh, '-c', 'mkdir -p @0@'.format(ovmf_path))

res_code = run_command(sh, '-c', '@0@ code @1@'.format(get_ovmf_script_path, arch))
ovmf_link = res_code.stdout().strip().replace('"', '')

run_command(sh, '-c', 'wget -O "@0@" @1@'.format(ovmf_file, ovmf_link))

res_vars = run_command(sh, '-c', '@0@ vars @1@'.format(get_ovmf_script_path, arch))
ovmf_vars_link = res_code.stdout().strip().replace('"', '')

run_command(sh, '-c', 'wget -O "@0@" @1@'.format(ovmf_vars_file, ovmf_vars_link))

if arch == 'riscv64' or arch == 'aarch64'
    run_command(sh, '-c', 'dd if=/dev/zero of="@0@" bs=1 count=0 seek=67108864'.format(ovmf_file))
endif



# NanoPrintf

npf_path = join_paths(deps_dir, 'nanoprintf', 'nanoprintf.h')

run_command(sh, '-c', 'mkdir -p "@0@/nanoprintf"'.format(deps_dir))

run_command(sh, '-c', 'wget -O "@0@" https://raw.githubusercontent.com/charlesnicholson/nanoprintf/main/nanoprintf.h'.format(npf_path))

npf_inc = include_directories(join_paths(deps_dir, 'nanoprintf'))

npf_dep = declare_dependency(include_directories: npf_inc)



# Linux Headers

linux_headers_path = join_paths(meson.global_source_root(), 'deps', 'linux-headers-@0@'.format(arch))

if not fs.is_dir(linux_headers_path)
    message('Downloading Linux kernel headers...')

    message(run_command(sh, '-c', 'wget -O @0@ https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.17.9.tar.xz'.format(join_paths(deps_dir, 'linux-6.17.9.tar.xz')), check: true, capture: true).stdout())

    run_command(sh, '-c', 'mkdir -p @0@'.format(join_paths(deps_dir, 'linux_kernel')), check: true)

    message(run_command(sh, '-c', 'tar xf @0@ -C @1@'.format(join_paths(deps_dir, 'linux-6.17.9.tar.xz'), join_paths(deps_dir, 'linux_kernel')), check: true, capture: true).stdout())

    message(run_command(sh, '-c', 'cd @0@ && make ARCH=@1@ headers_install INSTALL_HDR_PATH=@2@'.format(join_paths(deps_dir, 'linux_kernel', 'linux-6.17.9'), arch, linux_headers_path), check: true, capture: true).stdout())

    message(run_command(sh, '-c', 'rm -rf @0@'.format(join_paths(deps_dir, 'linux-6.17.9.tar.xz')), check: true, capture: true).stdout())
    message(run_command(sh, '-c', 'rm -rf @0@'.format(join_paths(deps_dir, 'linux_kernel')), check: true, capture: true).stdout())
endif



# LLVM

llvm_path = join_paths(deps_dir, 'llvm')
compiler_rt_path = join_paths(llvm_path, 'compiler-rt')
runtimes_path = join_paths(llvm_path, 'runtimes')

llvm_build_path = join_paths(llvm_path, 'build')
compiler_rt_build_path = join_paths(compiler_rt_path, 'build-rt-builtins')
cxx_build_path = join_paths(compiler_rt_path, 'build-cxx')

toolchain_path = join_paths(meson.global_source_root(), 'toolchain')

sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot', 'usr')

bin_path = join_paths(toolchain_path, 'bin')

mlibc_path = join_paths(meson.global_source_root(), 'libs', 'mlibc')

res_file_path = join_paths(meson.global_source_root(), 'res', arch, 'horizon-mlibc-cross.cfg')

if get_option('debug_build')
    mlibc_buildtype = 'debugoptimized'
else
    mlibc_buildtype = 'release'
endif

if get_option('build_llvm')
    # LLVM

    message(run_command(sh, '-c', 'cmake -S @0@ -B @1@ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_{C,CXX,ASM}_FLAGS="-pipe" -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" -DLLVM_TARGETS_TO_BUILD="X86;RISCV;AArch64" -DCMAKE_INSTALL_PREFIX=@2@ -DDEFAULT_SYSROOT=@3@ -DLLVM_DEFAULT_TARGET_TRIPLE=@4@-horizonos-elf -DENABLE_LINKER_BUILD_ID=ON -DLLVM_CCACHE_BUILD=ON -DLLVM_LINK_LLVM_DYLIB=ON -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20'.format(
                            join_paths(llvm_path, 'llvm'),
                            llvm_build_path,
                            toolchain_path,
                            join_paths(meson.global_source_root(), 'libs', 'sysroot'),
                            arch
                        ), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --parallel'.format(llvm_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --install @0@'.format(llvm_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'rm @0@'.format(llvm_build_path), capture: true, check: true).stderr())
endif

if get_option('build_libc')
    # MLibC Headers

    message(run_command(sh, '-c', 'cd @0@ && meson setup --reconfigure build --cross-file @1@ --prefix @2@ --libdir @2@ --includedir @3@ -Ddefault_library=both --buildtype @4@ -Dlinux_kernel_headers=@5@ -Dheaders_only=true && cd build && meson compile && meson install && cd .. && rm build'.format(
                            mlibc_path,
                            res_file_path,
                            join_paths(sysroot_path, 'lib'),
                            join_paths(sysroot_path, 'include'),
                            mlibc_buildtype,
                            join_paths(meson.global_source_root(), 'deps', 'linux-headers-x86_64', 'include')
                        ), capture: true, check: true).stderr())


    # Compiler RT

    message(run_command(sh, '-c', 'cmake -S @0@ -B @1@ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=@2@ -DCMAKE_CXX_COMPILER=@3@ -DCMAKE_ASM_COMPILER=@2@ -DCMAKE_AR=@4@ -DCMAKE_RANLIB=@5@ -DCMAKE_NM=@6@ -DLLVM_CMAKE_DIR=@7@ -DCMAKE_SYSROOT=@8@ -DCMAKE_INSTALL_PREFIX=@9@ -DCMAKE_C_COMPILER_TARGET=@10@-horizonos-elf -DCMAKE_CXX_COMPILER_TARGET=@10@-horizonos-elf -DCMAKE_ASM_COMPILER_TARGET=@10@-horizonos-elf -DCMAKE_{C,CXX,ASM}_FLAGS="-fuse-ld=lld -pipe" -DCOMPILER_RT_BUILD_BUILTINS=ON -DCOMPILER_RT_BUILD_SANITIZERS=OFF -DCOMPILER_RT_BUILD_XRAY=OFF -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_PROFILE=OFF -DCOMPILER_RT_BUILD_CRT=ON -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON -DCOMPILER_RT_INCLUDE_TESTS=OFF -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCOMPILER_RT_BUILD_STANDALONE_LIBATOMIC=ON -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20'.format(
                            compiler_rt_path,
                            compiler_rt_build_path,
                            join_paths(bin_path, 'clang'),
                            join_paths(bin_path, 'clang++'),
                            join_paths(bin_path, 'llvm-ar'),
                            join_paths(bin_path, 'llvm-ranlib'),
                            join_paths(bin_path, 'llvm-nm'),
                            join_paths(toolchain_path, 'lib', 'cmake', 'llvm'),
                            join_paths(meson.global_source_root(), 'libs', 'sysroot'),
                            sysroot_path,
                            arch
                        ), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target builtins --parallel'.format(compiler_rt_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target install-builtins'.format(compiler_rt_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'ln -sr @0@/libclang_rt.builtins-@2@.a @1@/libgcc.a'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())

    clang_resource_dir = run_command(sh, '-c', '@0@ --print-resource-dir'.format(join_paths(bin_path, 'clang')), capture: true, check: true).stdout().strip()

    message(run_command(sh, '-c', 'mkdir -p @1@ && ln -sr @0@/libclang_rt.builtins-@2@.a @1@/libclang_rt.builtins.a'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(clang_resource_dir, 'lib', '@0@-unknown-horizonos-elf'.format(arch)), arch), capture: true, check: true).stderr())


    # CRT

    message(run_command(sh, '-c', 'cmake --build @0@ --target crt --parallel'.format(compiler_rt_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target install-crt'.format(compiler_rt_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtbegin-@2@.o @1@/crtbegin.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())
    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtend-@2@.o @1@/crtend.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtbegin-@2@.o @1@/crtbeginS.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())
    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtend-@2@.o @1@/crtendS.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtbegin-@2@.o @1@/crtbeginT.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())
    message(run_command(sh, '-c', 'ln -sr @0@/clang_rt.crtend-@2@.o @1@/crtendT.o'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())


    # LibAtomic

    message(run_command(sh, '-c', 'cmake --build @0@ --target clang_rt.atomic-dynamic-@1@ --parallel'.format(compiler_rt_build_path, arch), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target install-clang_rt.atomic-dynamic-@1@'.format(compiler_rt_build_path, arch), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'ln -sr @0@/libclang_rt.atomic-@2@.so @1@/libatomic.so'.format(join_paths(sysroot_path, 'lib', 'linux'), join_paths(sysroot_path, 'lib'), arch), capture: true, check: true).stderr())


    # MLibC Full

    message(run_command(sh, '-c', 'cd @0@ && meson setup --reconfigure build --cross-file @1@ --prefix @2@ --libdir @2@ --includedir @3@ -Ddefault_library=both --buildtype @4@ -Dlinux_kernel_headers=@5@ -Dno_headers=true && cd build && meson compile && meson install && cd .. && rm build'.format(
                            mlibc_path,
                            res_file_path,
                            join_paths(sysroot_path, 'lib'),
                            join_paths(sysroot_path, 'include'),
                            mlibc_buildtype,
                            join_paths(meson.global_source_root(), 'deps', 'linux-headers-x86_64', 'include')
                        ), capture: true, check: true).stderr())


    # Libstdc++

    message(run_command(sh, '-c', 'cmake -S @0@ -B @1@ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=@2@ -DCMAKE_CXX_COMPILER=@3@ -DCMAKE_ASM_COMPILER=@2@ -DCMAKE_SYSROOT=@4@ -DCMAKE_INSTALL_PREFIX=@5@ -DCMAKE_C_COMPILER_TARGET=@6@-horizonos-elf -DCMAKE_CXX_COMPILER_TARGET=@6@-horizonos-elf -DCMAKE_ASM_COMPILER_TARGET=@6@-horizonos-elf -DLLVM_RUNTIME_TARGETS=@6@-horizonos-elf -DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" -DCMAKE_{C,CXX,ASM}_FLAGS="-fuse-ld=lld -pipe" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXXABI_USE_LLVM_UNWINDER=ON -DLIBCXX_ENABLE_SHARED=ON -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_SHARED=ON -DLIBCXXABI_ENABLE_STATIC=ON -DLIBUNWIND_ENABLE_SHARED=ON -DLIBUNWIND_ENABLE_STATIC=ON -DLIBCXX_ENABLE_ASSERTIONS=ON -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF -DLIBUNWIND_INCLUDE_TESTS=OFF -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DLIBCXX_ENABLE_THREADS=ON -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXX_HAS_PTHREAD_API=ON -DLIBCXXABI_HAS_PTHREAD_API=ON  -DLIBCXX_ENABLE_LOCALIZATION=OFF -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20'.format(
                            runtimes_path,
                            cxx_build_path,
                            join_paths(bin_path, 'clang'),
                            join_paths(bin_path, 'clang++'),
                            join_paths(meson.global_source_root(), 'libs', 'sysroot'),
                            sysroot_path,
                            arch
                        ), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target unwind cxxabi cxx --parallel'.format(cxx_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --target install-unwind install-cxxabi install-cxx'.format(cxx_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'rm @0@'.format(compiler_rt_build_path), capture: true, check: true).stderr())
    message(run_command(sh, '-c', 'rm @0@'.format(cxx_build_path), capture: true, check: true).stderr())
endif



# Targets

chdrs_branch = get_option('chdrs_branch')
cxxhdrs_branch = get_option('cxxhdrs_branch')
flanterm_branch = get_option('flanterm_branch')
limine_branch = get_option('limine_branch')
limine_protocol_branch = get_option('limine_protocol_branch')
llvm_branch = get_option('llvm_branch')
uacpi_branch = get_option('uacpi_branch')

run_target('setup_deps',
    command: [sh, '-c',
        'git clone --single-branch --depth 1 -b ' + chdrs_branch + ' https://codeberg.org/OSDev/freestnd-c-hdrs.git ' + chdrs_path + ' && ' +
        'git clone --single-branch --depth 1 -b ' + cxxhdrs_branch + ' https://codeberg.org/OSDev/freestnd-cxx-hdrs.git ' + cxxhdrs_path + ' && ' +
        'git clone --single-branch --depth 1 -b ' + flanterm_branch + ' https://codeberg.org/Mintsuki/Flanterm.git ' + flanterm_path + ' && ' +
        'git clone --single-branch --depth 1 -b ' + limine_branch + ' https://codeberg.org/Limine/Limine.git ' + limine_path + ' && ' +
        'git clone --single-branch --depth 1 -b ' + limine_protocol_branch + ' https://codeberg.org/Limine/limine-protocol.git ' + limine_protocol_path + ' && ' +
        'git clone --single-branch --depth 1 -b ' + uacpi_branch + ' https://github.com/uACPI/uACPI.git ' + uacpi_dep_path
    ]
)

run_target('update_deps',
    command: [sh, '-c',
        'cd ' + join_paths(meson.global_source_root(), chdrs_path) + ' && git pull && ' +
        'cd ' + join_paths(meson.global_source_root(), cxxhdrs_path) + ' && git pull && ' +
        'cd ' + join_paths(meson.global_source_root(), flanterm_path) + ' && git pull && ' +
        'cd ' + join_paths(meson.global_source_root(), limine_path) + ' && git pull && ' +
        'cd ' + join_paths(meson.global_source_root(), limine_protocol_path) + ' && git pull && ' +
        'cd ' + join_paths(meson.global_source_root(), uacpi_dep_path) + ' && git pull'
    ]
)

run_target('setup_llvm',
    command: [git, 'clone', '--single-branch', '--depth', '1', '-b', llvm_branch, 'https://github.com/Diamantino-Op/llvm-project.git', llvm_path]
)