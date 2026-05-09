# Services Path

hos_services_path = join_paths(meson.global_source_root(), 'src', 'services')



# Name/Registry Service

name_registry_proj = subproject('name_registry', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

name_registry_exe = name_registry_proj.get_variable('name_registry_exe')

# uAcpi Service

uacpi_proj = subproject('uacpi-service', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

uacpi_exe = uacpi_proj.get_variable('uacpi_exe')

# PCI Service

pci_proj = subproject('pci', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

pci_exe = pci_proj.get_variable('pci_exe')