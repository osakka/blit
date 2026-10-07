#!/bin/sh
set -eu

b=/usr/bin/busybox
finish() {
    code=$1
    $b poweroff -f 2>/dev/null || $b halt -f 2>/dev/null || true
    exit "$code"
}

$b mkdir -p /sys /proc /tmp
$b mount -t sysfs sysfs /sys 2>/dev/null || true
$b mount -t proc proc /proc 2>/dev/null || true
$b mount -t configfs configfs /sys/kernel/config 2>/dev/null || true

for index in 0 1 2 3; do
    $b mkdir -p \
        "/sys/kernel/config/vkms/blit/planes/plane$index" \
        "/sys/kernel/config/vkms/blit/crtcs/crtc$index" \
        "/sys/kernel/config/vkms/blit/encoders/encoder$index" \
        "/sys/kernel/config/vkms/blit/connectors/connector$index"
    echo 1 > "/sys/kernel/config/vkms/blit/planes/plane$index/type"
    echo 1 > "/sys/kernel/config/vkms/blit/connectors/connector$index/status"
    $b ln -s "/sys/kernel/config/vkms/blit/crtcs/crtc$index" \
        "/sys/kernel/config/vkms/blit/planes/plane$index/possible_crtcs" 2>/dev/null || true
    $b ln -s "/sys/kernel/config/vkms/blit/crtcs/crtc$index" \
        "/sys/kernel/config/vkms/blit/encoders/encoder$index/possible_crtcs" 2>/dev/null || true
    $b ln -s "/sys/kernel/config/vkms/blit/encoders/encoder$index" \
        "/sys/kernel/config/vkms/blit/connectors/connector$index/possible_encoders" 2>/dev/null || true
done
echo 1 > /sys/kernel/config/vkms/blit/enabled

$b mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
$b mkdir -p /dev/pts /lib64 /usr/lib/x86_64-linux-gnu
$b mount -t devpts devpts /dev/pts 2>/dev/null || true
$b ifconfig lo 127.0.0.1 up 2>/dev/null || true
$b cp /blit-stage/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
$b cp /blit-stage/libc.so.6 /usr/lib/x86_64-linux-gnu/libc.so.6

export BLIT_DRM_DEVICE=/dev/dri/card0
export BLIT_KMS_TRACE_TIMING=1
export BLIT_KMS_TRACE_PHASES=1
export BLIT_PSF_FONT=/blit-stage/fonts/Blit-Hibr-Terminus16.psf
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=127.0.0.1 \
    /blit-stage/ld-linux-x86-64.so.2 \
    --library-path /blit-stage:/blit-stage/lib \
    /blit-stage/blit-server 19191 >/tmp/blit.log 2>&1 &

ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if $b wget -qO /tmp/server.json http://127.0.0.1:19191/v1/server; then
        ready=1
        break
    fi
    $b sleep 1
done
[ "$ready" -eq 1 ] || { $b cat /tmp/blit.log; finish 1; }
$b cat /tmp/server.json
$b wget -qO /tmp/displays.json http://127.0.0.1:19191/v1/displays
$b wget -qO /tmp/diagnostics.json http://127.0.0.1:19191/v1/diagnostics
$b cat /tmp/displays.json
$b cat /tmp/diagnostics.json
$b grep -q '"display_count":4' /tmp/server.json || finish 1
$b grep -q '"displays":4' /tmp/diagnostics.json || finish 1

display_ids=$($b tr ',' '\n' < /tmp/displays.json |
    $b sed -n 's/.*"id":"\(display-[0-9][0-9]*\)".*/\1/p')
session_count=0
for display_id in $display_ids; do
    $b wget -qO "/tmp/session-$session_count.json" \
        --post-data="{\"command\":\"/blit-stage/pty-input-echo\",\"arguments\":[\"--wide\"],\"surface_mode\":\"terminal\",\"display_id\":\"$display_id\",\"surface\":{\"width\":96,\"height\":32}}" \
        --header='Content-Type: application/json' \
        http://127.0.0.1:19191/v1/terminal-sessions || finish 1
    session_count=$((session_count + 1))
done
[ "$session_count" -eq 4 ] || finish 1
$b sleep 4
$b wget -qO /tmp/diagnostics-final.json http://127.0.0.1:19191/v1/diagnostics
$b cat /tmp/diagnostics-final.json
$b grep -q '"sessions":4' /tmp/diagnostics-final.json || finish 1
$b grep -q 'KMS present views=4 outputs=4' /tmp/blit.log || finish 1
$b cat /tmp/blit.log
echo KMS_FOUR_OUTPUT_PASS
finish 0
