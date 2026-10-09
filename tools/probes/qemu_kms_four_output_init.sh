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
if ! $b grep -q '"display_count":4' /tmp/server.json || ! $b grep -q '"displays":4' /tmp/diagnostics.json; then
    $b echo 'FOUR_OUTPUT_FIXTURE_ERROR: expected four displays.' >&2
    $b echo 'FOUR_OUTPUT_FIXTURE_ERROR: boot with vkms.create_default_dev=0 so Blit selects the configfs VKMS card.' >&2
    $b cat /tmp/server.json >&2
    $b cat /tmp/diagnostics.json >&2
    $b cat /tmp/blit.log >&2
    finish 1
fi

display_ids=$($b tr ',' '\n' < /tmp/displays.json |
    $b sed -n 's/.*"id":"\(display-[0-9][0-9]*\)".*/\1/p')
display1=$(echo "$display_ids" | $b sed -n '1p')
display2=$(echo "$display_ids" | $b sed -n '2p')
display3=$(echo "$display_ids" | $b sed -n '3p')
display4=$(echo "$display_ids" | $b sed -n '4p')
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.hibr_four=1 "*)
        hibr_surface_width=128
        hibr_surface_height=48
        hibr_tall=0
        case " $($b cat /proc/cmdline 2>/dev/null || true) " in
            *" blit.hibr_tall=1 "*)
                # Diagnostic-only geometry mode. The ordinary four-output
                # acceptance fixture remains 128x48; this mode deliberately
                # exercises the tall-pane lifecycle seen on the physical
                # host and reports row coverage separately.
                hibr_surface_width=240
                hibr_surface_height=135
                hibr_tall=1
                ;;
        esac
        export HIBR_MODPATH=/blit-stage/mods
        export HIBR_RC=/blit-stage/blit-console.hibr
        export BLIT_SERVER_HOST=127.0.0.1
        export BLIT_SERVER_PORT=19191
        export TERM=xterm-256color
        export TMPDIR=/tmp
        export HOME=/root
        export XDG_STATE_HOME=/tmp/hibr-state
        export XDG_CONFIG_HOME=/tmp/hibr-config
        export XDG_DATA_HOME=/tmp/hibr-data
        $b mkdir -p /root /tmp/hibr-state /tmp/hibr-config /tmp/hibr-data /tmp/hibr-ctl-0
        $b ln -sf pts/ptmx /dev/ptmx
        $b wget -qO /tmp/hibr-primary.json \
            --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"/blit-stage/blit-session.hibr\",\"--session\",\"blit\",\"--name\",\"$display1\"],\"environment\":{\"CP_THEME\":\"retro_car\",\"DT_CTL\":\"1\",\"DT_CTLPATH\":\"/tmp/hibr-ctl-0/blit.ctl\",\"DT_LOG\":\"/tmp/primary-hibr.log\"},\"surface_mode\":\"terminal\",\"display_id\":\"$display1\",\"surface\":{\"width\":$hibr_surface_width,\"height\":$hibr_surface_height}}" \
            --header='Content-Type: application/json' \
            http://127.0.0.1:19191/v1/terminal-sessions || finish 1
        primary_script=/blit-stage/blit-session.hibr
        for index in 2 3 4; do
            display_id=$(echo "$display_ids" | $b sed -n "${index}p")
            $b wget -qO "/tmp/hibr-join-$index.json" \
                --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$primary_script\",\"--session\",\"blit\",\"--join\",\"--name\",\"$display_id\"],\"environment\":{\"DT_LOG\":\"/tmp/join-$index-hibr.log\"},\"surface_mode\":\"terminal\",\"display_id\":\"$display_id\",\"surface\":{\"width\":$hibr_surface_width,\"height\":$hibr_surface_height}}" \
                --header='Content-Type: application/json' \
                http://127.0.0.1:19191/v1/terminal-sessions || finish 1
        done
        $b sleep 12
        $b wget -qO /tmp/hibr-four-final.json http://127.0.0.1:19191/v1/diagnostics
        $b cat /tmp/hibr-four-final.json
        $b grep -q '"displays":4' /tmp/hibr-four-final.json || finish 1
        $b grep -q '"sessions":4' /tmp/hibr-four-final.json || finish 1
        $b grep -q '"surfaces":4' /tmp/hibr-four-final.json || finish 1
        $b grep -q 'KMS present views=4 outputs=4' /tmp/blit.log || finish 1
        if [ "$hibr_tall" -eq 1 ]; then
            # A complete tall frame must contain at least one styled cell on
            # its final row. Absence is an explicit Hibr held-session boundary
            # diagnostic, not a Blit four-output pass.
            for session_file in /tmp/hibr-primary.json /tmp/hibr-join-2.json \
                /tmp/hibr-join-3.json /tmp/hibr-join-4.json; do
                surface_id=$($b sed -n 's/.*"surface_id":"\([^"]*\)".*/\1/p' "$session_file")
                $b wget -qO "/tmp/${surface_id}-cells.json" \
                    "http://127.0.0.1:19191/v1/surfaces/$surface_id/cells" || finish 1
                if ! $b grep -q '"y":134' "/tmp/${surface_id}-cells.json"; then
                    echo KMS_FOUR_HIBR_TALL_HIBR_BOUNDARY_OPEN
                    finish 77
                fi
            done
            echo KMS_FOUR_HIBR_TALL_PASS
        fi
        $b cat /tmp/blit.log
        echo KMS_FOUR_HIBR_PASS
        finish 0
        ;;
esac
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
for display_index in 0 1 2 3; do
    display_id=$(echo "$display_ids" | $b sed -n "$((display_index + 1))p")
    case "$display_index" in
        0) color='#ff0000' ;;
        1) color='#00ff00' ;;
        2) color='#0000ff' ;;
        3) color='#ffff00' ;;
    esac
    $b wget -qO "/tmp/overlay-$display_index.json" \
        --post-data="{\"display_id\":\"$display_id\",\"x\":16,\"y\":16,\"width\":32,\"height\":32,\"color\":\"$color\",\"opacity\":100}" \
        --header='Content-Type: application/json' \
        http://127.0.0.1:19191/v1/overlays || finish 1
done
$b sleep 2
for display_index in 0 1 2 3; do
    display_id=$(echo "$display_ids" | $b sed -n "$((display_index + 1))p")
    $b wget -qO "/tmp/display-$display_index.bmp" \
        "http://127.0.0.1:19191/v1/diagnostics/screenshots/$display_id" || finish 1
done
/blit-stage/bmp-pixel-assert /tmp/display-0.bmp 24 24 255 0 0 || finish 1
/blit-stage/bmp-pixel-assert /tmp/display-1.bmp 24 24 0 255 0 || finish 1
/blit-stage/bmp-pixel-assert /tmp/display-2.bmp 24 24 0 0 255 || finish 1
/blit-stage/bmp-pixel-assert /tmp/display-3.bmp 24 24 255 255 0 || finish 1
echo FOUR_OUTPUT_INDEPENDENT_PIXEL_PASS
$b cat /tmp/blit.log
echo KMS_FOUR_OUTPUT_PASS
finish 0
