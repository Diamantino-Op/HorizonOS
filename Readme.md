# HorizonOS
![Build LLVM](https://github.com/Diamantino-Op/HorizonOS/actions/workflows/Setup-Deps.yml/badge.svg) ![Build X86_64](https://github.com/Diamantino-Op/HorizonOS/actions/workflows/Build-x86_64.yml/badge.svg) ![Build Aarch64](https://github.com/Diamantino-Op/HorizonOS/actions/workflows/Build-aarch64.yml/badge.svg) ![Build RiscV64](https://github.com/Diamantino-Op/HorizonOS/actions/workflows/Build-riscv64.yml/badge.svg)

<p align="center">
    <img src="res/Images/HorizonOS%20Logo.svg" alt="HorizonOS Logo" />
</p>

A simple microkernel OS that I am making for fun.

## Features
* **Modular:** Easy to add / remove modules such as drivers.
* **Simple:** Code is very simple to understand + I plan to document it soon.
* **Modern:** 64-bit os written using C++ 26 and the latest clang compiler.
* **Multi-Arch:** Currently only x86_64 is supported, but I plan to add support for Riscv64 and AArch64.
* **Hotplug:** Cpu, Memory and PCI Hotplug / Unplug support (WIP).

## IRQ Mappings
* **IRQ 0:** PIT Tick
* **IRQ 9:** SCI
* **IRQ 10:** Hpet (Scheduler sleep)

# Building:
Accepted `<arch>` values: (x86_64, riscv64, aarch64).
Currently only x86_64 is supported, but I plan to add support for Riscv64 and AArch64 in the future.

1. Clone the repository with `--recurse-submodules` flag.
2. Install: xorriso, rsync, qemu, cmake, meson.
3. Build the toolchain: sh deps/build_llvm_libc.sh build-llvm `<arch>` horizonos_llvm. 
4. Get the linux headers: sh deps/setup_linux_headers.sh `<arch>`
5. Build mlibc and libc++: sh deps/build_llvm_libc.sh build-libc `<arch>` horizonos_llvm release
6. Setup dependencies: sh deps/setup_deps.sh trunk trunk trunk v11.x-binary trunk master
7. Setup meson: meson setup --cross-file res/`<arch>`/horizon-cross.cfg -Dqemu_targets=false -Ddebug_build=false build
8. Build the OS: meson compile -C build -j 10

# Run Commands:

## Qemu:
**Kvm:** qemu-system-x86_64 -cpu host,+hypervisor,+invtsc,+tsc-deadline -device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd -device usb-mouse,id=usbmouse -smp 10 -M q35 -m 8G -accel kvm -drive if=pflash,format=raw,readonly=on,file=HorizonOS/deps/ovmf/x86_64/OVMF.fd -drive if=pflash,format=raw,file=HorizonOS/deps/ovmf/x86_64/OVMF_VARS.fd -cdrom HorizonOS/iso/out/HorizonOS-x86_64.iso -serial stdio

**Debug:** qemu-system-x86_64 -s -S -cpu host,+hypervisor,+invtsc,+tsc-deadline -device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd -device usb-mouse,id=usbmouse -smp 10 -M q35 -m 8G -drive if=pflash,format=raw,readonly=on,file=HorizonOS/deps/ovmf/x86_64/OVMF.fd -drive if=pflash,format=raw,file=HorizonOS/deps/ovmf/x86_64/OVMF_VARS.fd -cdrom HorizonOS/iso/out/HorizonOS-x86_64.iso -serial stdio

**Profile Flags:** -serial file:mcount.log
