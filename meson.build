project('HorizonOS', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'], version: '1.0.0', meson_version: '>=1.3.0')

add_languages('c', 'cpp', native: true, required: true)
add_languages('c', 'cpp', native: false, required: true)

arch = host_machine.cpu_family()

supported_archs = [
    'aarch64',
    'riscv64',
    'x86_64'
]

if not supported_archs.contains(arch)
    error('Unknown Architecture: @0@'.format(arch))
endif



# Programs

cp = find_program('cp', required: true)
mkdir = find_program('mkdir', required: true)
git = find_program('git', required: true)
sh = find_program('sh', required: true)
bash = find_program('bash', required: true)
nm = find_program('nm', required: true)

globber = join_paths(meson.global_source_root(), 'deps', 'Globber.sh')



# Paths

limine_path = join_paths(meson.global_source_root(), 'deps', 'limine')

ovmf_file = join_paths(meson.global_source_root(), 'deps', 'ovmf', arch, 'OVMF.fd')
ovmf_vars_file = join_paths(meson.global_source_root(), 'deps', 'ovmf', arch, 'OVMF_VARS.fd')

res_path = join_paths(meson.global_source_root(), 'res')

if get_option('debug_build')
    limine_conf_file = join_paths(res_path, 'Limine-Debug.conf')
    iso_path = join_paths(meson.global_source_root(), 'iso', arch, 'Debug')
else
    limine_conf_file = join_paths(res_path, 'Limine-Release.conf')
    iso_path = join_paths(meson.global_source_root(), 'iso', arch, 'Release')
endif

iso_out_path = join_paths(meson.global_source_root(), 'iso', 'out')

efi_boot_path = join_paths(iso_path, 'efi', 'boot')
horizonos_boot_path = join_paths(iso_path, 'boot', 'HorizonOS')
limine_boot_path = join_paths(iso_path, 'boot', 'limine')

iso_programs_path = join_paths(horizonos_boot_path, 'programs')
iso_services_path = join_paths(horizonos_boot_path, 'services')
iso_libs_path = join_paths(horizonos_boot_path, 'libs')

sysroot_path = join_paths(meson.global_source_root(), 'libs', 'sysroot')



# Subdirs

deps_proj = subproject('horizon_deps',
    default_options: [
        'globber_path=@0@'.format(globber),
        'build_llvm=@0@'.format(get_option('build_llvm') ? 'true' : 'false'),
        'build_libc=@0@'.format(get_option('build_libc') ? 'true' : 'false'),
        'debug_build=@0@'.format(get_option('debug_build') ? 'true' : 'false'),

        'chdrs_branch=@0@'.format(get_option('chdrs_branch')),
        'cxxhdrs_branch=@0@'.format(get_option('cxxhdrs_branch')),
        'flanterm_branch=@0@'.format(get_option('flanterm_branch')),
        'limine_branch=@0@'.format(get_option('limine_branch')),
        'limine_protocol_branch=@0@'.format(get_option('limine_protocol_branch')),
        'llvm_branch=@0@'.format(get_option('llvm_branch')),
        'uacpi_branch=@0@'.format(get_option('uacpi_branch')),
    ]
)

deps_proj = subproject('horizon_deps',
    default_options: [
        'globber_path=@0@'.format(globber)
    ]
)

uacpi_dep = deps_proj.get_variable('uacpi_dep')
limine_protocol_dep = deps_proj.get_variable('limine_protocol_dep')
flanterm_dep = deps_proj.get_variable('flanterm_dep')
freestnd_c_hdrs_dep = deps_proj.get_variable('freestnd_c_hdrs_dep')
freestnd_cxx_hdrs_dep = deps_proj.get_variable('freestnd_cxx_hdrs_dep')
npf_dep = deps_proj.get_variable('npf_dep')
linux_headers_path = deps_proj.get_variable('linux_headers_path')

subdir(join_paths('src', 'libs'))

subdir(join_paths('src', 'programs'))

subdir(join_paths('src', 'services'))

subdir(join_paths('src', 'kernel'))

subdir('libs')



# ISO

if arch == 'x86_64'
    boot_file = join_paths(limine_path, 'BOOTX64.EFI')
elif arch == 'riscv64'
    boot_file = join_paths(limine_path, 'BOOTRISCV64.EFI')
elif arch == 'aarch64'
    boot_file = join_paths(limine_path, 'BOOTAA64.EFI')
endif

iso_out_file = join_paths(iso_out_path, 'HorizonOS-@0@.iso'.format(arch))
sym_out_file = join_paths(horizonos_boot_path, 'HorizonOS.sym')

copy_iso_files = custom_target('copy_iso_files',
    input: [
        limine_conf_file,
        join_paths(limine_path, 'limine-bios.sys'),
        join_paths(limine_path, 'limine-bios-cd.bin'),
        join_paths(limine_path, 'limine-bios-pxe.bin'),
        join_paths(limine_path, 'limine-uefi-cd.bin'),
        join_paths(res_path, 'Images', 'HorizonOS-Logo-Limine.png'),
        boot_file,
    ],
    output: 'HorizonOS-Logo.png',
    command: [
        mkdir, '-p', iso_path,
        '&&',
        mkdir, '-p', efi_boot_path,
        '&&',
        mkdir, '-p', horizonos_boot_path,
        '&&',
        mkdir, '-p', iso_programs_path,
        '&&',
        mkdir, '-p', iso_services_path,
        '&&',
        mkdir, '-p', iso_libs_path,
        '&&',
        mkdir, '-p', limine_boot_path,
        '&&',
        cp, '@INPUT0@', join_paths(limine_boot_path, 'Limine.conf'),
        '&&',
        cp, '@INPUT1@', limine_boot_path,
        '&&',
        cp, '@INPUT2@', limine_boot_path,
        '&&',
        cp, '@INPUT3@', limine_boot_path,
        '&&',
        cp, '@INPUT4@', limine_boot_path,
        '&&',
        cp, '@INPUT5@', horizonos_boot_path,
        '&&',
        cp, '@INPUT6@', efi_boot_path,
    ]
)

create_sym_file = custom_target('create_sym_file',
    output: 'HorizonOS.sym',
    input: [
        kernel
    ],
    command: [ sh, '-c', 'nm -C @INPUT0@ > @0@'.format(sym_out_file) ],
    depends: kernel
)

# Services Sym files

name_registry_sym_out_file = join_paths(iso_services_path, 'name_registry.sym')
uacpi_sym_out_file = join_paths(iso_services_path, 'uacpi.sym')
pci_sym_out_file = join_paths(iso_services_path, 'pci.sym')
nvme_sym_out_file = join_paths(iso_services_path, 'nvme.sym')
storage_manager_sym_out_file = join_paths(iso_services_path, 'storage_manager.sym')
ext4_sym_out_file = join_paths(iso_services_path, 'ext4.sym')
vfs_sym_out_file = join_paths(iso_services_path, 'vfs.sym')
xhci_sym_out_file = join_paths(iso_services_path, 'xhci.sym')
fat32_sym_out_file = join_paths(iso_services_path, 'fat32.sym')
compositor_sym_out_file = join_paths(iso_services_path, 'compositor.sym')
display_sym_out_file = join_paths(iso_services_path, 'display.sym')
logd_sym_out_file = join_paths(iso_services_path, 'logd.sym')

create_services_sym_file = custom_target('create_services_sym_file',
                                output: [
                                    'name_registry.sym',
                                    'uacpi.sym',
                                    'pci.sym',
                                    'nvme.sym',
                                    'storage_manager.sym',
                                    'ext4.sym',
                                    'vfs.sym',
                                    'xhci.sym',
                                    'fat32.sym',
                                    'compositor.sym',
                                    'display.sym',
                                    'logd.sym'
                                ],
                                input: [
                                    name_registry_exe,
                                    uacpi_exe,
                                    pci_exe,
                                    nvme_exe,
                                    storage_manager_exe,
                                    ext4_exe,
                                    vfs_exe,
                                    xhci_exe,
                                    fat32_exe,
                                    compositor_exe,
                                    display_exe,
                                    logd_exe
                                ],
                                command: [
                                    sh, '-c', 'nm -C @INPUT0@ > @0@'.format(name_registry_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT1@ >> @0@'.format(uacpi_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT2@ >> @0@'.format(pci_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT3@ >> @0@'.format(nvme_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT4@ >> @0@'.format(storage_manager_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT5@ >> @0@'.format(ext4_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT6@ >> @0@'.format(vfs_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT7@ >> @0@'.format(xhci_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT8@ >> @0@'.format(fat32_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT9@ >> @0@'.format(compositor_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT10@ >> @0@'.format(display_sym_out_file),
                                    '&&',
                                    sh, '-c', 'nm -C @INPUT11@ >> @0@'.format(logd_sym_out_file),
                                ],
                                depends: [
                                    name_registry_exe,
                                    uacpi_exe,
                                    pci_exe,
                                    nvme_exe,
                                    storage_manager_exe,
                                    ext4_exe,
                                    vfs_exe,
                                    xhci_exe,
                                    fat32_exe,
                                    compositor_exe,
                                    display_exe,
                                    logd_exe
                                ]
)

install_apps = custom_target('install_apps',
    input: [
        # Apps
        hello_world_exe,

        # Services
        name_registry_exe,
        uacpi_exe,
        pci_exe,
        nvme_exe,
        storage_manager_exe,
        ext4_exe,
        vfs_exe,
        xhci_exe,
        fat32_exe,
        compositor_exe,
        display_exe,
        logd_exe,

        # Libs
        join_paths(sysroot_path, 'usr', 'lib', 'libc++.so'),
        join_paths(sysroot_path, 'usr', 'lib', 'libc++abi.so'),
        join_paths(sysroot_path, 'usr', 'lib', 'libc.so'),
        join_paths(sysroot_path, 'usr', 'lib', 'libpthread.so'),
        join_paths(sysroot_path, 'usr', 'lib', 'ld.so')
    ],
    output: 'InstalledApps.txt',
    command: [
        cp, '@INPUT0@', iso_programs_path,
        '&&',
        cp, '@INPUT1@', iso_services_path,
        '&&',
        cp, '@INPUT2@', iso_services_path,
        '&&',
        cp, '@INPUT3@', iso_services_path,
        '&&',
        cp, '@INPUT4@', iso_services_path,
        '&&',
        cp, '@INPUT5@', iso_services_path,
        '&&',
        cp, '@INPUT6@', iso_services_path,
        '&&',
        cp, '@INPUT7@', iso_services_path,
        '&&',
        cp, '@INPUT8@', iso_services_path,
        '&&',
        cp, '@INPUT9@', iso_services_path,
        '&&',
        cp, '@INPUT10@', iso_services_path,
        '&&',
        cp, '@INPUT11@', iso_services_path,
        '&&',
        cp, '@INPUT12@', iso_services_path,
        '&&',
        cp, '@INPUT13@', iso_libs_path,
        '&&',
        cp, '@INPUT14@', iso_libs_path,
        '&&',
        cp, '@INPUT15@', iso_libs_path,
        '&&',
        cp, '@INPUT16@', iso_libs_path,
        '&&',
        cp, '@INPUT17@', iso_libs_path,
        '&&',
        sh, '-c', 'mkdir -p @0@'.format(iso_out_path),
        '&&',
        sh, '-c', 'for f in @INPUT0@; do echo "$(basename $f)" >> @0@; done'.format(join_paths(iso_out_path, 'InstalledApps.txt'))
    ],
    depends: [
        hello_world_exe,
        #create_services_sym_file,
        name_registry_exe,
        uacpi_exe,
        pci_exe,
        nvme_exe,
        storage_manager_exe,
        ext4_exe,
        vfs_exe,
        xhci_exe,
        fat32_exe,
        compositor_exe,
        display_exe,
        logd_exe
    ]
)

horizonos_iso = custom_target('HorizonOS_ISO',
    output: 'HorizonOS-@0@.iso'.format(arch),
    input: [
        kernel
    ],
    command: [
        'cp', '@INPUT0@', join_paths(horizonos_boot_path, 'HorizonOS_Kernel'),
        '&&',
        mkdir, '-p', iso_out_path,
        '&&',
        'xorriso',
        '-as', 'mkisofs',
        '-R', '-r', '-J',
        '-b', 'boot/limine/limine-bios-cd.bin',
        '-no-emul-boot',
        '-boot-load-size', '4',
        '-boot-info-table',
        '-hfsplus',
        '-apm-block-size', '2048',
        '--efi-boot', 'boot/limine/limine-uefi-cd.bin',
        '-efi-boot-part',
        '--efi-boot-image',
        '--protective-msdos-label',
        iso_path,
        '-o', iso_out_file,
        '&&',
        join_paths(limine_path, 'limine'), 'bios-install', iso_out_file
    ],
    depends: [
        kernel,
        copy_iso_files,
        create_sym_file,
        install_apps
    ]
)


# Native driver regression tests

test('driver-regressions',
     bash,
     args: [join_paths(meson.global_source_root(), 'tests', 'run_driver_regressions.sh')],
     suite: 'drivers')



# Qemu

tcg_qemu_flags = []

kvm_qemu_flags = []

qemu_flags = [
    '-device', 'qemu-xhci,id=xhci',
    '-device', 'usb-kbd,id=usbkbd',
    '-device', 'usb-mouse,id=usbmouse',
    '-device', 'virtio-net-pci',
    '-smp', '10',
    '-m', '8G',
    '-drive', 'if=pflash,format=raw,readonly=on,file=@0@'.format(ovmf_file),
    '-drive', 'if=pflash,format=raw,file=@0@'.format(ovmf_vars_file),
    '-serial', 'stdio',
    '-no-reboot',
    '-no-shutdown',
]

if arch == 'x86_64'
    kvm_qemu_flags += [
        '-cpu', 'host,+hypervisor,+invtsc,+tsc-deadline',
        '-accel', 'kvm'
    ]

    tcg_qemu_flags += [
        '-cpu', 'max,+hypervisor,+invtsc,+tsc-deadline',
        '-accel', 'tcg'
    ]

    qemu_flags += [
        '-M', 'q35',
        '-cdrom', iso_out_file,
    ]
elif arch == 'riscv64'
    qemu_flags += [
        '-cpu', 'rv64',
        '-device', 'ramfb',
        '-device', 'ide-hd,drive=hdd0',
        '-M', 'virt',
        '-drive', 'file=%s,if=none,id=hdd0', iso_out_file,
    ]
elif arch == 'aarch64'
    qemu_flags += [
        '-cpu', 'cortex-a76',
        '-device', 'ramfb',
        '-M', 'virt',
        '-cdrom', iso_out_file,
    ]
endif

if get_option('qemu_targets')
    if arch == 'x86_64'
        run_target('Run_HorizonOS_Kvm',
                   command: [
                       'qemu-system-@0@'.format(arch),
                   ] + kvm_qemu_flags + qemu_flags,
                   depends: horizonos_iso
        )
    endif

    run_target('Run_HorizonOS_Gdb',
               command: [
                   'qemu-system-@0@'.format(arch),
                   '-s', '-S',
               ] + kvm_qemu_flags + qemu_flags,
               depends: horizonos_iso
    )

    run_target('Run_HorizonOS_DInt',
               command: [
                   'qemu-system-@0@'.format(arch),
                   '-d', 'int',
                   '-D', join_paths(iso_out_path, 'ints.log'),
               ] + tcg_qemu_flags + qemu_flags,
               depends: horizonos_iso
    )
endif
