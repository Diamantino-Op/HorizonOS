project('horizon_deps', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'])

# Utils

deps_dir = '.'

mkdir = find_program('cp', required: true)
git = find_program('git', required: true)
sh = find_program('sh', required: true)
fs = import('fs', required: true)
globber = get_option('globber_path')

arch = host_machine.cpu_family()

run_command(sh, '-c', 'cd', '..', '&&', git, 'submodule', 'update', '--recursive')



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

limine_protocol_inc = include_directories(join_paths(deps_dir, 'limine_protocol', 'include'))

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

if get_option('build_llvm')
    llvm_path = join_paths(deps_dir, 'llvm')

    llvm_build_path = join_paths(llvm_path, 'build')

    run_command(sh, '-c', 'cp @0@/HorizonOS.cpp @1@/HorizonOS.cpp'.format(join_paths(deps_dir, 'llvm-patches'), join_paths(deps_dir, 'llvm', 'clang', 'lib', 'Driver', 'ToolChains')), check: true)
    run_command(sh, '-c', 'cp @0@/HorizonOS.h @1@/HorizonOS.h'.format(join_paths(deps_dir, 'llvm-patches'), join_paths(deps_dir, 'llvm', 'clang', 'lib', 'Driver', 'ToolChains')), check: true)

    message(run_command(sh, '-c', 'cd @0@ && git apply ../llvm-patches/LLVM.patch'.format(llvm_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake -S @0@ -B @1@ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_{C,CXX}_FLAGS="-pipe" -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" -DLLVM_TARGETS_TO_BUILD="X86;RISCV;AArch64" -DLLVM_CCACHE_BUILD=ON -DDLLVM_ENABLE_ASSERTIONS=ON -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20'.format(join_paths(llvm_path, 'llvm'), llvm_build_path), capture: true, check: true).stderr())

    message(run_command(sh, '-c', 'cmake --build @0@ --parallel'.format(llvm_build_path), capture: true, check: true).stderr())
endif