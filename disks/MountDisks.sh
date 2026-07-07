sudo modprobe nbd max_part=16

sudo qemu-nbd --connect=/dev/nbd0 nvme_disk.qcow2

sudo mkdir /mnt/boot
sudo mkdir /mnt/horizonos
sudo mkdir /mnt/data

sudo mount /dev/nbd0p1 /mnt/boot
sudo mount /dev/nbd0p2 /mnt/horizonos
sudo mount /dev/nbd0p3 /mnt/data

sudo qemu-nbd --connect=/dev/nbd1 usb_stick.qcow2

sudo mkdir /mnt/usb

sudo mount /dev/nbd1p1 /mnt/usb