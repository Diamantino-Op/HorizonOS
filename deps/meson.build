project('horizon_deps', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'])

# Utils

# deps_dir = join_paths(meson.global_source_root(), 'deps')
deps_dir = '.'

mkdir = find_program('cp', required: true)
git = find_program('git', required: true)
sh = find_program('sh', required: true)
globber = get_option('globber_path')

arch = host_machine.cpu_family()



# UAcpi

uacpi_dep_path = join_paths(deps_dir, 'uacpi')

uacpi_git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(uacpi_dep_path, get_option('uacpi_branch'), 'https://github.com/uACPI/uACPI.git', git)

run_command(sh, '-c', uacpi_git_cmd)

uacpi = subproject('uacpi')

uacpi_src = uacpi.get_variable('sources')
uacpi_inc = uacpi.get_variable('includes')

uacpi_dep = declare_dependency(sources: uacpi_src, include_directories: uacpi_inc)



# Limine

limine_path = join_paths(deps_dir, 'limine')

limine_dep_path = join_paths(deps_dir, 'limine')

limine_git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(limine_path, get_option('limine_branch'), 'https://codeberg.org/Limine/Limine.git', git)

run_command(sh, '-c', limine_git_cmd)

run_command(sh, '-c', 'cd', limine_path, '&&', 'make')

limine_inc = include_directories(limine_path)

limine_dep = declare_dependency(include_directories: limine_inc)



# Flanterm

flanterm_path = join_paths(deps_dir, 'flanterm')

flanterm_git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(flanterm_path, get_option('flanterm_branch'), 'https://codeberg.org/mintsuki/flanterm.git', git)

run_command(sh, '-c', flanterm_git_cmd)

flanterm_src_dir = join_paths(flanterm_path, 'src')

flanterm_inc = include_directories(flanterm_src_dir)

flanterm_src = run_command(sh, '-c', globber, flanterm_src_dir, check: true).stdout().strip().split('\n')

flanterm_dep = declare_dependency(sources: flanterm_src, include_directories: flanterm_inc)



# C Headers

chdrs_path = join_paths(deps_dir, 'chdrs')

chdrs_git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(chdrs_path, get_option('chdrs_branch'), 'https://codeberg.org/osdev/freestnd-c-hdrs.git', git)

run_command(sh, '-c', chdrs_git_cmd)

chdrs_inc = include_directories(join_paths(chdrs_path, arch))

freestnd_c_hdrs_dep = declare_dependency(include_directories: chdrs_inc)



# C++ Headers

cxxhdrs_path = join_paths(deps_dir, 'cxxhdrs')

cxxhdrs_git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(cxxhdrs_path, get_option('cxxhdrs_branch'), 'https://codeberg.org/osdev/freestnd-cxx-hdrs.git', git)

run_command(sh, '-c', cxxhdrs_git_cmd)

cxxhdrs_inc = include_directories(join_paths(cxxhdrs_path, arch))

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



# LLVM

if get_option('build_llvm')
    llvm_path = join_paths(deps_dir, 'llvm')

    git_cmd = 'if [ - d "@0@/.git" ] then cd @0@ && @3@ pull || true; else @3@ clone --depth=1 --branch "@1@" @2@ @0@; fi'.format(llvm_path, get_option('llvm_branch'), 'https://github.com/llvm/llvm-project.git', git)

    run_command(sh, '-c', git_cmd)

    llvm_build_path = join_paths(llvm_path, 'build')

    run_command(sh, '-c', 'cmake -S @0@ -B @1@ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_{C,CXX}_FLAGS="-pipe" -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" -DLLVM_TARGETS_TO_BUILD="X86;RISCV;AArch64" -DCMAKE_CXX_STANDARD=23 -DLLVM_CCACHE_BUILD=ON -DDLLVM_ENABLE_ASSERTIONS=ON -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20'.format(llvm_path, llvm_build_path))

    run_command(sh, '-c', 'cmake --build %s --parallel' % llvm_build_path)
endif