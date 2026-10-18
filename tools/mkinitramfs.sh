#!/bin/sh
# Build a minimal initramfs that boots straight into plymouthd + the demo.
# Everything is copied from the host: busybox, plymouth and their libraries,
# and the DRM modules for QEMU's std VGA (bochs) from kernel $KVER.
#
# usage: mkinitramfs.sh <build-dir> <output.cpio.gz>
# env:   DEMO_NAME PLUGINDIR THEME_DIR RUN_SECONDS KVER
set -eu

BUILD=$1
OUT=$2
ROOT=$BUILD/initramfs
KVER=${KVER:-$(uname -r)}
PLUGINDIR=${PLUGINDIR%/}
HERE=$(cd "$(dirname "$0")" && pwd)

rm -rf "$ROOT"
mkdir -p "$ROOT"/usr/bin "$ROOT"/usr/sbin "$ROOT"/usr/lib "$ROOT"/usr/lib64 \
         "$ROOT"/etc/plymouth "$ROOT"/proc "$ROOT"/sys "$ROOT"/dev "$ROOT"/run \
         "$ROOT"/lib/modules/demo "$ROOT"/usr/share/plymouth/themes
# merged-/usr layout, same as the host
for d in bin sbin lib64; do ln -s usr/$d "$ROOT/$d"; done
mv "$ROOT/lib/modules" "$ROOT/usr/lib/" && rmdir "$ROOT/lib" && ln -s usr/lib "$ROOT/lib"

# Copy a file to the same path inside the root, resolving symlinks
put() {
        mkdir -p "$ROOT$(dirname "$2")"
        cp -L "$1" "$ROOT$2"
}

# Copy an ELF file plus every shared library it needs. Libraries all go
# into the default multiarch dir, since the host may resolve some through
# ld.so.conf paths (e.g. /opt/...) that the initramfs loader won't search.
LIBDIR=$(dirname "$(ldd /bin/sh | awk '/libc.so/ { print $3 }')")
put_elf() {
        put "$1" "${2:-$1}"
        ldd "$1" 2>/dev/null | awk '/=> \// { print $3 }' |
        while read -r lib; do
                dst=$LIBDIR/$(basename "$lib")
                [ -e "$ROOT$dst" ] || put "$lib" "$dst"
        done
        ldd "$1" 2>/dev/null | awk '/^\t\/.*ld-linux/ { print $1 }' |
        while read -r ld; do
                [ -e "$ROOT$ld" ] || put "$ld" "$ld"
        done
}

need() { command -v "$1" >/dev/null || { echo "missing $1 on host" >&2; exit 1; }; }
need busybox
need plymouthd
need cpio

# busybox + applets
put_elf "$(command -v busybox)" /usr/bin/busybox
for app in $(busybox --list); do
        [ -e "$ROOT/usr/bin/$app" ] || ln -s busybox "$ROOT/usr/bin/$app"
done

# plymouth
put_elf "$(command -v plymouthd)" /usr/sbin/plymouthd
put_elf "$(command -v plymouth)" /usr/bin/plymouth
for so in "$PLUGINDIR"/details.so "$PLUGINDIR"/text.so "$PLUGINDIR"/renderers/*.so; do
        [ -e "$so" ] && put_elf "$so"
done
put_elf "$BUILD/$DEMO_NAME.so" "$PLUGINDIR/$DEMO_NAME.so"
put /usr/share/plymouth/plymouthd.defaults /usr/share/plymouth/plymouthd.defaults
for t in details text; do
        [ -d /usr/share/plymouth/themes/$t ] && cp -r /usr/share/plymouth/themes/$t "$ROOT/usr/share/plymouth/themes/"
done

mkdir -p "$ROOT$THEME_DIR"
cp "$BUILD/$DEMO_NAME.plymouth" "$ROOT$THEME_DIR/"
cp -r "$HERE/../assets/." "$ROOT$THEME_DIR/"
rm -f "$ROOT$THEME_DIR/.gitkeep"

cat > "$ROOT/etc/plymouth/plymouthd.conf" <<EOF
[Daemon]
Theme=$DEMO_NAME
ShowDelay=0
DeviceTimeout=5
EOF

# Kernel modules for -vga std, in dependency order, decompressed so busybox
# insmod can load them
: > "$ROOT/etc/modules.order"
modprobe -S "$KVER" --show-depends bochs | awk '$1 == "insmod" { print $2 }' |
while read -r ko; do
        name=$(basename "$ko"); name=${name%.xz}; name=${name%.zst}; name=${name%.gz}
        case "$ko" in
        *.xz)  xz -dc "$ko" ;;
        *.zst) zstd -qdc "$ko" ;;
        *.gz)  gzip -dc "$ko" ;;
        *)     cat "$ko" ;;
        esac > "$ROOT/lib/modules/demo/$name"
        echo "$name" >> "$ROOT/etc/modules.order"
done

printf 'DEMO_NAME=%s\nRUN_SECONDS=%s\n' "$DEMO_NAME" "${RUN_SECONDS:-0}" > "$ROOT/etc/demo.conf"
install -m 0755 "$HERE/init.sh" "$ROOT/init"

(cd "$ROOT" && find . | cpio -o -H newc --quiet) | gzip -1 > "$OUT"
echo "initramfs: $OUT ($(du -h "$OUT" | cut -f1), kernel $KVER)"
