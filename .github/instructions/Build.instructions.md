---
applyTo: '**'
description: 'description'
---
To build the project, you have to go into the build director and then run this command: meson compile HorizonOS_ISO -j 10

To connect via gdb:
- gdb /home/diamantino/Projects/horizonos/iso/x86_64/Debug/boot/HorizonOS/HorizonOS_Kernel
- target remote localhost:1234
- use hb (hardware breakpoint) instead of b (breakpoint) to set breakpoints in the kernel, otherwise it will not work because of kvm

If there is no qemu available, start it like this: qemu-system-x86_64 -cpu host,+hypervisor,+invtsc,+tsc-deadline -device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd -device usb-mouse,id=usbmouse -smp 2 -M q35 -m 8G -accel kvm -drive if=pflash,unit=0,format=raw,file=/home/diamantino/Projects/horizonos/deps/ovmf/x86_64/OVMF.fd -cdrom /home/diamantino/Projects/horizonos/iso/out/HorizonOS-x86_64.iso -no-shutdown -no-reboot -debugcon stdio -serial file:/home/diamantino/Serial.txt -s -S