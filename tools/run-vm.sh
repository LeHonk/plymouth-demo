#!/bin/sh
# Install the current source tree into a throwaway overlay of the VM image,
# build it there, make it the default theme, and boot through GRUB/systemd.
#
# usage: run-vm.sh <vm-dir>
# env:   DEMO_NAME, HOLD_SECONDS (keep the splash up this long before
#        plymouth quits; default 15), QEMU_OPTS
set -eu

DIR=$1
BASE=$DIR/base.qcow2
RUN=$DIR/run.qcow2
STAGE=$DIR/stage
REPO=$(cd "$(dirname "$0")/.." && pwd)
HOLD=${HOLD_SECONDS:-15}

[ -e "$BASE" ] || { echo "no VM image; run 'make vm' first" >&2; exit 1; }

# Fresh copy-on-write overlay each run
rm -f "$RUN"
qemu-img create -q -f qcow2 -b "$(realpath "$BASE")" -F qcow2 "$RUN"

# Stage the working tree (uncommitted changes included, build/ excluded)
rm -rf "$STAGE" && mkdir -p "$STAGE/plymouth-demo"
tar -C "$REPO" --exclude=./build --exclude=./.git -cf - . | tar -C "$STAGE/plymouth-demo" -xf -

mkdir -p "$STAGE/hold"
cat > "$STAGE/hold/hold.conf" <<EOF
[Service]
ExecStartPre=/bin/sleep $HOLD
EOF

virt-customize -a "$RUN" \
        --copy-in "$STAGE/plymouth-demo:/root" \
        --run-command "make -C /root/plymouth-demo install" \
        --run-command "plymouth-set-default-theme -R $DEMO_NAME" \
        --mkdir /etc/systemd/system/plymouth-quit.service.d \
        --copy-in "$STAGE/hold/hold.conf:/etc/systemd/system/plymouth-quit.service.d"

ACCEL=
[ -w /dev/kvm ] && ACCEL="-enable-kvm -cpu host"

# shellcheck disable=SC2086
exec qemu-system-x86_64 $ACCEL -m 1024 -smp 2 \
        -name "plymouth demo (VM)" \
        -drive file="$RUN",if=virtio \
        -vga std -display gtk,zoom-to-fit=on \
        -serial mon:stdio \
        ${QEMU_OPTS:-}
