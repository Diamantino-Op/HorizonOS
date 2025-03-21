project('HorizonOS', 'cpp',
        version : '0.0.1',
        default_options : [
          'warning_level=3',
          'c_std=gnu11',
          'cpp_std=c++2a',
        ]
)

version = get_option('horizon_version')

# ------------------ Meson Modules ------------------
fs = import('fs')

subprojectsDir = 'subprojects'

# ------------------ Global Variables ------------------
horizon_arch = get_option('horizon_arch')

linker_script = join_paths('res', horizon_arch, 'Linker.ld')
file_grabber_path = join_paths('scripts', 'FileGrabber.sh')

c_flags = ['-Wall', '-Wextra', '-O2', '-fPIE', '-fshort-wchar', '--target=@0@-none-elf'.format(horizon_arch)]
cxx_flags = c_flags
asm_flags = c_flags
exe_linker_flags = ['-T', linker_script, '-pie', '-nostdlib']

# ------------------ LLVM Compiler ------------------
llvm_repo = 'https://github.com/llvm/llvm-project.git'
llvm_branch = 'release/20.x'
llvm_dir = join_paths(subprojectsDir, 'llvm')
llvm_build_dir = join_paths(llvm_dir, 'build')
llvm_bin_dir = join_paths(llvm_build_dir, 'bin')

# Clone LLVM
if not fs.exists(llvm_dir)
  message('Cloning LLVM repository...')
  run_command('git', 'clone', '--depth=1', '--branch', llvm_branch, llvm_repo, llvm_dir, check : true)
  run_command('rm', '-rf', join_paths(llvm_dir, '.git'), check : true)
endif

# Build LLVM
if not fs.exists(llvm_build_dir)
  mkdir(llvm_build_dir)
  message('Configuring LLVM build...')
  run_command('cmake', '-S', join_paths(llvm_dir, 'llvm'), '-B', llvm_build_dir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb"', '-DLLVM_TARGETS_TO_BUILD="X86;RISCV;AArch64"', '-DLLVM_ENABLE_ASSERTIONS=OFF', '-DCMAKE_INSTALL_PREFIX=@0@/bin'.format(llvm_dir), '-DLLVM_PARALLEL_COMPILE_JOBS=12', '-DLLVM_PARALLEL_LINK_JOBS=12', check : true)

  message('Building LLVM...')
  run_command('cmake', '--build', llvm_build_dir, '--parallel', check : true)
endif

# ------------------ C Headers ------------------
chdrs_repo = 'https://github.com/osdev0/freestnd-c-hdrs.git'
chdrs_branch = 'trunk'
chdrs_dir = 'chdrs'
chdrs_inc_dir = join_paths(chdrs_dir, horizon_arch, 'include')

if not fs.exists(chdrs_dir)
  message('Cloning C Headers repository...')
  run_command('git', 'clone', '--depth=1', '--branch', chdrs_branch, chdrs_repo, chdrs_dir, check : true)
  run_command('rm', '-rf', join_paths(chdrs_dir, '.git'), check : true)
endif

include_directories(chdrs_inc_dir)

# ------------------ uACPI ------------------
uacpi_repo = 'https://github.com/uACPI/uACPI.git'
uacpi_dir = join_paths(subprojectsDir, 'uacpi')

if not fs.exists(uacpi_dir)
  message('Cloning uACPI repository...')
  run_command('git', 'clone', '--depth=1', uacpi_repo, uacpi_dir, check : true)
  run_command('rm', '-rf', join_paths(uacpi_dir, '.git'), check : true)
endif

# ------------------ Limine Bootloader ------------------
limine_repo = 'https://github.com/limine-bootloader/limine.git'
limine_branch = 'v9.x-binary'
limine_dir = 'limine'

# Clone Limine
if not fs.exists(limine_dir)
  message('Cloning Limine repository...')
  run_command('git', 'clone', '--depth=1', '--branch', limine_branch, limine_repo, limine_dir, check : true)
  run_command('rm', '-rf', join_paths(limine_dir, '.git'), check : true)
endif

# Build Limine
if not fs.exists(join_paths(limine_dir, 'limine'))
  message('Building Limine...')
  run_command('cd @0@ && make'.format(limine_dir), check : true)
endif

# ------------------ ISO Directory ------------------
iso_dir = join_paths('iso', horizon_arch)
res_dir = 'res'

if not fs.exists(iso_dir)
  mkdir(join_paths('iso', 'out'))
  mkdir(iso_dir)
  mkdir(join_paths(iso_dir, 'efi', 'boot'))
  mkdir(join_paths(iso_dir, 'boot'))
  mkdir(join_paths(iso_dir, 'boot', 'limine'))
  mkdir(join_paths(iso_dir, 'boot', 'HorizonOS'))

  if horizon_arch == 'x86_64'
    install_data(join_paths(limine_dir, 'BOOTX64.EFI'), install_dir : join_paths(iso_dir, 'efi', 'boot'))
  elif horizon_arch == 'riscv64'
    install_data(join_paths(limine_dir, 'BOOTRISCV64.EFI'), install_dir : join_paths(iso_dir, 'efi', 'boot'))
  elif horizon_arch == 'aarch64'
    install_data(join_paths(limine_dir, 'BOOTAA64.EFI'), install_dir : join_paths(iso_dir, 'efi', 'boot'))
  endif

  install_data(join_paths(res_dir, 'Limine.cfg'), install_dir : join_paths(iso_dir, 'boot', 'limine'))
  install_data(join_paths(limine_dir, 'limine-bios.sys'), install_dir : join_paths(iso_dir, 'boot', 'limine'))
  install_data(join_paths(limine_dir, 'limine-bios-cd.bin'), install_dir : join_paths(iso_dir, 'boot', 'limine'))
  install_data(join_paths(limine_dir, 'limine-bios-pxe.bin'), install_dir : join_paths(iso_dir, 'boot', 'limine'))
  install_data(join_paths(limine_dir, 'limine-uefi-cd.bin'), install_dir : join_paths(iso_dir, 'boot', 'limine'))
endif

# ------------------ OVMF Bios ------------------
ovmf_file = join_paths('ovmf', horizon_arch, 'OVMF.fd')
ovmf_link = ''

if horizon_arch == 'x86_64'
  ovmf_link = 'https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd'

  if not fs.exists(ovmf_file)
      run_command('wget', ovmf_link, '-O', ovmf_file, check : true)
  endif
elif horizon_arch == 'riscv64'
  ovmf_link = 'https://retrage.github.io/edk2-nightly/bin/RELEASERISCV64_VIRT_CODE.fd'

  if not fs.exists(ovmf_file)
      run_command('wget', ovmf_link, '-O', ovmf_file, check : true)
      run_command('dd', 'if=/dev/zero', 'of=@0@'.format(ovmf_file), 'bs=1', 'count=0', 'seek=33554432', check : true)
  endif
elif horizon_arch == 'aarch64'
  ovmf_link = 'https://retrage.github.io/edk2-nightly/bin/RELEASEAARCH64_QEMU_EFI.fd'

  if not fs.exists(ovmf_file)
      run_command('wget', ovmf_link, '-O', ovmf_file, check : true)
      run_command('dd', 'if=/dev/zero', 'of=@0@'.format(ovmf_file), 'bs=1', 'count=0', 'seek=33554432', check : true)
  endif
endif

# ------------------ Tools Setup ------------------
# C
c_compiler = find_program(join_paths(llvm_bin_dir, 'clang'), required : true)
c_ar = find_program(join_paths(llvm_bin_dir, 'llvm-ar'), required : true)
c_linker = find_program(join_paths(llvm_bin_dir, 'ld.lld'), required : true)

# C++
cxx_compiler = find_program(join_paths(llvm_bin_dir, 'clang++'), required : true)
cxx_ar = find_program(join_paths(llvm_bin_dir, 'llvm-ar'), required : true)
cxx_linker = find_program(join_paths(llvm_bin_dir, 'ld.lld'), required : true)

# ASM
asm_compiler = find_program(join_paths(llvm_bin_dir, 'clang'), required : true)
ar = find_program(join_paths(llvm_bin_dir, 'llvm-ar'), required : true)

# ------------------ Modules ------------------
modules_prj = subproject('modules', default_options : ['horizon_arch=@0@'.format(horizon_arch)])

# ------------------ ISO ------------------
iso_output = join_paths('iso', 'out', 'HorizonOS-@0@.iso'.format(horizon_arch))

# Generate ISO
iso_cmd = custom_target('HorizonOS-ISO',
                        output : iso_output,
                        command : [
                          find_program('cp'), join_paths(meson.build_root(), 'kernel/HorizonOS'), join_paths(iso_dir, 'boot', 'HorizonOS', 'HorizonOS_Kernel'),
                          find_program('xorriso'), '-as', 'mkisofs', '-R', '-r', '-J', '-b', 'boot/limine/limine-bios-cd.bin', '-no-emul-boot', '-boot-load-size', '4', '-boot-info-table', '-hfsplus', '-apm-block-size', '2048', '--efi-boot', 'boot/limine/limine-uefi-cd.bin', '-efi-boot-part', '--efi-boot-image', '--protective-msdos-label', iso_dir, '-o', iso_output,
                          join_paths(limine_dir, 'limine'), 'bios-install', iso_output,
                        ],
                        input : kernel_subproject.get_variable('kernel_binary'),
                        depend_files : [join_paths(limine_dir, 'limine'),join_paths(limine_dir, 'limine-bios-cd.bin'),join_paths(limine_dir, 'limine-uefi-cd.bin')],
                        depends : kernel_subproject.get_target('HorizonOS'),
                        install : false,
)

# ------------------ Qemu Target ------------------

# GDB Cmd: gdb target extended-remote localhost:1234

if horizon_arch == 'x86_64'
  debug_cmd = custom_target('Run-HorizonOS-Debug',
                            command : [
                              find_program('qemu-system-x86_64'), '-s', '-S', '-cpu', 'max,+hypervisor,+invtsc,+tsc-deadline', '-smp', '10', '-M', 'q35', '-m', '8G', '-accel', 'kvm', '-bios', ovmf_file, '-cdrom', iso_output, '-serial', 'stdio',
                            ],
                            input : iso_cmd,
                            depends : iso_cmd,
                            install : false,
  )

  nodebug_cmd = custom_target('Run-HorizonOS-NoDbg',
                              command : [
                                find_program('qemu-system-x86_64'), '-cpu', 'max,+hypervisor,+invtsc,+tsc-deadline', '-smp', '10', '-M', 'q35', '-m', '8G', '-accel', 'kvm', '-bios', ovmf_file, '-cdrom', iso_output, '-serial', 'stdio',
                              ],
                              input : iso_cmd,
                              depends : iso_cmd,
                              install : false,
  )
elif horizon_arch == 'riscv64'
  debug_cmd = custom_target('Run-HorizonOS-Debug',
                            command : [
                              find_program('qemu-system-riscv64'), '-s', '-S', '-cpu', 'rv64', '-smp', '10', '-M', 'virt', '-m', '8G', '-usb', '-device', 'qemu-xhci', '-device', 'usb-kbd', '-device', 'usb-mouse', '-device', 'ramfb', '-device', 'ahci,id=ahci', '-device', 'ide-hd,drive=hdd0', '-drive', 'if=pflash,unit=0,format=raw,file=@0@'.format(ovmf_file), '-drive', 'file=@0@,if=none,id=hdd0'.format(iso_output), '-serial', 'stdio',
                            ],
                            input : iso_cmd,
                            depends : iso_cmd,
                            install : false,
  )

  nodebug_cmd = custom_target('Run-HorizonOS-NoDbg',
                              command : [
                                find_program('qemu-system-riscv64'), '-cpu', 'rv64', '-smp', '10', '-M', 'virt', '-m', '8G', '-usb', '-device', 'qemu-xhci', '-device', 'usb-kbd', '-device', 'usb-mouse', '-device', 'ramfb', '-device', 'ahci,id=ahci', '-device', 'ide-hd,drive=hdd0', '-drive', 'if=pflash,unit=0,format=raw,file=@0@'.format(ovmf_file), '-drive', 'file=@0@,if=none,id=hdd0'.format(iso_output), '-serial', 'stdio',
                              ],
                              input : iso_cmd,
                              depends : iso_cmd,
                              install : false,
  )
elif horizon_arch == 'aarch64'
  debug_cmd = custom_target('Run-HorizonOS-Debug',
                            command : [
                              find_program('qemu-system-aarch64'), '-s', '-S', '-cpu', 'cortex-a53', '-smp', '10', '-M', 'virt', '-m', '8G', '-usb', '-device', 'qemu-xhci', '-device', 'usb-kbd', '-device', 'usb-mouse', '-device', 'ramfb', '-device', 'ahci,id=ahci', '-device', 'ide-hd,drive=hdd0', '-drive', 'if=pflash,unit=0,format=raw,file=@0@'.format(ovmf_file), '-drive', 'file=@0@,if=none,id=hdd0'.format(iso_output), '-serial', 'stdio',
                            ],
                            input : iso_cmd,
                            depends : iso_cmd,
                            install : false,
  )
  nodebug_cmd = custom_target('Run-HorizonOS-NoDbg',
                              command : [
                                find_program('qemu-system-aarch64'), '-cpu', 'cortex-a53', '-smp', '10', '-M', 'virt', '-m', '8G', '-usb', '-device', 'qemu-xhci', '-device', 'usb-kbd', '-device', 'usb-mouse', '-device', 'ramfb', '-device', 'ahci,id=ahci', '-device', 'ide-hd,drive=hdd0', '-drive', 'if=pflash,unit=0,format=raw,file=@0@'.format(ovmf_file), '-drive', 'file=@0@,if=none,id=hdd0'.format(iso_output), '-serial', 'stdio',
                              ],
                              input : iso_cmd,
                              depends : iso_cmd,
                              install : false,
  )
endif

test('Run-HorizonOS-Debug', debug_cmd)
test('Run-HorizonOS-NoDbg', nodebug_cmd)
