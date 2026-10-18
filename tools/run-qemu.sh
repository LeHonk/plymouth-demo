#!/bin/sh
# Boot the host kernel with the demo initramfs in a QEMU window.
# Serial console (kernel + init log, plymouthd debug with DEBUG=1) goes
# to this terminal. Close the window or press Ctrl-A X here to quit.
#
# usage: run-qemu.sh <initramfs.cpio.gz>
# env:   KVER (kernel version, default: running kernel), DEBUG=1, QEMU_OPTS,
#        APPEND (extra kernel args, e.g. demo.password to test the prompt)
set -eu

INITRD=$1
KVER=${KVER:-$(uname -r)}
KERNEL=/boot/vmlinuz-$KVER

[ -r "$KERNEL" ] || { echo "cannot read $KERNEL" >&2; exit 1; }

CMDLINE="quiet splash plymouth.ignore-udev plymouth.ignore-serial-consoles console=ttyS0 loglevel=3 vt.global_cursor_default=0"
[ "${DEBUG:-0}" = 1 ] && CMDLINE="$CMDLINE plymouth.debug=stream:/dev/ttyS0"
CMDLINE="$CMDLINE ${APPEND:-}"

ACCEL=
[ -w /dev/kvm ] && ACCEL="-enable-kvm -cpu host"

# shellcheck disable=SC2086
exec qemu-system-x86_64 $ACCEL -m 512 -smp 2 \
        -name "plymouth demo" \
        -vga std -display gtk,zoom-to-fit=on \
        -kernel "$KERNEL" -initrd "$INITRD" -append "$CMDLINE" \
        -serial mon:stdio -no-reboot \
        ${QEMU_OPTS:-}
