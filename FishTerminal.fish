#!/usr/bin/env fish

# Set the name for the perf data and flamegraph output
set perfdata qemu_perf.data
set folded out.folded
set flamegraph flamegraph.svg

# Clone these tools only if not already present
if not test -d FlameGraph
    git clone https://github.com/brendangregg/FlameGraph.git
end

# Path to FlameGraph scripts
set flamegraph_dir (pwd)/FlameGraph

# Start QEMU in the background, writing its PID to a temp file
set pidfile (mktemp)
qemu-system-x86_64 -cpu host,+hypervisor,+invtsc,+tsc-deadline -device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd -device usb-mouse,id=usbmouse -smp 10 -M q35 -m 8G -accel kvm -drive if=pflash,unit=0,format=raw,file=/home/diamantino/Projects/HorizonOS/deps/ovmf/x86_64/OVMF.fd -cdrom /home/diamantino/Projects/HorizonOS/iso/out/HorizonOS-x86_64.iso -serial stdio $argv --pidfile $pidfile &
set qemu_pid (cat $pidfile)
rm $pidfile

# Wait briefly to let QEMU start
sleep 1

echo "QEMU running as PID $qemu_pid"

# Start perf on QEMU, kill perf when QEMU dies
sudo perf record -F 99 -g -p $qemu_pid -o $perfdata &
set perf_pid $perf_pid

# Wait for QEMU to exit
wait $qemu_pid

# Stop perf (unless it already stopped)
kill $perf_pid ^/dev/null

echo "QEMU stopped, generating flamegraph..."

sudo perf script -i $perfdata | $flamegraph_dir/stackcollapse-perf.pl > $folded
$flamegraph_dir/flamegraph.pl $folded > $flamegraph

echo "Open $flamegraph in your browser."