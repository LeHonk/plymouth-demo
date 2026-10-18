#!/bin/sh
# Build a lightweight Debian live image for PXE boot with the demo as the
# boot splash. Rootless: uses mmdebstrap --mode=unshare.
#
#   1. a throwaway build chroot (compiler + plymouth -dev packages) compiles
#      the plugin against the target suite's plymouth and installs it into
#      a staging dir; only those files leave the chroot
#   2. the image: minbase + kernel + systemd + live-boot + plymouth, no
#      toolchain, no docs/man pages/locales, no kernel modules a diskless
#      demo box doesn't need, and a netboot-sized initrd
#
# Output in <out-dir>: vmlinuz, initrd.img, filesystem.squashfs, boot.ipxe
#
# usage: mkpxe.sh <work-dir>
# env:   DEMO_NAME, PXE_SUITE (trixie), PXE_MIRROR, PXE_URL (where the
#        files will be served, for boot.ipxe)
#        PXE_INITRD_MODULES: initramfs-tools MODULES= (default netboot:
#          network drivers only; the splash uses the kernel's built-in
#          EFI/VESA framebuffer, GPU drivers load later from the root)
#        PXE_PRUNE_MODULES: module subtrees (under kernel/) removed from
#          the image and its initrd; set to "" to keep every module. The
#          default drops hardware a diskless demo box doesn't use, plus the
#          GPU drivers that need firmware packages the image doesn't ship
#          (loaded without firmware they kick out the EFI framebuffer and
#          leave the screen black)
set -eu

WORK=$1
OUT=$WORK/out
CACHE=$WORK/apt-cache
REPO=$(cd "$(dirname "$0")/.." && pwd)
SUITE=${PXE_SUITE:-trixie}
MIRROR=${PXE_MIRROR:-http://deb.debian.org/debian}
URL=${PXE_URL:-http://10.0.2.2:8080}
MODULES=${PXE_INITRD_MODULES:-netboot}
PRUNE=${PXE_PRUNE_MODULES-drivers/net/wireless net/wireless net/mac80211 net/bluetooth drivers/bluetooth drivers/media sound drivers/infiniband drivers/iio drivers/staging drivers/isdn drivers/gpu/drm/amd drivers/gpu/drm/radeon drivers/gpu/drm/nouveau drivers/gpu/drm/xe}

for tool in mmdebstrap mksquashfs; do
        command -v $tool >/dev/null || { echo "missing $tool (apt install mmdebstrap squashfs-tools)" >&2; exit 1; }
done

mkdir -p "$WORK" "$OUT" "$CACHE"
WORK=$(cd "$WORK" && pwd)
OUT=$WORK/out
CACHE=$WORK/apt-cache

# Share downloaded .debs between runs and between the two chroots
CACHE_IN="sync-in $CACHE /var/cache/apt/archives/"
CACHE_OUT="sync-out /var/cache/apt/archives $CACHE"

# Stage the working tree (uncommitted changes included, no build output)
rm -rf "$WORK/src" "$WORK/payload" "$WORK/overlay"
mkdir -p "$WORK/src" "$WORK/payload"
tar -C "$REPO" --exclude=./build --exclude=./.git -cf - . | tar -C "$WORK/src" -xf -

# Files added to the image as-is
umask 022
O=$WORK/overlay
mkdir -p "$O/etc/plymouth" "$O/etc/systemd/network" "$O/etc/initramfs-tools/hooks"
printf '[Daemon]\nTheme=%s\nShowDelay=0\n' "$DEMO_NAME" > "$O/etc/plymouth/plymouthd.conf"
printf '[Match]\nName=en* eth*\n\n[Network]\nDHCP=yes\n' > "$O/etc/systemd/network/20-dhcp.network"
echo demo-pxe > "$O/etc/hostname"
cat > "$O/etc/initramfs-tools/hooks/zz-demo-slim" <<'EOF'
#!/bin/sh
# Keep the initrd small for slow PXE links: udev's hardware database
# (~13 MB) only refines device names and isn't needed before the root
# is up, where the full copy lives.
[ "$1" = prereqs ] && exit 0
rm -f "$DESTDIR/usr/lib/udev/hwdb.bin" "$DESTDIR/etc/udev/hwdb.bin"
EOF
chmod 0755 "$O/etc/initramfs-tools/hooks/zz-demo-slim"

echo "== [1/2] building the plugin in a $SUITE build chroot"
mmdebstrap --mode=unshare --variant=apt --skip=essential/unlink \
        --setup-hook='mkdir -p "$1"/var/cache/apt/archives' \
        --setup-hook="$CACHE_IN" --customize-hook="$CACHE_OUT" \
        --include=build-essential,pkg-config,libplymouth-dev,libxkbcommon-dev,libevdev-dev,libudev-dev \
        --customize-hook='mkdir -p "$1/root/src"' \
        --customize-hook="sync-in $WORK/src /root/src" \
        --customize-hook='chroot "$1" make -C /root/src install DESTDIR=/root/out' \
        --customize-hook="sync-out /root/out $WORK/payload" \
        "$SUITE" /dev/null "$MIRROR"

# sync-in keeps the outside owner (you), so remember every injected path
# and hand it to root inside the image
(cd "$WORK/payload" && find . -mindepth 1; cd "$O" && find . -mindepth 1) |
        sed 's|^\./|/|' | sort -u > "$WORK/injected.list"

echo "== [2/2] building the image"
# Kept deliberately small: no recommends (mmdebstrap default), no docs.
# fontconfig is only there because Debian's plymouth initramfs hook needs
# it for any graphical theme.
# xz-utils compresses the initrd (smallest of the formats the kernel reads).
PACKAGES=linux-image-amd64,systemd-sysv,udev,live-boot,busybox,plymouth,fontconfig,iproute2,kmod,xz-utils

rm -f "$WORK/.failed" "$OUT"/vmlinuz "$OUT"/initrd.img
{ mmdebstrap --mode=unshare --variant=minbase --skip=essential/unlink \
        --setup-hook='mkdir -p "$1"/var/cache/apt/archives' \
        --setup-hook="$CACHE_IN" --customize-hook="$CACHE_OUT" \
        --include="$PACKAGES" \
        --dpkgopt='path-exclude=/usr/share/man/*' \
        --dpkgopt='path-exclude=/usr/share/doc/*' \
        --dpkgopt='path-include=/usr/share/doc/*/copyright' \
        --dpkgopt='path-exclude=/usr/share/locale/*' \
        --dpkgopt='path-include=/usr/share/locale/locale.alias' \
        --dpkgopt='path-exclude=/usr/share/info/*' \
        --customize-hook="sync-in $WORK/payload /" \
        --customize-hook="sync-in $O /" \
        --customize-hook="upload $WORK/injected.list /tmp/injected.list" \
        --customize-hook='while read -r p; do chown -h 0:0 "$1$p" && chmod go-w "$1$p"; done < "$1/tmp/injected.list"; rm "$1/tmp/injected.list"' \
        --customize-hook="sed -i -e 's/^MODULES=.*/MODULES=$MODULES/' -e 's/^COMPRESS=.*/COMPRESS=xz/' \"\$1/etc/initramfs-tools/initramfs.conf\"" \
        --customize-hook='printf "bochs\nvirtio_gpu\n" >> "$1/etc/initramfs-tools/modules"' \
        --customize-hook="for d in $PRUNE; do rm -rf \"\$1\"/usr/lib/modules/*/kernel/\$d; done" \
        --customize-hook='for k in "$1"/usr/lib/modules/*; do chroot "$1" depmod -a "${k##*/}"; done' \
        --customize-hook='chroot "$1" passwd -d root' \
        --customize-hook='chroot "$1" systemctl enable systemd-networkd' \
        --customize-hook='chroot "$1" update-initramfs -u -k all' \
        --customize-hook='cp -L "$1"/boot/vmlinuz-* "$1/boot/vmlinuz" && cp -L "$1"/boot/initrd.img-* "$1/boot/initrd.img"' \
        --customize-hook="download /boot/vmlinuz $OUT/vmlinuz" \
        --customize-hook="download /boot/initrd.img $OUT/initrd.img" \
        --customize-hook='rm -rf "$1"/boot/* "$1"/var/lib/apt/lists/* "$1"/var/cache/apt/archives/*' \
        --customize-hook='mkdir -p "$1"/var/cache/apt/archives/partial && chown -R 0:0 "$1"/var/cache/apt/archives' \
        "$SUITE" - "$MIRROR" || touch "$WORK/.failed"; } |
        mksquashfs - "$OUT/filesystem.squashfs" -tar -noappend -quiet -comp zstd -Xcompression-level 19 -b 1M \
                -root-uid 0 -root-gid 0 -root-mode 0755
# POSIX sh has no pipefail: catch an mmdebstrap failure on the left side
if [ -e "$WORK/.failed" ]; then
        rm -f "$WORK/.failed" "$OUT/filesystem.squashfs"
        echo "image build failed" >&2
        exit 1
fi

cat > "$OUT/boot.ipxe" <<EOF
#!ipxe
# Serve vmlinuz, initrd.img and filesystem.squashfs from \${base}.
set base $URL
dhcp
kernel \${base}/vmlinuz boot=live fetch=\${base}/filesystem.squashfs ip=dhcp quiet splash plymouth.ignore-serial-consoles
initrd \${base}/initrd.img
boot
EOF

echo "== done: $OUT"
ls -lh "$OUT"
