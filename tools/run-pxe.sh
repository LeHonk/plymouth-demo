#!/bin/sh
# Network-boot the PXE image in QEMU, the way a real client would:
# QEMU's iPXE ROM gets DHCP and boot.ipxe over TFTP from QEMU's user
# network, then fetches kernel, initrd and squashfs over HTTP from a
# server on this host (reachable from the guest as 10.0.2.2).
#
# usage: run-pxe.sh <out-dir>
# env:   PXE_PORT (8080), QEMU_OPTS
set -eu

OUT=$1
PORT=${PXE_PORT:-8080}

for f in vmlinuz initrd.img filesystem.squashfs; do
        [ -e "$OUT/$f" ] || { echo "missing $OUT/$f; run 'make pxe' first" >&2; exit 1; }
done

# boot.ipxe for the QEMU test network (the one in $OUT is for your server)
TFTP=$OUT/.qemu-tftp
mkdir -p "$TFTP"
sed "s|^set base .*|set base http://10.0.2.2:$PORT|" "$OUT/boot.ipxe" > "$TFTP/boot.ipxe"

python3 -m http.server --bind 127.0.0.1 --directory "$OUT" "$PORT" >/dev/null 2>&1 &
HTTP_PID=$!
trap 'kill $HTTP_PID 2>/dev/null' EXIT INT TERM

ACCEL=
[ -w /dev/kvm ] && ACCEL="-enable-kvm -cpu host"

# live-boot copies the squashfs into RAM, hence the memory size
# shellcheck disable=SC2086
qemu-system-x86_64 $ACCEL -m 2048 -smp 2 \
        -name "plymouth demo (PXE)" \
        -boot n -nic user,model=virtio-net-pci,tftp="$TFTP",bootfile=boot.ipxe \
        -vga std -display gtk,zoom-to-fit=on \
        -serial mon:stdio \
        ${QEMU_OPTS:-}
