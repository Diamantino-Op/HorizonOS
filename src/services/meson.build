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

ext4_proj = subproject('ext4', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

ext4_exe = ext4_proj.get_variable('ext4_exe')

# Vfs Service

vfs_proj = subproject('vfs', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

vfs_exe = vfs_proj.get_variable('vfs_exe')

# XHCI Service

xhci_proj = subproject('xhci', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

xhci_exe = xhci_proj.get_variable('xhci_exe')

# Fat32 Service

fat32_proj = subproject('fat32', default_options: [ 'globber_path=@0@'.format(globber), 'arch=@0@'.format(arch) ])

fat32_exe = fat32_proj.get_variable('fat32_exe')