#!/bin/sh

qemu-system-x86_64 -cpu host,+hypervisor,+invtsc,+tsc-deadline -device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd -device usb-mouse,id=usbmouse -smp 10 -M q35 -m 8G -accel kvm -drive if=pflash,unit=0,format=raw,file=$1/deps/ovmf/x86_64/OVMF.fd -cdrom $1/iso/out/HorizonOS-x86_64.iso -serial stdio -no-reboot -no-shutdown -s -S