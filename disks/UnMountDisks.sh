sudo umount /mnt/boot
sudo umount /mnt/horizonos
sudo umount /mnt/data

sudo qemu-nbd --disconnect /dev/nbd0

sudo umount /mnt/usb

sudo qemu-nbd --disconnect /dev/nbd1