#!/bin/sh
# One-time: prepare a Debian VM image for `make run-vm`.
# Installs plymouth plus the toolchain so the plugin is built *inside* the
# VM against its own plymouth version (the plugin ABI is not stable).
#
# libguestfs appliance networking (passt) is blocked by Debian's AppArmor
# profile for passt, so the image is only edited offline. Packages are
# installed by booting the VM once with QEMU user networking: a one-shot
# service runs apt-get and powers the VM off.
#
# usage: mkvm.sh <vm-dir>
# env:   BASE_IMAGE=<existing Debian qcow2> to skip the download
#        IMAGE_URL to use a different Debian cloud image
# needs: libguestfs-tools (virt-customize), qemu-utils
set -eu

DIR=$1
BASE=$DIR/base.qcow2
TMP=$DIR/base.qcow2.tmp
STAGE=$DIR/stage-provision
IMAGE_URL=${IMAGE_URL:-https://cloud.debian.org/images/cloud/trixie/latest/debian-13-nocloud-amd64.qcow2}
# - libplymouth-dev headers include xkbcommon/libevdev, and its .pc file
#   requires their .pc files, but the package does not depend on them
# - Debian's plymouth initramfs hook copies fontconfig files for every
#   graphical theme and fails without fontconfig
PACKAGES="plymouth build-essential pkg-config libplymouth-dev libxkbcommon-dev libevdev-dev libudev-dev fontconfig"

command -v virt-customize >/dev/null || { echo "install libguestfs-tools first" >&2; exit 1; }
mkdir -p "$DIR"

if [ -e "$BASE" ]; then
        echo "$BASE already exists; delete it to rebuild"
        exit 0
fi

trap 'rm -f "$TMP"; rm -rf "$STAGE"' EXIT

if [ -n "${BASE_IMAGE:-}" ]; then
        cp --reflink=auto "$BASE_IMAGE" "$TMP"
else
        echo "Downloading $IMAGE_URL"
        wget -O "$TMP" "$IMAGE_URL"
fi

mkdir -p "$STAGE"
cat > "$STAGE/demo-provision" <<EOF
#!/bin/sh
set -e
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y $PACKAGES
touch /var/lib/demo-provisioned
EOF
cat > "$STAGE/demo-provision.service" <<'EOF'
[Unit]
Description=Install plymouth-demo build dependencies (first boot only)
Wants=network-online.target
After=network-online.target
ConditionPathExists=!/var/lib/demo-provisioned

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/demo-provision
ExecStartPost=/bin/systemctl --no-block poweroff
StandardOutput=journal+console

[Install]
WantedBy=multi-user.target
EOF

echo "Configuring image (offline)"
virt-customize --no-network -a "$TMP" \
        --hostname demo-vm --timezone UTC \
        --run-command "systemctl mask systemd-firstboot.service" \
        --run-command "passwd -d root" \
        --copy-in "$STAGE/demo-provision:/usr/local/sbin" \
        --chmod "0755:/usr/local/sbin/demo-provision" \
        --copy-in "$STAGE/demo-provision.service:/etc/systemd/system" \
        --link "/etc/systemd/system/demo-provision.service:/etc/systemd/system/multi-user.target.wants/demo-provision.service"

echo "Booting the VM once to install: $PACKAGES"
echo "(headless, takes a few minutes; it powers off when done)"
ACCEL=
[ -w /dev/kvm ] && ACCEL="-enable-kvm -cpu host"
# shellcheck disable=SC2086
timeout 1800 qemu-system-x86_64 $ACCEL -m 1024 -smp 2 -display none \
        -drive file="$TMP",if=virtio -nic user,model=virtio-net-pci \
        -serial file:"$DIR/provision.log" -no-reboot

virt-customize --no-network -a "$TMP" \
        --run-command "test -e /var/lib/demo-provisioned" \
        --run-command "dpkg -s libplymouth-dev >/dev/null" ||
        { echo "provisioning failed; see the VM journal" >&2; exit 1; }

mv "$TMP" "$BASE"
echo "VM image ready: $BASE (log in as root, no password)"
