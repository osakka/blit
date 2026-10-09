#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
kernel=${BLIT_QEMU_KERNEL:-/tmp/linux-vkms-build/arch/x86/boot/bzImage}
initramfs=${1:?usage: qemu_hibr_lifecycle_gate.sh INITRAMFS}
timeout_seconds=${BLIT_QEMU_TIMEOUT:-120}
owned_log=0
if [ -n "${BLIT_QEMU_LOG:-}" ]; then
	log=$BLIT_QEMU_LOG
else
	log=$(mktemp "${TMPDIR:-/tmp}/blit-qemu-hibr.XXXXXX")
	owned_log=1
fi

cleanup() {
	if [ "$owned_log" -eq 1 ]; then
		rm -f "$log"
	fi
}
trap cleanup EXIT INT TERM

[ -r "$kernel" ] || {
	echo "QEMU_HIBR_LIFECYCLE_FAIL missing_kernel=$kernel" >&2
	exit 77
}
[ -r "$initramfs" ] || {
	echo "QEMU_HIBR_LIFECYCLE_FAIL missing_initramfs=$initramfs" >&2
	exit 77
}

set +e
timeout "$timeout_seconds" qemu-system-x86_64 \
	-M q35 -m 1024 -smp 2 \
	-kernel "$kernel" -initrd "$initramfs" \
	-append 'console=ttyS0 init=/init vkms.create_default_dev=0 blit.hibr_panes=1 blit.hibr_restart=1' \
	-display none -serial stdio -monitor none -no-reboot \
	>"$log" 2>&1
qemu_status=$?
set -e

if [ "$qemu_status" -ne 0 ]; then
	echo "QEMU_HIBR_LIFECYCLE_FAIL qemu_exit=$qemu_status log=$log" >&2
	tail -80 "$log" >&2
	exit 1
fi

for marker in \
	LAYOUT_MATRIX_PASS \
	HIBR_PRIMARY_RESTART_PASS \
	HIBR_JOIN_RECONNECT_PASS \
	HIBR_PANE_MATRIX_PASS; do
	if ! grep -F -q "$marker" "$log"; then
		echo "QEMU_HIBR_LIFECYCLE_FAIL missing=$marker log=$log" >&2
		rg -n 'HIBR_|LAYOUT_|FAIL|PASS|qemu_exit' "$log" >&2 || true
		exit 1
	fi
done

if grep -F -q 'HIBR_PRIMARY_RESTART_FAIL' "$log" ||
	grep -F -q 'HIBR_JOIN_RECONNECT_FAIL' "$log" ||
	grep -F -q 'HIBR_PANE_MATRIX_FAIL' "$log"; then
	echo "QEMU_HIBR_LIFECYCLE_FAIL contradictory_marker log=$log" >&2
	exit 1
fi

echo "QEMU_HIBR_LIFECYCLE_PASS qemu_exit=0 markers=4"
