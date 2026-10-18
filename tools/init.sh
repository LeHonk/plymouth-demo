#!/bin/sh
# /init of the QEMU test initramfs (busybox sh, POSIX only).
# Starts plymouthd with the demo theme, feeds it some fake boot events,
# then powers off after RUN_SECONDS (0 = run until the window is closed).

export PATH=/usr/sbin:/usr/bin:/sbin:/bin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /run/plymouth /var/log /tmp
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /run
mkdir -p /run/plymouth

. /etc/demo.conf

log() { echo "[init] $*" > /dev/ttyS0; }

for m in $(cat /etc/modules.order); do
        insmod "/lib/modules/demo/$m" 2>/dev/null || log "insmod $m failed"
done

i=0
while [ ! -e /dev/dri/card0 ] && [ $i -lt 50 ]; do
        sleep 0.1
        i=$((i + 1))
done
[ -e /dev/dri/card0 ] || log "no /dev/dri/card0, plymouth will fall back to fbdev/text"

# Debug logging is enabled from the kernel command line
# (plymouth.debug=stream:/dev/ttyS0, see run-qemu.sh)
plymouthd --mode=boot --pid-file=/run/plymouth/pid --attach-to-session --ignore-serial-consoles ||
        log "plymouthd failed to start"
plymouth show-splash
log "splash shown: theme $DEMO_NAME, running ${RUN_SECONDS}s (0 = forever)"

# demo.password on the kernel command line: test the LUKS-style prompt
if grep -q demo.password /proc/cmdline; then
        (sleep 3; plymouth ask-for-password --prompt="Please unlock disk sda3_crypt" > /dev/ttyS0; log "password entered") &
fi

# Fake boot: exercise the status/message/progress hooks
messages="Mounting filesystems
Loading kernel modules
Starting udev
Bringing up network
Starting display manager"

elapsed=0
while [ "$RUN_SECONDS" -eq 0 ] || [ $elapsed -lt "$RUN_SECONDS" ]; do
        n=$(echo "$messages" | wc -l)
        msg=$(echo "$messages" | sed -n "$((elapsed % n + 1))p")
        plymouth display-message --text="$msg"
        plymouth update --status="$msg"
        if [ "$RUN_SECONDS" -gt 0 ]; then
                plymouth system-update --progress=$((elapsed * 100 / RUN_SECONDS))
        else
                plymouth system-update --progress=$((elapsed * 5 % 101))
        fi
        sleep 1
        elapsed=$((elapsed + 1))
done

plymouth quit
log "done, powering off"
poweroff -f
