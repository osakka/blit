#!/bin/sh
set -eu
b=/usr/bin/busybox
finish() { $b poweroff -f 2>/dev/null || $b halt -f 2>/dev/null || true; exit "$1"; }
$b mkdir -p /sys /proc /tmp /tmp/guest-screenshots /tmp/host-screenshots
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
$b ifconfig lo 127.0.0.1 up 2>/dev/null || true
$b mkdir -p /dev/pts
$b mount -t devpts devpts /dev/pts 2>/dev/null || true
$b mkdir -p /lib64 /usr/lib/x86_64-linux-gnu
$b cp /blit-stage/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
$b cp /blit-stage/libc.so.6 /usr/lib/x86_64-linux-gnu/libc.so.6
$b test -x /blit-stage/ppm-assert || { echo PPM_ASSERT_MISSING; finish 1; }
$b test -x /blit-stage/bmp-assert || { echo BMP_ASSERT_MISSING; finish 1; }
$b mount -t 9p -o trans=virtio,version=9p2000.L blitshots /tmp/host-screenshots 2>/dev/null || true
export BLIT_KMS_SCREENSHOT_DIR=/tmp/guest-screenshots
export BLIT_KMS_SCREENSHOT_AFTER=0
export BLIT_KMS_TRACE_TIMING=1
export BLIT_KMS_TRACE_PHASES=1
if [ -f /blit-stage/fonts/Blit-Hibr-Terminus16.psf ]; then
    export BLIT_PSF_FONT=/blit-stage/fonts/Blit-Hibr-Terminus16.psf
fi
if [ -e /dev/dri/card1 ]; then export BLIT_DRM_DEVICE=/dev/dri/card1; else export BLIT_DRM_DEVICE=/dev/dri/card0; fi
loss_test=0
stack_test=0
mixed_refresh_test=0
high_refresh_test=0
kitty_limits_test=0
kitty_query_test=0
sixel_test=0
visuals_test=0
pixel_test=0
pixel_stream_test=0
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.output_loss=1 "*) loss_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.kitty_stack=1 "*) stack_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.mixed_refresh=1 "*) mixed_refresh_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.high_refresh=1 "*) high_refresh_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.kitty_limits=1 "*) kitty_limits_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.kitty_query=1 "*) kitty_query_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.sixel=1 "*) sixel_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.visuals=1 "*) visuals_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.pixel_frame=1 "*) pixel_test=1 ;;
esac
case " $($b cat /proc/cmdline 2>/dev/null || true) " in
    *" blit.pixel_stream=1 "*) pixel_test=1; pixel_stream_test=1 ;;
esac
if [ "$visuals_test" -eq 1 ] || [ "$pixel_test" -eq 1 ]; then
    # Capture after the effect, overlay, and session requests have had time
    # to reach a presented frame; frame zero is the pre-API blank modeset.
    export BLIT_KMS_SCREENSHOT_AFTER=8
fi
if [ "$pixel_test" -eq 1 ]; then
    export BLIT_KMS_SCREENSHOT_READY_FILE=/tmp/pixel-ready
    $b rm -f /tmp/pixel-ready
fi
if [ "$mixed_refresh_test" -eq 1 ]; then
    export BLIT_TEST_MODE=1
    export BLIT_TEST_REFRESH_HZ=60,120
fi
if [ "$high_refresh_test" -eq 1 ]; then
    export BLIT_TEST_MODE=1
    export BLIT_TEST_REFRESH_HZ=60,240
fi
if [ "$kitty_query_test" -eq 1 ]; then
    export BLIT_KITTY_TRACE=1
fi
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=127.0.0.1 /blit-stage/ld-linux-x86-64.so.2 --library-path /blit-stage:/blit-stage/lib /blit-stage/blit-server 19191 >/tmp/blit.log 2>&1 &
ready=0
for n in 1 2 3 4 5 6 7 8 9 10; do
    if $b wget -qO /tmp/server.json http://127.0.0.1:19191/v1/server; then ready=1; break; fi
    $b sleep 1
done
echo SERVER; $b cat /tmp/server.json 2>/dev/null || true
if [ "$ready" -ne 1 ]; then $b cat /tmp/blit.log; finish 1; fi
diagnostics=$($b wget -qO- http://127.0.0.1:19191/v1/diagnostics || true)
echo DIAGNOSTICS; echo "$diagnostics"
scheduler_pass=1
echo "$diagnostics" | $b grep -q '"mode":"refresh-aware"' || scheduler_pass=0
echo "$diagnostics" | $b grep -q '"effective_interval_ms":' || scheduler_pass=0
echo "$diagnostics" | $b grep -q '"outputs":\[' || scheduler_pass=0
mixed_refresh_pass=1
if [ "$mixed_refresh_test" -eq 1 ]; then
    echo "$diagnostics" | $b grep -q '"refresh_hz":120.000' || mixed_refresh_pass=0
    echo "$diagnostics" | $b grep -q '"refresh_source":"test-override"' || mixed_refresh_pass=0
    echo "$diagnostics" | $b grep -q '"effective_interval_ms":8' || mixed_refresh_pass=0
    [ "$mixed_refresh_pass" -eq 1 ] && echo REFRESH_SCHEDULER_MIXED_PASS || echo REFRESH_SCHEDULER_MIXED_FAIL
fi
high_refresh_pass=1
if [ "$high_refresh_test" -eq 1 ]; then
    echo "$diagnostics" | $b grep -q '"refresh_hz":240.000' || high_refresh_pass=0
    echo "$diagnostics" | $b grep -q '"refresh_source":"test-override"' || high_refresh_pass=0
    echo "$diagnostics" | $b grep -q '"effective_interval_ms":4' || high_refresh_pass=0
    [ "$high_refresh_pass" -eq 1 ] && echo REFRESH_SCHEDULER_240_PASS || echo REFRESH_SCHEDULER_240_FAIL
fi
graphics_inventory=$($b wget -qO- http://127.0.0.1:19191/v1/graphics || true)
echo GRAPHICS; echo "$graphics_inventory"
graphics_pass=1
echo "$graphics_inventory" | $b grep -q '"graphics_cards":\[' || graphics_pass=0
echo "$graphics_inventory" | $b grep -q '"vendor":"' || graphics_pass=0
echo "$graphics_inventory" | $b grep -q '"device":"' || graphics_pass=0
echo "$graphics_inventory" | $b grep -q '"driver":"' || graphics_pass=0
echo "$graphics_inventory" | $b grep -q '"hardware_acceleration":' || graphics_pass=0
if [ "$visuals_test" -eq 1 ]; then
    /blit-stage/http-put 19191 /v1/effects '{"revision":1,"enabled":true,"preset":"scanlines","intensity":100}' >/tmp/effect.json || true
    $b wget -qO /tmp/overlay.json --post-data='{"display_id":"display-47","x":2,"y":32,"width":4,"height":4,"color":"#0000ff","opacity":100}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/overlays || true
    echo VISUAL_EFFECT; $b cat /tmp/effect.json 2>/dev/null || true
    echo VISUAL_OVERLAY; $b cat /tmp/overlay.json 2>/dev/null || true
fi
if [ "$stack_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--wide","--kitty","--stack"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
elif [ "$kitty_limits_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--kitty-limits"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
elif [ "$kitty_query_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--kitty-query"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
elif [ "$sixel_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--sixel"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
elif [ "$visuals_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--wide"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
elif [ "$pixel_test" -eq 1 ]; then
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--wide"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
else
    $b wget -qO /tmp/one.json --post-data='{"command":"/blit-stage/pty-input-echo","arguments":["--wide","--kitty"],"surface_mode":"terminal","display_id":"display-47","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
fi
$b wget -qO /tmp/two.json --post-data='{"command":"/blit-stage/pty-input-echo","surface_mode":"terminal","display_id":"display-48","surface":{"width":128,"height":48}}' --header='Content-Type: application/json' http://127.0.0.1:19191/v1/terminal-sessions || true
if [ "$pixel_test" -eq 1 ]; then
    pixel_session=$($b sed -n 's/.*"id":"\([^"]*\)".*/\1/p' /tmp/one.json)
    if [ -n "$pixel_session" ] && {
        if [ "$pixel_stream_test" -eq 1 ]; then
            /blit-stage/ws-pixel-frame-probe "$pixel_session" --60hz
        else
            /blit-stage/ws-pixel-frame-probe "$pixel_session"
        fi
    }; then
        :
    else
        echo PIXEL_FRAME_PROBE_FAIL
        finish 1
    fi
    if [ "$pixel_stream_test" -eq 1 ]; then
        $b wget -qO /tmp/pixel-runtime.json http://127.0.0.1:19191/v1/diagnostics/runtime || true
        if $b grep -q '"pixel_frames_accepted":60' /tmp/pixel-runtime.json &&
           $b grep -q '"missed_deadlines":0' /tmp/pixel-runtime.json; then
            echo PIXEL_FRAME_RATE_PASS
        else
            echo PIXEL_FRAME_RATE_FAIL
            finish 1
        fi
    fi
    $b touch /tmp/pixel-ready
    $b sleep 2
    $b wget -qO /tmp/api-pixel.bmp http://127.0.0.1:19191/v1/diagnostics/screenshots/display-47 || true
    if [ -f /tmp/api-pixel.bmp ] && /blit-stage/bmp-assert /tmp/api-pixel.bmp; then
        echo KMS_API_PIXEL_PASS
    else
        echo KMS_API_PIXEL_FAIL
        finish 1
    fi
fi
[ "$loss_test" -eq 1 ] && ( $b sleep 2; echo 2 > /sys/kernel/config/vkms/blit/connectors/connector0/status ) &
# Hibr/PTY startup is intentionally asynchronous.  Give both probe sessions
# enough time to produce their first content before the capture predicate is
# evaluated; a fixed short sleep made the visual gate depend on QEMU TCG load.
$b sleep 12
echo ONE; $b cat /tmp/one.json; echo TWO; $b cat /tmp/two.json
kitty_limits_pass=1
if [ "$kitty_limits_test" -eq 1 ]; then
    limits_session=$($b wget -qO- http://127.0.0.1:19191/v1/terminal-sessions/1 || true)
    echo KITTY_LIMITS; echo "$limits_session"
    echo "$limits_session" | $b grep -q '"rejected":1' || kitty_limits_pass=0
    echo "$limits_session" | $b grep -q '"evicted":1' || kitty_limits_pass=0
    [ "$kitty_limits_pass" -eq 1 ] && echo KITTY_LIMITS_PASS || echo KITTY_LIMITS_FAIL
fi
loss_pass=1
if [ "$loss_test" -eq 1 ]; then
    loss_displays=$($b wget -qO- http://127.0.0.1:19191/v1/displays || true)
    loss_diagnostics=$($b wget -qO- http://127.0.0.1:19191/v1/diagnostics || true)
    echo LOSS_DISPLAYS; echo "$loss_displays"
    echo LOSS_DIAGNOSTICS; echo "$loss_diagnostics"
    echo "$loss_displays" | $b grep -q '"presentation_state":"failed"' || loss_pass=0
    echo "$loss_displays" | $b grep -q '"presentation_state":"presenting"' || loss_pass=0
    echo "$loss_diagnostics" | $b grep -q 'display_presentation_failed' || loss_pass=0
    [ "$loss_pass" -eq 1 ] && echo OUTPUT_LOSS_PASS || echo OUTPUT_LOSS_FAIL
fi
echo BLIT_LOG; $b cat /tmp/blit.log
echo GUEST_SCREENSHOTS; $b ls -l /tmp/guest-screenshots 2>/dev/null || true
for shot in /tmp/guest-screenshots/display-47.ppm /tmp/guest-screenshots/display-48.ppm; do
    if [ -f "$shot" ]; then
        echo FRAME_FILE; $b sha256sum "$shot"; $b od -An -t u1 -N 32 "$shot"
    fi
done
if [ "$kitty_limits_test" -eq 1 ]; then
    if [ "$kitty_limits_pass" -eq 1 ]; then
        echo KMS_KITTY_LIMITS_PASS
        finish 0
    fi
    echo KMS_KITTY_LIMITS_FAIL
    finish 1
fi
if [ "$kitty_query_test" -eq 1 ]; then
    query_session=$($b wget -qO- http://127.0.0.1:19191/v1/terminal-sessions/1 || true)
    echo KITTY_QUERY; echo "$query_session"
    echo "$query_session" | $b grep -q 'KITTY_QUERY_RESPONSE_PASS' && echo KMS_KITTY_QUERY_PASS && finish 0
    echo KMS_KITTY_QUERY_FAIL
    finish 1
fi
if [ "$loss_test" -eq 1 ]; then
    /blit-stage/ppm-assert /tmp/guest-screenshots/display-48.ppm && echo PPM_DISPLAY_48_SURVIVOR_PASS || { echo PPM_DISPLAY_48_SURVIVOR_FAIL; finish 1; }
elif [ "$stack_test" -eq 1 ]; then
    /blit-stage/ppm-assert /tmp/guest-screenshots/display-47.ppm --wide --green && echo PPM_DISPLAY_47_WIDE_STACK_CLIP_PASS || { echo PPM_DISPLAY_47_WIDE_STACK_CLIP_FAIL; finish 1; }
elif [ "$sixel_test" -eq 1 ]; then
    /blit-stage/ppm-assert /tmp/guest-screenshots/display-47.ppm --sixel && echo PPM_DISPLAY_47_SIXEL_PASS || { echo PPM_DISPLAY_47_SIXEL_FAIL; finish 1; }
elif [ "$visuals_test" -eq 1 ]; then
    /blit-stage/ppm-assert /tmp/guest-screenshots/display-47.ppm --visuals && echo PPM_DISPLAY_47_VISUALS_PASS || { echo PPM_DISPLAY_47_VISUALS_FAIL; finish 1; }
elif [ "$pixel_test" -eq 1 ]; then
    echo PPM_DISPLAY_47_API_PIXEL_NOT_USED
else
    /blit-stage/ppm-assert /tmp/guest-screenshots/display-47.ppm --wide --kitty && echo PPM_DISPLAY_47_WIDE_KITTY_PASS || { echo PPM_DISPLAY_47_WIDE_KITTY_FAIL; finish 1; }
fi
/blit-stage/ppm-assert /tmp/guest-screenshots/display-48.ppm && echo PPM_DISPLAY_48_PASS || { echo PPM_DISPLAY_48_FAIL; finish 1; }
$b wget -qO /tmp/api-display-47.bmp http://127.0.0.1:19191/v1/diagnostics/screenshots/display-47 || true
api_screenshot_pass=0
if [ -f /tmp/api-display-47.bmp ]; then
    api_magic=$($b od -An -t x1 -N 2 /tmp/api-display-47.bmp | $b tr -d ' \n')
    api_size=$($b wc -c /tmp/api-display-47.bmp | $b cut -d ' ' -f 1)
    if [ "$api_magic" = "424d" ] && [ "$api_size" -gt 3145728 ] && [ "$api_size" -le 67108864 ]; then
        api_screenshot_pass=1
        echo KMS_API_SCREENSHOT_PASS size=$api_size
    fi
fi
if [ "$api_screenshot_pass" -ne 1 ]; then
    echo KMS_API_SCREENSHOT_FAIL
    finish 1
fi
$b cp /tmp/guest-screenshots/display-*.ppm /tmp/host-screenshots/ 2>/dev/null || true
size1=0
size2=0
hash1=
hash2=
if [ -f /tmp/guest-screenshots/display-47.ppm ]; then
    size1=$($b wc -c /tmp/guest-screenshots/display-47.ppm | $b cut -d ' ' -f 1)
    hash1=$($b sha256sum /tmp/guest-screenshots/display-47.ppm | $b cut -d ' ' -f 1)
fi
if [ -f /tmp/guest-screenshots/display-48.ppm ]; then
    size2=$($b wc -c /tmp/guest-screenshots/display-48.ppm | $b cut -d ' ' -f 1)
    hash2=$($b sha256sum /tmp/guest-screenshots/display-48.ppm | $b cut -d ' ' -f 1)
fi
capture_pass=0
if [ "$loss_test" -eq 1 ]; then
    [ "$size2" -gt 2359296 ] && capture_pass=1
elif [ "$size1" -gt 2359296 ] && [ "$size2" -gt 2359296 ] && [ "$hash1" != "$hash2" ]; then
    capture_pass=1
fi
if [ "$capture_pass" -eq 1 ] && [ "$loss_pass" -eq 1 ] && [ "$graphics_pass" -eq 1 ] && [ "$scheduler_pass" -eq 1 ] && [ "$mixed_refresh_pass" -eq 1 ] && [ "$high_refresh_pass" -eq 1 ] && [ "$kitty_limits_pass" -eq 1 ] && [ "$api_screenshot_pass" -eq 1 ]; then echo KMS_CAPTURE_PASS; finish 0; fi
echo KMS_CAPTURE_FAIL
finish 1
