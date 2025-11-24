build_flags = [ '-pipe', '-nostdinc', '-static', '--target=@0@-none-elf'.format(arch), '-ffreestanding', '-ffunction-sections', '-fdata-sections', '-m64', '-march=x86-64', '-O3', '-ffast-math' ]

uacpi_lib = static_library('uacpi_lib',
                           dependencies: [
                               freestnd_c_hdrs_dep,
                               freestnd_cxx_hdrs_dep,
                               uacpi_dep
                           ],
                           c_args: build_flags,
                           cpp_args: build_flags,
                           pic: true,
                           native: false
)

uacpi_lib_dep = declare_dependency(include_directories: uacpi_inc, link_with: uacpi_lib)