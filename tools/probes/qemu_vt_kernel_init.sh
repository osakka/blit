#!/bin/sh
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox mkdir -p /dev/pts /tmp /run
/bin/busybox mount -t devpts devpts /dev/pts
/bin/busybox insmod /evdev.ko
/bin/busybox insmod /uinput.ko
/vt-kernel-gate
result=$?
echo "VT_KERNEL_EXIT=$result"
if [ "$result" = 0 ] && [ -x /vt-server-gate ]; then
    /bin/busybox insmod /drm.ko
    /bin/busybox insmod /ttm.ko
    /bin/busybox insmod /drm_kms_helper.ko
    /bin/busybox insmod /drm_ttm_helper.ko
    /bin/busybox insmod /drm_vram_helper.ko
    /bin/busybox insmod /bochs.ko
    /bin/busybox ifconfig lo 127.0.0.1 up
    export LD_LIBRARY_PATH=/lib/x86_64-linux-gnu
    /vt-server-gate
    result=$?
    echo "VT_SERVER_EXIT=$result"
fi
/bin/busybox poweroff -f
