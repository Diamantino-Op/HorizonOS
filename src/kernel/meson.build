kernel_common_proj = subproject('kernel-common')
kernel_common_lib = kernel_common_proj.get_variable('kernel-common')

if horizon_arch == 'x86_64'
  kernel_arch_proj = subproject('kernel-x86_64')
elif horizon_arch == 'riscv64'
  kernel_arch_proj = subproject('kernel-riscv64')
elif horizon_arch == 'aarch64'
  kernel_arch_proj = subproject('kernel-aarch64')
else
  error('Invalid Arch')
endif

kernel = kernel_arch_proj.get_variable('kernel')

hos_base_proj = subproject('hos-base')
hos_base = hos_base_proj.get_variable('hos-base')

uacpi_includes = uacpi.get_variable('includes')

kernel.include_directories(uacpi_includes)
kernel_common_lib.include_directories(uacpi_includes)

kernel_common_lib.link_with(hos_base)
kernel.link(kernel_common_lib)

linker_script_path = get_option('linker_script')
kernel.link_args('-T' + linker_script_path)