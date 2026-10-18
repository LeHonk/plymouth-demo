#!/bin/sh
# One-time: prepare a Debian VM image for `make run-vm`.
# Installs plymouth plus the toolchain so the plugin is built *inside* the
# VM against its own plymouth version (the plugin ABI is not stable).
#
# usage: mkvm.sh <vm-dir>
# env:   BASE_IMAGE=<existing Debian qcow2> to skip the download
#        IMAGE_URL to use a different Debian cloud image
# needs: libguestfs-tools (virt-customize), qemu-utils
set -eu

DIR=$1
BASE=$DIR/base.qcow2
IMAGE_URL=${IMAGE_URL:-https://cloud.debian.org/images/cloud/trixie/latest/debian-13-nocloud-amd64.qcow2}

command -v virt-customize >/dev/null || { echo "install libguestfs-tools first" >&2; exit 1; }
mkdir -p "$DIR"

if [ -e "$BASE" ]; then
        echo "$BASE already exists; delete it to rebuild"
        exit 0
fi

if [ -n "${BASE_IMAGE:-}" ]; then
        cp --reflink=auto "$BASE_IMAGE" "$BASE.tmp"
else
        echo "Downloading $IMAGE_URL"
        wget -O "$BASE.tmp" "$IMAGE_URL"
fi

virt-customize -a "$BASE.tmp" \
        --install plymouth,build-essential,pkg-config,libplymouth-dev \
        --run-command "sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT=.*/GRUB_CMDLINE_LINUX_DEFAULT=\"quiet splash\"/; s/^GRUB_TIMEOUT=.*/GRUB_TIMEOUT=1/' /etc/default/grub" \
        --run-command "update-grub"

mv "$BASE.tmp" "$BASE"
echo "VM image ready: $BASE (log in as root, no password)"
