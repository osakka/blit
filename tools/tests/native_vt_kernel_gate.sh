#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
kernel=${BLIT_QEMU_KERNEL:-}
busybox=${BLIT_QEMU_BUSYBOX:-}
modules=${BLIT_QEMU_MODULES:-}
if [ ! -r "$kernel" ] || [ ! -x "$busybox" ] ||
   [ ! -r "$modules/kernel/drivers/input/evdev.ko" ] ||
   [ ! -r "$modules/kernel/drivers/input/misc/uinput.ko" ]; then
    echo 'VT_KERNEL_SKIP needs BLIT_QEMU_KERNEL, static BLIT_QEMU_BUSYBOX, BLIT_QEMU_MODULES (matching kernel module tree)'
    exit 77
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-vt-kernel.XXXXXX")
mkdir -p "$work/root/bin" "$work/root/proc" "$work/root/sys" "$work/root/dev"
tcc -std=c99 -O2 "$root/tools/tests/native_vt_kernel_gate.c" \
    "$root/src/blit_vt.c" -o "$work/root/vt-kernel-gate"
cp "$busybox" "$work/root/bin/busybox"
ln -s busybox "$work/root/bin/sh"
cp "$root/tools/probes/qemu_vt_kernel_init.sh" "$work/root/init"
chmod +x "$work/root/init"
cp "$modules/kernel/drivers/input/evdev.ko" "$work/root/evdev.ko"
cp "$modules/kernel/drivers/input/misc/uinput.ko" "$work/root/uinput.ko"
executables="$work/root/vt-kernel-gate"
if [ "${BLIT_VT_SERVER_TEST:-0}" = 1 ]; then
    binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
    # An extracted package's /usr/bin/blit is an absolute symlink. Following
    # it here would test the HOST's installed binary, not the archive payload.
    if [ -L "$binary" ]; then
        echo 'VT_KERNEL_FAIL select the real server payload, not a symlink' >&2
        exit 1
    fi
    sha256sum "$binary"
    tcc -std=c99 -O2 "$root/tools/tests/native_vt_server_gate.c" \
        "$root/src/blit_vt.c" -o "$work/root/vt-server-gate"
    cp "$binary" "$work/root/blit-server"
    cp "$root/assets/fonts/Blit-Hibr-Terminus16.psf" "$work/root/font.psf"
    for module in drm drm_kms_helper drm_ttm_helper drm_vram_helper; do
        cp "$modules/kernel/drivers/gpu/drm/$module.ko" "$work/root/$module.ko"
    done
    cp "$modules/kernel/drivers/gpu/drm/ttm/ttm.ko" "$work/root/ttm.ko"
    cp "$modules/kernel/drivers/gpu/drm/tiny/bochs.ko" "$work/root/bochs.ko"
    executables="$executables $work/root/vt-server-gate $work/root/blit-server"
fi
for library in $(LD_LIBRARY_PATH="$root/build/sdl3-kms:$root/build/deps/prefix/lib/x86_64-linux-gnu" ldd $executables | awk '{if ($3 ~ /^\//) print $3; else if ($1 ~ /^\// && $1 !~ /:$/) print $1}' | sort -u); do
    mkdir -p "$work/root$(dirname "$library")"
    cp "$library" "$work/root$library"
    mkdir -p "$work/root/lib/x86_64-linux-gnu"
    cp "$library" "$work/root/lib/x86_64-linux-gnu/$(basename "$library")"
done
(cd "$work/root" && find . -print0 | cpio --null -o --format=newc > "$work/initramfs.cpio")
echo "VT_KERNEL_EVIDENCE=$work"
status=0
timeout 45s qemu-system-x86_64 -M q35 -m 256 -smp 1 \
    -kernel "$kernel" -initrd "$work/initramfs.cpio" \
    -append 'quiet loglevel=3 console=ttyS0 init=/init blit_vt_test=1 panic=-1' \
    -display none -serial stdio -monitor none -no-reboot > "$work/serial.log" 2>&1 || status=$?
cat "$work/serial.log"
test "$status" -eq 0
grep -q '^VT_KERNEL_PASS ' "$work/serial.log"
grep -q '^VT_KERNEL_EXIT=0' "$work/serial.log"
if grep -q 'VT_KERNEL_FAIL' "$work/serial.log"; then exit 1; fi
if [ "${BLIT_VT_SERVER_TEST:-0}" = 1 ]; then
    grep -q '^VT_SERVER_PASS ' "$work/serial.log"
    grep -q '^VT_SERVER_EXIT=0' "$work/serial.log"
fi
