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
$b mkdir -p /sys/kernel/config/vkms/blit/planes/plane0 /sys/kernel/config/vkms/blit/planes/plane1 /sys/kernel/config/vkms/blit/crtcs/crtc0 /sys/kernel/config/vkms/blit/crtcs/crtc1 /sys/kernel/config/vkms/blit/encoders/encoder0 /sys/kernel/config/vkms/blit/encoders/encoder1 /sys/kernel/config/vkms/blit/connectors/connector0 /sys/kernel/config/vkms/blit/connectors/connector1
echo 1 > /sys/kernel/config/vkms/blit/planes/plane0/type
echo 1 > /sys/kernel/config/vkms/blit/planes/plane1/type
echo 1 > /sys/kernel/config/vkms/blit/connectors/connector0/status
echo 1 > /sys/kernel/config/vkms/blit/connectors/connector1/status
$b ln -s /sys/kernel/config/vkms/blit/crtcs/crtc0 /sys/kernel/config/vkms/blit/planes/plane0/possible_crtcs 2>/dev/null || true
$b ln -s /sys/kernel/config/vkms/blit/crtcs/crtc1 /sys/kernel/config/vkms/blit/planes/plane1/possible_crtcs 2>/dev/null || true
$b ln -s /sys/kernel/config/vkms/blit/crtcs/crtc0 /sys/kernel/config/vkms/blit/encoders/encoder0/possible_crtcs 2>/dev/null || true
$b ln -s /sys/kernel/config/vkms/blit/crtcs/crtc1 /sys/kernel/config/vkms/blit/encoders/encoder1/possible_crtcs 2>/dev/null || true
$b ln -s /sys/kernel/config/vkms/blit/encoders/encoder0 /sys/kernel/config/vkms/blit/connectors/connector0/possible_encoders 2>/dev/null || true
$b ln -s /sys/kernel/config/vkms/blit/encoders/encoder1 /sys/kernel/config/vkms/blit/connectors/connector1/possible_encoders 2>/dev/null || true
echo 1 > /sys/kernel/config/vkms/blit/enabled
$b mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
$b mkdir -p /dev/pts
$b mount -t devpts devpts /dev/pts -o gid=5,mode=620,ptmxmode=666 2>/dev/null || $b mount -t devpts devpts /dev/pts 2>/dev/null || true
$b ln -sf pts/ptmx /dev/ptmx
$b ls -l /dev/ptmx /dev/pts/ptmx 2>/dev/null || true
$b ifconfig lo 127.0.0.1 up 2>/dev/null || true
$b mkdir -p /tmp/guest-screenshots /tmp/host-screenshots
$b mount -t 9p -o trans=virtio,version=9p2000.L blitshots /tmp/host-screenshots 2>/dev/null || true

# Hibr is an ELF executable whose interpreter is the host glibc loader.  The
# minimal Alpine fixture intentionally has no host /lib64 tree, so install
# only the two required compatibility files before launching Hibr directly.
$b mkdir -p /lib64 /usr/lib/x86_64-linux-gnu
$b cp /blit-stage/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
$b cp /blit-stage/libc.so.6 /usr/lib/x86_64-linux-gnu/libc.so.6

export HIBR_MODPATH=/blit-stage/mods
export HIBR_RC=/blit-stage/blit-console.hibr
export BLIT_SERVER_HOST=127.0.0.1
export BLIT_SERVER_PORT=19191
if [ -e /dev/dri/card1 ]; then
    export BLIT_DRM_DEVICE=/dev/dri/card1
elif [ -e /dev/dri/card0 ]; then
    export BLIT_DRM_DEVICE=/dev/dri/card0
else
    export BLIT_DRM_DEVICE=/dev/dri/card0
fi
if [ -f /blit-stage/fonts/Blit-Hibr-Terminus16.psf ]; then
    export BLIT_PSF_FONT=/blit-stage/fonts/Blit-Hibr-Terminus16.psf
elif [ -f /blit-stage/fonts/Lat15-Terminus16.psf ]; then
    export BLIT_PSF_FONT=/blit-stage/fonts/Lat15-Terminus16.psf
fi
export TERM=xterm-256color
export BLIT_KMS_TRACE_PHASES=1
export TMPDIR=/tmp
export HOME=/root
export XDG_STATE_HOME=/tmp/hibr-state
export XDG_CONFIG_HOME=/tmp/hibr-config
export XDG_DATA_HOME=/tmp/hibr-data
export BLIT_KMS_SCREENSHOT_DIR=/tmp/guest-screenshots
export BLIT_KMS_SCREENSHOT_AFTER=30
export BLIT_KMS_TRACE_TIMING=1
$b mkdir -p /root /tmp/hibr-state /tmp/hibr-config /tmp/hibr-data
$b mkdir -p /tmp/hibr-ctl-0
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.input_soak=1 "*) BLIT_INPUT_ONLY_TEST=1; BLIT_INPUT_SOAK_TEST=1 ;;
    *" blit.input_only=1 "*) BLIT_INPUT_ONLY_TEST=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.input_loss=1 "*) BLIT_INPUT_ONLY_TEST=1; BLIT_INPUT_LOSS_TEST=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.hibr_panes=1 "*) BLIT_HIBR_PANES_TEST=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.hibr_restart=1 "*) BLIT_HIBR_PANES_TEST=1; BLIT_HIBR_RESTART_TEST=1 ;;
esac
BLIT_HIBR_PANE_WORKLOAD=/blit-stage/hibr_window_workload.hibr
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.hibr_stock=1 "*)
        BLIT_HIBR_PANE_WORKLOAD=/blit-stage/blit-stock-session.hibr
        $b ln -sf /blit-stage/hibr /usr/bin/hibr
        export HIBR_RC=/blit-stage/blit-hibr-console.hibr
        export DT_LOG=/tmp/hibr-stock.log
        ;;
esac
if [ "${BLIT_INPUT_ONLY_TEST:-0}" = 1 ]; then
    # The input-only gate measures routing and PTY bytes, not framebuffer
    # capture.  Avoid serializing two 1024x768 PPMs on the slow VKMS guest
    # while the synthetic pointer stress stream is running.
    unset BLIT_KMS_SCREENSHOT_DIR
    unset BLIT_KMS_SCREENSHOT_AFTER
    export BLIT_TRACE_KEYS=1
    /blit-stage/emit-uinput >/tmp/uinput.log 2>&1 &
    emit_pid=$!
    echo INPUT_EMITTER_PID=$emit_pid
    # uinput creates the keyboard and both synthetic mice asynchronously.
    # Let all device nodes settle before Blit performs its initial scan; the
    # runtime still retains reconnect scanning for hotplug after startup.
    $b sleep 1
    if [ "${BLIT_INPUT_LOSS_TEST:-0}" = 1 ]; then
        (
            $b sleep 14
            /blit-stage/emit-uinput --hold >/tmp/uinput-reconnect.log 2>&1
        ) &
        echo INPUT_RECONNECT_SCHEDULER_PID=$!
    fi
fi
# Hibr's desktop scripts invoke mkdir as an external command.  The minimal
# fixture uses BusyBox directly, so expose the applet under the name Hibr
# resolves instead of making the fixture fail before its control socket binds.
$b ln -sf /usr/bin/busybox /usr/bin/mkdir
if [ ! -f /blit-stage/lib/ctl.hibr ]; then
    echo HIBR_CONTROL_CLIENT_MISSING
    finish 1
fi
if [ ! -f /blit-stage/hibr_window_workload.hibr ]; then
    echo HIBR_WINDOW_WORKLOAD_MISSING
    finish 1
fi
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=127.0.0.1 /blit-stage/ld-linux-x86-64.so.2 --library-path /blit-stage:/blit-stage/lib /blit-stage/blit-server 19191 >/tmp/blit.log 2>&1 &
server_pid=$!
echo BLIT_SERVER_PID=$server_pid
ready=0
for n in 1 2 3 4 5 6 7 8 9 10; do
    if $b wget -qO /tmp/server.json http://127.0.0.1:19191/v1/server; then
        ready=1
        break
    fi
    $b sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo SERVER_RESPONSE; $b cat /tmp/server.json 2>/dev/null || true
    echo BLIT_LOG; $b cat /tmp/blit.log
    finish 1
fi
if [ "${BLIT_INPUT_ONLY_TEST:-0}" = 1 ]; then
    # Subscribe before topology/session setup; the synthetic producer can
    # begin emitting while the deterministic layout matrix is running.
    /blit-stage/ws-input-probe >/tmp/stream-input.log 2>&1 &
    stream_probe_pid=$!
    echo INPUT_STREAM_PROBE_PID=$stream_probe_pid
    stream_ready=0
    for stream_attempt in 1 2 3 4 5; do
        if $b grep -q 'subscription_received' /tmp/stream-input.log 2>/dev/null; then
            stream_ready=1
            break
        fi
        $b sleep 1
    done
    if [ "$stream_ready" -ne 1 ]; then
        echo BLIT_INPUT_STREAM_NOT_READY
        finish 1
    fi
fi
topology=$($b wget -qO- http://127.0.0.1:19191/v1/topology)
layout_matrix_pass=1
for layout in 1x1 2x1 1x2 2x2; do
    columns=$(echo "$layout" | $b cut -d x -f 1)
    rows=$(echo "$layout" | $b cut -d x -f 2)
    expected=$((columns * rows * 2))
    layout_response=$(/blit-stage/http-put 19191 /v1/topology "{\"layout\":{\"columns\":$columns,\"rows\":$rows}}")
    pane_count=$(echo "$layout_response" | $b tr ',' '\n' | $b sed -n 's/.*"id":"pane-[^"]*".*/x/p' | $b wc -l)
    echo LAYOUT_$layout; echo "$layout_response"
    if [ "$pane_count" -ne "$expected" ]; then
        layout_matrix_pass=0
    fi
done
topology=$(/blit-stage/http-put 19191 /v1/topology '{"layout":{"columns":1,"rows":1}}')
panes_cmd=$(echo "$topology" | $b tr ',' '\n' | $b sed -n 's/.*"id":"\(pane-[^"]*\)".*/\1/p')
pane1=$(echo "$panes_cmd" | $b sed -n '1p')
pane2=$(echo "$panes_cmd" | $b sed -n '2p')
display1=$(echo "$topology" | $b tr ',' '\n' | $b sed -n 's/.*"display_id":"\([^"]*\)".*/\1/p' | $b sed -n '1p')
display2=$(echo "$topology" | $b tr ',' '\n' | $b sed -n 's/.*"display_id":"\([^"]*\)".*/\1/p' | $b sed -n '2p')
echo TOPOLOGY; echo "$topology"
echo PANES; echo "pane1=$pane1 pane2=$pane2 display1=$display1 display2=$display2"

if [ "$layout_matrix_pass" -ne 1 ]; then
    echo LAYOUT_MATRIX_FAIL
    finish 1
fi
echo LAYOUT_MATRIX_PASS

if [ -z "$pane1" ] || [ -z "$pane2" ] || [ -z "$display1" ] || [ -z "$display2" ]; then
    echo MULTI_DISPLAY_FIXTURE_NOT_READY
    finish 1
fi

if [ "${BLIT_INPUT_ONLY_TEST:-0}" = 1 ]; then
    # Deterministic Blit-only input gate.  The PTY child enables SGR mouse
    # reporting and prints every received byte, so keyboard and pointer
    # routing are observable without involving Hibr.
    if [ ! -x /blit-stage/emit-uinput ] ||
       [ ! -x /blit-stage/pty-input-echo ] ||
       [ ! -x /blit-stage/ws-input-probe ]; then
        echo BLIT_INPUT_HELPERS_MISSING
        finish 1
    fi
    input_soak_args=''
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" = 1 ]; then input_soak_args=',"--soak"'; fi
    $b wget -qO /tmp/input-key.json --post-data="{\"command\":\"/blit-stage/pty-input-echo\",\"arguments\":[\"--key\"$input_soak_args],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane1\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    $b wget -qO /tmp/input-pointer.json --post-data="{\"command\":\"/blit-stage/pty-input-echo\",\"arguments\":[\"--mouse\",\"--focus\"$input_soak_args],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane2\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    input_key_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/input-key.json)
    input_pointer_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/input-pointer.json)
    /blit-stage/http-put 19191 /v1/input/routes/input-key "{\"device_id\":\"evdev-keyboard\",\"target_session_id\":\"session-$input_key_id\",\"enabled\":true}" > /tmp/input-key-route.json || true
    /blit-stage/http-put 19191 /v1/input/routes/input-pointer "{\"device_id\":\"evdev-pointer\",\"target_session_id\":\"session-$input_pointer_id\",\"enabled\":true}" > /tmp/input-pointer-route.json || true
    /blit-stage/http-put 19191 /v1/input/routes/input-gamepad "{\"device_id\":\"evdev-gamepad\",\"target_session_id\":\"session-$input_key_id\",\"enabled\":true}" > /tmp/input-gamepad-route.json || true
    input_devices=$($b wget -qO- http://127.0.0.1:19191/v1/input/devices || true)
    echo INPUT_DEVICES; echo "$input_devices"
    if echo "$input_devices" | $b grep -q '"gamepad"'; then
        echo BLIT_INPUT_GAMEPAD_DISCOVERY_PASS
    else
        echo BLIT_INPUT_GAMEPAD_DISCOVERY_FAIL
    fi
    echo BLIT_INPUT_READY
    $b cat /tmp/input-key.json; $b cat /tmp/input-pointer.json
    $b cat /tmp/input-key-route.json; $b cat /tmp/input-pointer-route.json
    $b cat /tmp/input-gamepad-route.json
    input_wait=30
    [ "${BLIT_INPUT_SOAK_TEST:-0}" = 1 ] && input_wait=12
    input_wait_arg=$($b cat /proc/cmdline 2>/dev/null | $b sed -n 's/.*blit.input_wait=\([0-9][0-9]*\).*/\1/p')
    [ -n "$input_wait_arg" ] && input_wait=$input_wait_arg
    $b sleep "$input_wait"
    echo BLIT_LOG_BEFORE_INPUT_PROBES
    $b cat /tmp/blit.log 2>/dev/null || true
    # The synthetic producer starts before the gateway and waits before
    # emitting events. Poll the observable PTY surfaces with a bounded
    # deadline instead of assuming a fixed sleep covers slow QEMU scheduling.
    input_probe_wait=20
    while [ "$input_probe_wait" -gt 0 ]; do
        key_probe=$($b timeout 3 $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-$input_key_id/cells || true)
        pointer_probe=$($b timeout 3 $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-$input_pointer_id/cells || true)
        if echo "$key_probe" | $b grep -q '"text":"B"' &&
           echo "$pointer_probe" | $b grep -q '"text":"B"'; then
            break
        fi
        $b sleep 1
        input_probe_wait=$((input_probe_wait - 1))
    done
    $b timeout 3 $b wget -qO /tmp/input-key-surface.json http://127.0.0.1:19191/v1/surfaces/surface-$input_key_id/cells || true
    key_text=$($b tr ',' '\n' < /tmp/input-key-surface.json | \
        $b sed -n 's/.*"text":"\([^"]*\)".*/\1/p' | $b tr -d '\n')
    keypad_pass=1
    echo INPUT_KEY_TEXT=$key_text
    echo "$key_text" | $b grep -q 'BYTE 1B' || keypad_pass=0
    echo "$key_text" | $b grep -q 'BYTE 4F' || keypad_pass=0
    echo "$key_text" | $b grep -q 'BYTE 71' || keypad_pass=0
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" = 1 ]; then
        echo BLIT_INPUT_KEYPAD_SOAK_OBSERVED
    elif [ "$keypad_pass" -eq 1 ]; then
        echo BLIT_INPUT_KEYPAD_PASS
    else
        echo BLIT_INPUT_KEYPAD_FAIL
    fi
    echo BLIT_INPUT_SURFACE_KEY
    $b cat /tmp/input-key-surface.json | $b cut -c 1-4000
    echo BLIT_INPUT_SURFACE_POINTER
    pointer_surface=$($b timeout 3 $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-$input_pointer_id/cells || true)
    pointer_text=$(echo "$pointer_surface" | $b tr ',' '\n' | \
        $b sed -n 's/.*"text":"\([^"]*\)".*/\1/p' | $b tr -d '\n')
    echo INPUT_POINTER_TEXT=$pointer_text
    echo "$pointer_surface" | $b cut -c 1-4000
    echo BLIT_INPUT_LOG
    $b cat /tmp/blit.log
    mouse_device_events=$($b grep -c 'evdev mouse events received' /tmp/blit.log || true)
    if [ "${mouse_device_events:-0}" -ge 2 ]; then
        echo BLIT_INPUT_MULTI_MOUSE_PASS
    else
        echo BLIT_INPUT_MULTI_MOUSE_FAIL
    fi
    echo BLIT_INPUT_STREAM
    $b cat /tmp/stream-input.log
    stream_pass=1
    gamepad_pass=1
    $b grep -q "\"session_id\":\"session-$input_key_id\".*\"kind\":\"key\"" /tmp/stream-input.log || stream_pass=0
    $b grep -q '"pressed":false' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"button":0,"pressed":true' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"button":1,"pressed":true' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"button":2,"pressed":true' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"kind":"pointer_wheel"' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"kind":"gamepad_button"' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"kind":"gamepad_axis"' /tmp/stream-input.log || stream_pass=0
    $b grep -q '"session_id":"session-'"$input_key_id"'.*"kind":"gamepad_' /tmp/stream-input.log || gamepad_pass=0
    $b grep -q "\"session_id\":\"session-$input_pointer_id\".*\"kind\":\"pointer_motion\"" /tmp/stream-input.log || stream_pass=0
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" != 1 ] &&
       ! echo "$pointer_text" | $b grep -q 'BYTE 1B'; then stream_pass=0; fi
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" != 1 ] &&
       ! echo "$pointer_text" | $b grep -q 'BYTE 5B'; then stream_pass=0; fi
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" != 1 ] &&
       ! echo "$pointer_text" | $b grep -q 'BYTE 3C'; then stream_pass=0; fi
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" != 1 ] && [ "$keypad_pass" -ne 1 ]; then
        stream_pass=0
    fi
    if [ "$stream_pass" -eq 1 ] && [ "${mouse_device_events:-0}" -ge 2 ]; then
        echo BLIT_INPUT_STREAM_PASS
    else
        echo BLIT_INPUT_STREAM_FAIL
    fi
    if [ "$gamepad_pass" -eq 1 ] && $b grep -q 'evdev gamepad events received' /tmp/blit.log; then
        echo BLIT_INPUT_GAMEPAD_PASS
    else
        echo BLIT_INPUT_GAMEPAD_FAIL
    fi
    raw_runtime=$($b wget -qO- http://127.0.0.1:19191/v1/diagnostics/runtime || true)
    raw_mouse_events=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_events":\([0-9][0-9]*\).*/\1/p')
    raw_mouse_syncs=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_syncs":\([0-9][0-9]*\).*/\1/p')
    raw_mouse_motion=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_motion_events":\([0-9][0-9]*\).*/\1/p')
    raw_mouse_buttons=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_button_events":\([0-9][0-9]*\).*/\1/p')
    raw_mouse_repeats=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_button_repeats_ignored":\([0-9][0-9]*\).*/\1/p')
    raw_mouse_coalesced=$(echo "$raw_runtime" | $b sed -n 's/.*"evdev_mouse_button_state_coalesced":\([0-9][0-9]*\).*/\1/p')
    echo "BLIT_INPUT_RAW_EVDEV events=${raw_mouse_events:-0} syncs=${raw_mouse_syncs:-0} motion=${raw_mouse_motion:-0} buttons=${raw_mouse_buttons:-0} repeats_ignored=${raw_mouse_repeats:-0} state_coalesced=${raw_mouse_coalesced:-0}"
    if [ "${raw_mouse_events:-0}" -gt 0 ] &&
       [ "${raw_mouse_syncs:-0}" -gt 0 ] &&
       [ "${raw_mouse_motion:-0}" -gt 0 ] &&
       [ "${raw_mouse_buttons:-0}" -gt 0 ]; then
        echo BLIT_INPUT_RAW_EVDEV_PASS
    else
        echo BLIT_INPUT_RAW_EVDEV_FAIL
    fi
    if [ "${raw_mouse_repeats:-0}" -gt 0 ]; then
        echo BLIT_INPUT_MOUSE_REPEAT_PASS
    else
        echo BLIT_INPUT_MOUSE_REPEAT_FAIL
    fi
    if [ "${raw_mouse_coalesced:-0}" -gt 0 ]; then
        echo BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS
    else
        echo BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_FAIL
    fi
    if [ "${BLIT_INPUT_LOSS_TEST:-0}" = 1 ]; then
        echo BLIT_INPUT_ROUTES_AFTER_DEVICE_LOSS
        echo BLIT_INPUT_ROUTE_TARGET_SESSIONS
        for target_session in 1 2; do
            $b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$target_session" || true
        done
        routes_after_loss=$($b wget -qO- http://127.0.0.1:19191/v1/input/routes)
        diagnostics_after_loss=$($b wget -qO- http://127.0.0.1:19191/v1/diagnostics)
        echo "$routes_after_loss"
        echo "$diagnostics_after_loss"
        loss_pass=1
        # QEMU also exposes its baseline keyboard and mouse. The synthetic
        # devices therefore exercise compaction while a live target keeps its
        # route enabled. The soak helper intentionally exits after its bounded
        # workload, so a stopped target must have its route disabled by the
        # session lifecycle; both outcomes are asserted explicitly.
        target_one_state=$($b wget -qO- http://127.0.0.1:19191/v1/terminal-sessions/1 |
            $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
        target_two_state=$($b wget -qO- http://127.0.0.1:19191/v1/terminal-sessions/2 |
            $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
        if [ "$target_one_state" = running ]; then
            echo "$routes_after_loss" | $b grep -q '"id":"input-key".*"device_id":"evdev-keyboard".*"enabled":true' || loss_pass=0
            echo "$routes_after_loss" | $b grep -q '"id":"input-gamepad".*"device_id":"evdev-gamepad".*"enabled":true' || loss_pass=0
        else
            echo "$routes_after_loss" | $b grep -q '"id":"input-key".*"device_id":"evdev-keyboard".*"enabled":false' || loss_pass=0
            echo "$routes_after_loss" | $b grep -q '"id":"input-gamepad".*"device_id":"evdev-gamepad".*"enabled":false' || loss_pass=0
        fi
        if [ "$target_two_state" = running ]; then
            echo "$routes_after_loss" | $b grep -q '"id":"input-pointer".*"device_id":"evdev-pointer".*"enabled":true' || loss_pass=0
        else
            echo "$routes_after_loss" | $b grep -q '"id":"input-pointer".*"device_id":"evdev-pointer".*"enabled":false' || loss_pass=0
        fi
        echo "$diagnostics_after_loss" | $b grep -q '"keyboards":[1-9]' || loss_pass=0
        echo "$diagnostics_after_loss" | $b grep -q '"pointers":[1-9]' || loss_pass=0
        $b grep -q 'evdev keyboard disappeared:' /tmp/blit.log || loss_pass=0
        $b grep -q 'evdev mouse disappeared:' /tmp/blit.log || loss_pass=0
        $b grep -q 'evdev keyboard reconnected:' /tmp/blit.log || loss_pass=0
        $b grep -q 'evdev mouse reconnected:' /tmp/blit.log || loss_pass=0
        $b grep -q '"code":"input_device_lost"' /tmp/stream-input.log || loss_pass=0
        $b grep -q '"code":"input_device_reconnected"' /tmp/stream-input.log || loss_pass=0
        if [ "$loss_pass" -eq 1 ]; then
            echo BLIT_INPUT_DEVICE_LOSS_PASS
        else
            echo BLIT_INPUT_DEVICE_LOSS_FAIL
        fi
    fi
    if [ "${BLIT_INPUT_SOAK_TEST:-0}" = 1 ]; then
        [ "$stream_pass" -eq 1 ] || { echo BLIT_INPUT_RENDER_SOAK_FAIL; finish 1; }
        [ "${mouse_device_events:-0}" -ge 2 ] || { echo BLIT_INPUT_RENDER_SOAK_FAIL; finish 1; }
        echo BLIT_INPUT_RENDER_SOAK_PASS
    fi
    finish 0
fi

if [ "${BLIT_HIBR_PANES_TEST:-0}" = 1 ]; then
    # Exercise the production orchestration shape: one held primary desktop
    # and three --join clients, each bound to a Blit-owned pane.
    topology=$(/blit-stage/http-put 19191 /v1/topology \
        '{"layout":{"columns":2,"rows":2}}')
    panes_cmd=$(echo "$topology" | $b tr ',' '\n' |
        $b sed -n 's/.*"id":"\(pane-[^"]*\)".*/\1/p')
    pane1=$(echo "$panes_cmd" | $b sed -n '1p')
    pane2=$(echo "$panes_cmd" | $b sed -n '2p')
    pane3=$(echo "$panes_cmd" | $b sed -n '3p')
    pane4=$(echo "$panes_cmd" | $b sed -n '4p')
    pane5=$(echo "$panes_cmd" | $b sed -n '5p')
    echo HIBR_PANE_MATRIX_TOPOLOGY; echo "$topology"
    if [ -z "$pane1" ] || [ -z "$pane2" ] ||
       [ -z "$pane3" ] || [ -z "$pane4" ]; then
        echo HIBR_PANE_MATRIX_NOT_READY
        finish 1
    fi
    # Keep argument construction explicit: it is part of the acceptance
    # evidence for primary versus auxiliary Hibr invocation.
    $b wget -qO /tmp/hibr-pane-1.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$BLIT_HIBR_PANE_WORKLOAD\",\"--session\",\"blit\",\"--name\",\"display-47\"],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane1\",\"surface\":{\"width\":64,\"height\":24}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    primary_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/hibr-pane-1.json)
    $b sleep 3
    $b wget -qO /tmp/hibr-pane-2.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$BLIT_HIBR_PANE_WORKLOAD\",\"--session\",\"blit\",\"--join\",\"--name\",\"$pane2\"],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane2\",\"surface\":{\"width\":64,\"height\":24}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    $b wget -qO /tmp/hibr-pane-3.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$BLIT_HIBR_PANE_WORKLOAD\",\"--session\",\"blit\",\"--join\",\"--name\",\"$pane3\"],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane3\",\"surface\":{\"width\":64,\"height\":24}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    $b wget -qO /tmp/hibr-pane-4.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$BLIT_HIBR_PANE_WORKLOAD\",\"--session\",\"blit\",\"--join\",\"--name\",\"$pane4\"],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane4\",\"surface\":{\"width\":64,\"height\":24}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
    $b sleep 10
    pane_matrix_pass=1
    echo HIBR_PANE_MATRIX_SESSIONS
    for n in 1 2 3 4; do
        session_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' "/tmp/hibr-pane-$n.json")
        session_detail=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$session_id")
        state=$(echo "$session_detail" |
            $b sed -n 's/.*"state":"\([^\"]*\)".*/\1/p')
        surface_revision=$(echo "$session_detail" |
            $b sed -n 's/.*"surface_revision":\([0-9][0-9]*\).*/\1/p')
        surface_json=$($b wget -qO- \
            "http://127.0.0.1:19191/v1/surfaces/surface-$session_id/cells")
        cell_count=$(echo "$surface_json" | $b tr ',' '\n' |
            $b grep -c '"x":' || true)
        echo "pane$n session=$session_id state=$state surface_revision=${surface_revision:-0} cells=$cell_count"
        [ "$state" = running ] || pane_matrix_pass=0
        [ "${surface_revision:-0}" -ge 2 ] || pane_matrix_pass=0
        [ "${cell_count:-0}" -gt 0 ] || pane_matrix_pass=0
    done
    if [ "$pane_matrix_pass" -ne 1 ]; then
        echo HIBR_PANE_MATRIX_START_RESPONSES
        for n in 1 2 3 4; do
            echo "pane$n"; $b cat "/tmp/hibr-pane-$n.json" 2>/dev/null || true
            session_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' "/tmp/hibr-pane-$n.json")
            echo "surface$n"
            $b wget -qO- "http://127.0.0.1:19191/v1/surfaces/surface-$session_id/cells" 2>/dev/null |
                $b cut -c 1-3000 || true
        done
        echo HIBR_PANE_MATRIX_LOGS
        $b find /tmp -maxdepth 2 -type f -name '*.log' -print \
            -exec $b sh -c 'echo --- "$1"; cat "$1"' sh {} \; 2>/dev/null || true
    fi
    if [ "${BLIT_HIBR_RESTART_TEST:-0}" = 1 ] && [ "$pane_matrix_pass" -eq 1 ]; then
        primary_session_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/hibr-pane-1.json)
        primary_detail=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$primary_session_id" 2>/dev/null || true)
        primary_pid=$(echo "$primary_detail" |
            $b sed -n 's/.*"process_id"[ ]*:[ ]*\([0-9][0-9]*\).*/\1/p')
        desktop_pid=
        for proc in /proc/[0-9]*; do
            [ -r "$proc/comm" ] || continue
            comm=$( $b cat "$proc/comm" 2>/dev/null || true )
            [ "$comm" = 'hibr [desktop]' ] || continue
            candidate=${proc##*/}
            [ "$candidate" != "$primary_pid" ] || continue
            desktop_pid=$candidate
            break
        done
        restart_pass=1
        [ -n "$desktop_pid" ] || restart_pass=0
        if [ "$restart_pass" -eq 1 ]; then
            echo HIBR_PRIMARY_RESTART_TARGET pid=$desktop_pid process_id=$primary_pid
            $b kill -SEGV "$desktop_pid" 2>/dev/null || restart_pass=0
            $b sleep 15
        fi
        restart_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$primary_session_id" |
            $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
        [ "$restart_state" = running ] || restart_pass=0
        if [ "$restart_pass" -eq 1 ]; then
            echo HIBR_PRIMARY_RESTART_PASS
        else
            echo HIBR_PRIMARY_RESTART_FAIL target=$desktop_pid process_id=$primary_pid state=$restart_state detail="$primary_detail"
            pane_matrix_pass=0
        fi
        join_reconnect_pass=1
        join_session_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/hibr-pane-2.json)
        join_detail=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$join_session_id" 2>/dev/null || true)
        join_pid=$(echo "$join_detail" |
            $b sed -n 's/.*"process_id"[ ]*:[ ]*\([0-9][0-9]*\).*/\1/p')
        [ -n "$join_pid" ] || join_reconnect_pass=0
        if [ "$join_reconnect_pass" -eq 1 ]; then
            $b kill -KILL "$join_pid" 2>/dev/null || join_reconnect_pass=0
            $b sleep 3
        fi
        join_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$join_session_id" |
            $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
        primary_after_join=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$primary_session_id" |
            $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
        [ "$join_state" = failed ] || join_reconnect_pass=0
        [ "$primary_after_join" = running ] || join_reconnect_pass=0
        if [ "$join_reconnect_pass" -eq 1 ] && [ -n "$pane5" ]; then
            $b wget -qO /tmp/hibr-pane-rejoin.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$BLIT_HIBR_PANE_WORKLOAD\",\"--session\",\"blit\",\"--join\",\"--name\",\"$pane5\"],\"surface_mode\":\"terminal\",\"pane_id\":\"$pane5\",\"surface\":{\"width\":64,\"height\":24}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
            rejoin_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/hibr-pane-rejoin.json)
            $b sleep 8
            rejoin_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$rejoin_id" |
                $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
            [ "$rejoin_state" = running ] || join_reconnect_pass=0
        else
            join_reconnect_pass=0
        fi
        if [ "$join_reconnect_pass" -eq 1 ]; then
            echo HIBR_JOIN_RECONNECT_PASS
        else
            echo HIBR_JOIN_RECONNECT_FAIL join_pid=$join_pid join_state=$join_state primary_state=$primary_after_join rejoin_state=${rejoin_state:-}
            pane_matrix_pass=0
        fi
    fi
    final_topology=$($b wget -qO- http://127.0.0.1:19191/v1/topology)
    echo HIBR_PANE_MATRIX_FINAL_TOPOLOGY; echo "$final_topology"
    surface_count=$(echo "$final_topology" | $b tr ',' '\n' |
        $b sed -n 's/.*"surface_id":"surface-[0-9][0-9]*".*/x/p' | $b wc -l)
    expected_surface_count=4
    [ "${BLIT_HIBR_RESTART_TEST:-0}" = 1 ] && expected_surface_count=5
    [ "$surface_count" -eq "$expected_surface_count" ] || pane_matrix_pass=0
    if [ "$pane_matrix_pass" -eq 1 ]; then
        echo HIBR_PANE_MATRIX_PASS
        finish 0
    fi
    echo HIBR_PANE_MATRIX_FAIL
    finish 1
fi

$b wget -qO /tmp/probe.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"--agent\",\"/blit-stage/blit-adapter-probe.hibr\"],\"surface_mode\":\"external\",\"display_id\":\"$display1\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
$b sleep 2
echo PROBE; $b cat /tmp/probe.json 2>/dev/null || true
echo PROBE_SURFACE; $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-1/cells | $b cut -c 1-1000

$b touch /tmp/primary.json /tmp/join.json
primary_script=/blit-stage/blit-session.hibr
join_script=/blit-stage/blit-session.hibr
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.hibr_window_control=1 "*)
        primary_script=/blit-stage/hibr_window_workload.hibr
        join_script=/blit-stage/hibr_window_workload.hibr
        ;;
esac
$b wget -qO /tmp/primary.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$primary_script\",\"--session\",\"blit\",\"--name\",\"$display1\"],\"environment\":{\"BLIT_HIBR_ADAPTER\":\"/blit-stage/mods/console-blit.so\",\"CP_THEME\":\"retro_car\",\"DT_CTL\":\"1\",\"DT_CTLPATH\":\"/tmp/hibr-ctl-0/blit.ctl\",\"DT_LOG\":\"/tmp/primary-hibr.log\"},\"surface_mode\":\"terminal\",\"pane_id\":\"$pane1\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
primary_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/primary.json)
ready=0
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    primary_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$primary_id" 2>/dev/null || true)
    state=$(echo "$primary_state" | $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
    surface_revision=$(echo "$primary_state" | $b sed -n 's/.*"surface_revision":\([0-9][0-9]*\).*/\1/p')
    if [ "$state" = running ] && [ "${surface_revision:-0}" -ge 2 ]; then
        ready=1
        break
    fi
    $b sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo PRIMARY_NOT_READY
    echo "$primary_state"
    echo PRIMARY_SURFACE
    $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-$primary_id/cells 2>/dev/null |
        $b cut -c 1-6000 || true
    echo PRIMARY_HIBR_LOG
    $b cat /tmp/primary-hibr.log 2>/dev/null || true
    echo HIBR_ADAPTER_ENV
    $b cat /tmp/blit-adapter-env.log 2>/dev/null || true
    echo HIBR_STATE_FILES
    $b find /tmp/hibr-state -type f -maxdepth 4 -print -exec $b cat {} \; 2>/dev/null || true
    echo BLIT_LOG; $b cat /tmp/blit.log
    exec $b sh
fi
$b wget -qO /tmp/join.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"$join_script\",\"--session\",\"blit\",\"--join\",\"--name\",\"$display2\"],\"environment\":{\"BLIT_HIBR_ADAPTER\":\"/blit-stage/mods/console-blit.so\",\"DT_LOG\":\"/tmp/hibr-join.log\"},\"surface_mode\":\"terminal\",\"pane_id\":\"$pane2\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
join_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/join.json)
$b sleep 8
ready=0
for n in 1 2 3 4 5 6 7 8 9 10; do
    primary_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$primary_id" 2>/dev/null || true)
    join_state=$($b wget -qO- "http://127.0.0.1:19191/v1/terminal-sessions/$join_id" 2>/dev/null || true)
    primary_running=$(echo "$primary_state" | $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
    join_running=$(echo "$join_state" | $b sed -n 's/.*"state":"\([^"]*\)".*/\1/p')
    if [ "$primary_running" = running ] && [ "$join_running" = running ]; then
        ready=1
        break
    fi
    $b sleep 1
done
echo PRIMARY; $b cat /tmp/primary.json 2>/dev/null || true
echo JOIN; $b cat /tmp/join.json 2>/dev/null || true
echo PRIMARY_STATE; echo "$primary_state"
echo JOIN_STATE; echo "$join_state"

# Exercise Hibr's released desktop control path against the same held session
# used by the primary and joined clients.  Blit owns transport and panes;
# Hibr owns desktop/display placement and reports its authoritative topology.
echo HIBR_CONTROL_SOCKETS
$b find /tmp -type s -print 2>/dev/null || true
control_displays_pass=1
control_displays=
for n in 1 2 3 4 5 6 7 8 9 10; do
    control_displays=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit displays 2>/tmp/hibr-control.err || true)
    echo "$control_displays" | $b grep -q '"ok":true' || control_displays=
    [ -n "$control_displays" ] && break
    $b sleep 1
done
echo HIBR_CONTROL_DISPLAYS; echo "$control_displays"
echo "$control_displays" | $b grep -q '"ok":true' || control_displays_pass=0
echo "$control_displays" | $b grep -q "\"name\":\"$display1\"" || control_displays_pass=0
echo "$control_displays" | $b grep -q "\"name\":\"$display2\"" || control_displays_pass=0
if [ "$control_displays_pass" -ne 1 ]; then
    echo HIBR_CONTROL_DISPLAYS_FAIL
    $b cat /tmp/hibr-control.err 2>/dev/null || true
    echo HIBR_ADAPTER_ENV
    $b cat /tmp/blit-adapter-env.log 2>/dev/null || true
    echo HIBR_STATE_FILES
    $b find /tmp/hibr-state -maxdepth 4 -type f -print -exec $b cat {} \; 2>/dev/null || true
    echo PRIMARY_HIBR_LOG
    $b cat /tmp/primary-hibr.log 2>/dev/null || true
fi
control_windows=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit windows 2>/tmp/hibr-control-windows.err || true)
echo HIBR_CONTROL_WINDOWS; echo "$control_windows"
window_id=$(echo "$control_windows" | $b sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p' | $b sed -n '1p')
window_control_pass=0
if [ -n "$window_id" ]; then
    move_window=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit move "$window_id" "$display2" 2>/tmp/hibr-control-move.err || true)
    resize_window=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit resize "$window_id" 20 60 2>/tmp/hibr-control-resize.err || true)
    focus_window=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit focus "$window_id" 2>/tmp/hibr-control-focus.err || true)
    echo HIBR_CONTROL_MOVE; echo "$move_window"
    echo HIBR_CONTROL_RESIZE; echo "$resize_window"
    echo HIBR_CONTROL_FOCUS; echo "$focus_window"
    echo "$move_window" | $b grep -q '"ok":true' &&
        echo "$resize_window" | $b grep -q '"ok":true' &&
        echo "$focus_window" | $b grep -q '"ok":true' && window_control_pass=1
else
    echo HIBR_CONTROL_WINDOWS_EMPTY
    # The stock desktop session deliberately starts without application
    # windows.  That is a valid primary/join acceptance result; the
    # move/resize/focus contract is asserted separately by the explicit
    # blit.hibr_window_control=1 workload.
    case " $($b cat /proc/cmdline 2>/dev/null || true) " in
        *" blit.hibr_window_control=1 "*) ;;
        *) window_control_pass=1 ;;
    esac
fi
if [ "$control_displays_pass" -ne 1 ]; then
    echo HIBR_CONTROL_BLOCKED
    finish 1
fi
keyboard_route=$(/blit-stage/http-put 19191 /v1/input/routes/keyboard-primary '{"device_id":"evdev-keyboard","target_session_id":"session-2","enabled":true}' || true)
pointer_route=$(/blit-stage/http-put 19191 /v1/input/routes/pointer-join '{"device_id":"evdev-pointer","target_session_id":"session-3","enabled":true}' || true)
routes=$($b wget -qO- http://127.0.0.1:19191/v1/input/routes)
echo INPUT_ROUTES; echo "$routes"
input_routes_pass=1
echo "$routes" | $b grep -q '"id":"keyboard-primary"' || input_routes_pass=0
echo "$routes" | $b grep -q '"target_session_id":"session-2"' || input_routes_pass=0
echo "$routes" | $b grep -q '"id":"pointer-join"' || input_routes_pass=0
echo "$routes" | $b grep -q '"target_session_id":"session-3"' || input_routes_pass=0

    # Leave the primary keyboard route active long enough for the harness to send
    # a real Hibr menu shortcut before the probe routes take ownership.
    echo READY_FOR_WINDOW_INPUT
    $b sleep 8

$b wget -qO /tmp/input-key.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"--agent\",\"/blit-stage/input_probe.hibr\"],\"surface_mode\":\"external\",\"display_id\":\"$display1\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
$b wget -qO /tmp/input-pointer.json --post-data="{\"command\":\"/blit-stage/hibr\",\"arguments\":[\"--agent\",\"/blit-stage/input_probe.hibr\"],\"surface_mode\":\"external\",\"display_id\":\"$display2\",\"surface\":{\"width\":128,\"height\":48}}" --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
input_key_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/input-key.json)
input_pointer_id=$($b sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p' /tmp/input-pointer.json)
$b sleep 3
/blit-stage/http-put 19191 /v1/input/routes/input-key-probe "{\"device_id\":\"evdev-keyboard\",\"target_session_id\":\"session-$input_key_id\",\"enabled\":true}" > /tmp/input-key-route.json || true
/blit-stage/http-put 19191 /v1/input/routes/input-pointer-probe "{\"device_id\":\"evdev-pointer\",\"target_session_id\":\"session-$input_pointer_id\",\"enabled\":true}" > /tmp/input-pointer-route.json || true
echo INPUT_PROBE_SESSIONS; $b cat /tmp/input-key.json; $b cat /tmp/input-pointer.json
echo INPUT_PROBE_ROUTES; $b cat /tmp/input-key-route.json; $b cat /tmp/input-pointer-route.json
$b sleep 3
control_windows=$(/blit-stage/hibr /blit-stage/lib/ctl.hibr --session blit windows 2>/tmp/hibr-control-windows.err || true)
echo HIBR_CONTROL_WINDOWS; echo "$control_windows"
echo TOPOLOGY; $b wget -qO- http://127.0.0.1:19191/v1/topology
echo SURFACE1; $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-1/cells | $b cut -c 1-1000
echo SURFACE2; $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-2/cells | $b cut -c 1-1000
echo SURFACE3; $b wget -qO- http://127.0.0.1:19191/v1/surfaces/surface-3/cells | $b cut -c 1-1000
echo BLIT_LOG; $b cat /tmp/blit.log
echo HIBR_LOGS; $b find /tmp /root -type f -name '*.log' -exec $b sh -c 'echo --- "$1"; cat "$1"' sh {} \; 2>/dev/null || true
$b echo GUEST_SCREENSHOTS; $b ls -l /tmp/guest-screenshots 2>/dev/null || true
$b echo HIBR_TOP_LEFT_PIXELS
for point in '4 4' '24 24' '32 32'; do
    px=${point% *}
    py=${point#* }
    offset=$((16 + (py * 1024 + px) * 3))
    for image in /tmp/guest-screenshots/display-*.ppm; do
        [ -f "$image" ] || continue
        rgb=$($b dd if="$image" bs=1 skip="$offset" count=3 2>/dev/null |
            $b od -An -tu1 | $b tr -s ' ' ',' )
        echo "${image##*/} x=$px y=$py rgb=$rgb"
    done
done
$b cp /tmp/guest-screenshots/display-*.ppm /tmp/host-screenshots/ 2>/dev/null || true
if [ "$ready" -eq 1 ] && [ "$input_routes_pass" -eq 1 ] && [ "$control_displays_pass" -eq 1 ] && [ "$window_control_pass" -eq 1 ]; then
    echo ACCEPTANCE_PASS
    finish 0
else
    echo ACCEPTANCE_FAIL
    finish 1
fi
