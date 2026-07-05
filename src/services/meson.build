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

# NVMe Service

nvme_proj = subproject('nvme', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

nvme_exe = nvme_proj.get_variable('nvme_exe')

# Storage Manager Service

storage_manager_proj = subproject('storage_manager', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

storage_manager_exe = storage_manager_proj.get_variable('storage_manager_exe')

# Ext2 Service

ext2_proj = subproject('ext2', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

ext2_exe = ext2_proj.get_variable('ext2_exe')

# Ext2 Service

vfs_proj = subproject('vfs', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

vfs_exe = vfs_proj.get_variable('vfs_exe')