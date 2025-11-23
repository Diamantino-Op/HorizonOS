project('HorizonOS', 'c', 'cpp', default_options: ['warning_level=2', 'c_std=gnu2x', 'cpp_std=c++23'], version: '1.0.0')

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

# Ad to cross config
# [properties]
# c_stdlib = 'mlibc'



# Programs

cp = find_program('cp', required: true)
mkdir = find_program('cp', required: true)
git = find_program('git', required: true)
sh = find_program('sh', required: true)

globber = join_paths(meson.global_source_root(), 'deps', 'Globber.sh')



# Paths

limine_path = join_paths('deps', 'limine')

ovmf_file = join_paths('deps', 'ovmf', 'OVMF.fd')
ovmf_vars_file = join_paths('deps', 'ovmf', 'OVMF_VARS.fd')

res_path = join_paths(meson.global_source_root(), 'res')

if get_option('debug_build')
    limine_conf_file = join_paths(res_path, 'Limine-Debug.conf')
    iso_path = join_paths(meson.global_source_root(), 'iso', arch, 'Debug')
else
    limine_conf_file = join_paths(res_path, 'Limine-Release.conf')
    iso_path = join_paths(meson.global_source_root(), 'iso', arch, 'Release')
endif

iso_out_path = join_paths(iso_path, 'out')

efi_boot_path = join_paths(iso_path, 'efi', 'boot')
horizonos_boot_path = join_paths(iso_path, 'boot', 'HorizonOS')
limine_boot_path = join_paths(iso_path, 'boot', 'limine')



# Subdirs

deps_proj = subproject('horizon_deps', default_options: [ 'globber_path=@0@'.format(globber) ])

uacpi_dep = deps_proj.get_variable('uacpi_dep')
limine_protocol_dep = deps_proj.get_variable('limine_protocol_dep')
flanterm_dep = deps_proj.get_variable('flanterm_dep')
freestnd_c_hdrs_dep = deps_proj.get_variable('freestnd_c_hdrs_dep')
freestnd_cxx_hdrs_dep = deps_proj.get_variable('freestnd_cxx_hdrs_dep')
npf_dep = deps_proj.get_variable('npf_dep')

subdir(join_paths('src', 'libs'))

# subdir(join_paths('src', 'programs'))

# subdir(join_paths('src', 'services'))

subdir(join_paths('src', 'kernel'))



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
        join_paths(res_path, 'Images', 'HorizonOS-Logo.png'),
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
    command: [
        'nm',
        '-C',
        '@INPUT0@',
        '>',
        sym_out_file
    ],
    depends: kernel
)

horizonos_iso = custom_target('HorizonOS_ISO',
    output: 'HorizonOS-@0@.iso'.format(arch),
    command: [
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
        join_paths(limine_path, 'limine-install'), 'bios-install', iso_out_file
    ],
    depends: [
        kernel,
        copy_iso_files,
        create_sym_file
    ]
)



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
    '-drive', 'if=pflash,format=raw,readpnly=on,file=@0@'.format(ovmf_file),
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