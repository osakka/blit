#!/bin/sh
set -eu

# Operator-only tooling. This is not installed in the Debian package.
#
# docs/development-workflow.md names the gates that prove each class of
# change. That mapping has lived only in prose, so running "the relevant
# gates" has meant reading the list and invoking each one by hand with its
# own convention. That works for an operator who wrote them. It does not
# survive a second operator: the gates take their target through four
# different spellings (BLIT_TEST_SERVER, BLIT_GATE_SERVER, BLIT_GATE_PORT,
# a positional argument), two of them are sub-gates that another gate
# invokes, and two need an argument that has no default. Running the suite
# by hand without knowing that produces failures that look like product
# defects and are not.
#
# This script is that mapping, executable. It owns the server lifecycle for
# the gates that need one, refuses to run a sub-gate standalone, and skips
# with a stated reason rather than failing when a gate needs QEMU, physical
# hardware, a package, or a live host.

usage() {
    cat >&2 <<'EOF'
usage: run-gates.sh [SUITE ...]

Suites (from docs/development-workflow.md "Required gates"):
  schema      schema validation, API conformance, version identity, streams
  terminal    TCC terminal conformance, session lifecycle, write/resize
  font        glyph coverage, UTF-8 snapshots, font API, atlas filter
  renderer    dummy-backend pixel, damage, pointer, graphics cache
  gles        explicit offscreen OpenGLES2 composition (requires working EGL/GLES)
  hibr        singleton ownership and resume fail-closed behaviour
  lifecycle   restart, shutdown, supervised update handling
  timing      measured presentation cadence and the bounded PTY soak
  all         standard suites above; run gles separately where EGL/GLES is available

Environment:
  BLIT_GATE_BINARY   server under test  (default build/blit/blit-server)
  BLIT_GATE_FONT     PSF font           (default the bundled Hibr font)
  BLIT_GATE_LIBS     LD_LIBRARY_PATH    (default build/sdl3-kms)
  BLIT_GATE_PACKAGE  .deb path; enables the packaging artifact gate
  BLIT_GATE_LIVE     live base URL; enables the live-host-only gates
  BLIT_SOAK_SECONDS  soak duration      (default 120, gate's own default)
  BLIT_HIBR_ROOT     Hibr checkout providing the headers the adapter gate
                     compiles against (default /tmp/hibr-<packaging/HIBR_VERSION>
                     when present; without it that gate skips)

Exit status is 0 only when every gate that ran reported success. A skipped
gate is not a pass and is counted separately.
EOF
    exit 2
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}
# assets/fonts is the checked-in artifact the Debian package installs. The
# build tree carries a copy that is not regenerated with it and has been
# observed stale and short of glyphs the manifest gate requires, so a local
# run must not reach for it by default.
FONT=${BLIT_GATE_FONT:-$ROOT/assets/fonts/Blit-Hibr-Terminus16.psf}
LIBS=${BLIT_GATE_LIBS:-$ROOT/build/sdl3-kms}
PACKAGE=${BLIT_GATE_PACKAGE:-}
LIVE=${BLIT_GATE_LIVE:-}
PORT=${BLIT_GATE_BASE_PORT:-19400}
# The adapter gate compiles hibr/console-blit.c, so it needs Hibr's headers.
# Default to the pinned checkout rather than any newer development tree:
# docs/development-workflow.md makes the version that matches
# packaging/HIBR_VERSION the only valid adapter build target, and a gate that
# proved the adapter against different headers than the package ships would
# be proving the wrong artifact.
if [ -z "${BLIT_HIBR_ROOT:-}" ]; then
    pinned_hibr=/tmp/hibr-$(sed -n '1p' "$ROOT/packaging/HIBR_VERSION")
    if [ -d "$pinned_hibr/include" ] && [ -d "$pinned_hibr/mods" ]; then
        BLIT_HIBR_ROOT=$pinned_hibr
        export BLIT_HIBR_ROOT
    fi
fi

pass=0
fail=0
skip=0
failed_gates=''

report() {
    # name verdict detail
    printf '%-38s %-5s %s\n' "$1" "$2" "$3"
}

skip_gate() {
    skip=$((skip + 1))
    report "$1" SKIP "$2"
}

# Several gates bind the same fixed port (19195, 19197, 19202 and 19249 are
# each used by three of them). Run back to back, one gate can reach its
# listen() before the previous gate's server has released the socket, and the
# failure looks like the gate rather than the sequencing. Reap stragglers in
# the gates' own range between runs and give the kernel a moment. The pattern
# deliberately excludes this script's own servers (19400+) and anything not
# named blit-server, so a production `blit` on its own port is never matched.
# Issue #28 removed the port-reaping that used to live here. Every gate that
# spawns a server now has a port of its own and waits for it, reporting a
# conflict as a conflict; a pkill between gates hid that behind a one-second
# sleep and killed servers this script did not start. If a port conflict
# reappears, the gate will say so by name.

# Run one gate. The caller has already decided it is runnable here.
run_gate() {
    name=$1
    shift
    out=$("$@" 2>&1) && rc=0 || rc=$?
    last=$(printf '%s' "$out" | tail -1 | cut -c1-72)
    if [ "$rc" -eq 0 ]; then
        pass=$((pass + 1))
        report "$name" PASS "$last"
    elif [ "$rc" -eq 77 ]; then
        # 77 is the established "not applicable on this host" status; the
        # physical multi-display gate already uses it.
        skip=$((skip + 1))
        report "$name" SKIP "not applicable here (77): $last"
    else
        # Keep the whole output of a failing gate, and name the file.  This
        # reported only the last line, so a gate that failed once in a suite
        # run and passed on its own left nothing to diagnose -- which is
        # exactly what happened to pointer_damage_atomic_gate.rb (issue #61).
        # An intermittent gate makes every later run ambiguous; losing its
        # message makes it permanent.
        fail=$((fail + 1))
        failed_gates="$failed_gates $name"
        failure_log="${BLIT_GATE_FAILURE_DIR:-/tmp}/blit-gate-$name.$$.out"
        printf '%s\n' "$out" > "$failure_log" 2>/dev/null || failure_log=''
        if [ -n "$failure_log" ]; then
            report "$name" FAIL "rc=$rc $last"
            printf '%s\n' "    output: $failure_log" >&2
        else
            report "$name" FAIL "rc=$rc $last"
        fi
    fi
}

# Some gates assert against a server they do not start. Own that lifecycle
# here so the gate cannot be run against a stale or absent one.
server_pid=''
start_server() {
    PORT=$((PORT + 1))
    BLIT_PSF_FONT="$FONT" SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$LIBS" \
        "$BINARY" "$PORT" >"$tmpdir/server-$PORT.log" 2>&1 &
    server_pid=$!
    i=0
    while [ "$i" -lt 50 ]; do
        if curl --fail --silent --max-time 1 \
            "http://127.0.0.1:$PORT/v1/version" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.2
    done
    echo "run-gates: server did not become ready on port $PORT" >&2
    cat "$tmpdir/server-$PORT.log" >&2
    return 1
}

stop_server() {
    [ -n "$server_pid" ] || return 0
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=''
}

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/blit-run-gates.XXXXXX")
cleanup() {
    stop_server
    rm -rf "$tmpdir"
}
trap cleanup EXIT HUP INT TERM

suite_schema() {
    run_gate api_schema_gate.rb ruby tools/tests/api_schema_gate.rb
    run_gate observation_api_gate.rb ruby tools/tests/observation_api_gate.rb
    run_gate stream_replay_gate.rb ruby tools/tests/stream_replay_gate.rb
    run_gate diagnostic_auth_gate.sh "$ROOT"/tools/tests/diagnostic_auth_gate.sh
    # network_auth_gate.sh invokes http_fragmentation_gate.rb and
    # http_backpressure_gate.rb itself, passing them its port. They are
    # sub-gates: running either standalone connects to nothing and raises.
    if [ -x "$BINARY" ]; then
        run_gate network_auth_gate.sh env BLIT_GATE_BINARY="$BINARY" \
            "$ROOT"/tools/tests/network_auth_gate.sh
    else
        skip_gate network_auth_gate.sh "needs $BINARY"
    fi
}

suite_terminal() {
    if start_server; then
        run_gate terminal_conformance.rb env \
            BLIT_TEST_SERVER="http://127.0.0.1:$PORT" \
            ruby tools/tests/terminal_conformance.rb
        stop_server
    else
        skip_gate terminal_conformance.rb "server would not start"
    fi
    run_gate session_lifecycle_gate.sh "$ROOT"/tools/tests/session_lifecycle_gate.sh
    run_gate session_write_resize_gate.rb ruby tools/tests/session_write_resize_gate.rb
}

suite_font() {
    run_gate font_api_gate.rb ruby tools/tests/font_api_gate.rb
    run_gate font_identity_gate.rb ruby tools/tests/font_identity_gate.rb
    run_gate wide_glyph_gate.rb ruby tools/tests/wide_glyph_gate.rb
    run_gate sdl_glyph_render_gate.rb ruby tools/tests/sdl_glyph_render_gate.rb
    run_gate hibr_font_manifest_gate.py python3 tools/tests/hibr_font_manifest_gate.py
    run_gate font_fit_gate.py python3 tools/tests/font_fit_gate.py
    if [ -d "$ROOT/vendor/SDL3/include" ]; then
        run_gate sdl_glyph_filter_gate.sh "$ROOT"/tools/tests/sdl_glyph_filter_gate.sh
    else
        skip_gate sdl_glyph_filter_gate.sh "needs vendor/SDL3/include"
    fi
}

suite_renderer() {
    run_gate pixel_frame_gate.rb ruby tools/tests/pixel_frame_gate.rb
    run_gate hidden_cursor_damage_gate.rb ruby tools/tests/hidden_cursor_damage_gate.rb
    run_gate pointer_damage_atomic_gate.rb ruby tools/tests/pointer_damage_atomic_gate.rb
    run_gate evdev_scan_gate.rb ruby tools/tests/evdev_scan_gate.rb
    run_gate evdev_open_latency_gate.rb ruby tools/tests/evdev_open_latency_gate.rb
    run_gate input_identity_gate.sh "$ROOT"/tools/tests/input_identity_gate.sh
    run_gate pointer_curve_gate.sh "$ROOT"/tools/tests/pointer_curve_gate.sh
    run_gate audio_control_gate.sh "$ROOT"/tools/tests/audio_control_gate.sh
    run_gate display_geometry_gate.sh "$ROOT"/tools/tests/display_geometry_gate.sh
    run_gate evdev_char_gate.sh "$ROOT"/tools/tests/evdev_char_gate.sh
    run_gate input_device_api_gate.rb ruby tools/tests/input_device_api_gate.rb
    run_gate input_device_config_gate.rb ruby tools/tests/input_device_config_gate.rb
    run_gate settings_persistence_gate.rb ruby tools/tests/settings_persistence_gate.rb
    run_gate login_policy_gate.rb ruby tools/tests/login_policy_gate.rb
    run_gate input_hotplug_cycle_gate.rb ruby tools/tests/input_hotplug_cycle_gate.rb
    run_gate input_device_route_gate.rb ruby tools/tests/input_device_route_gate.rb
    run_gate audio_device_api_gate.rb ruby tools/tests/audio_device_api_gate.rb
    run_gate audio_card_gate.rb ruby tools/tests/audio_card_gate.rb
    run_gate display_device_api_gate.rb ruby tools/tests/display_device_api_gate.rb
    run_gate pointer_target_gate.rb ruby tools/tests/pointer_target_gate.rb
    run_gate pointer_wallpaper_stress_gate.rb ruby tools/tests/pointer_wallpaper_stress_gate.rb
    run_gate external_pointer_composition_gate.rb ruby tools/tests/external_pointer_composition_gate.rb
    run_gate external_pointer_full_rebuild_gate.rb env \
        BLIT_TEST_SCENE_CACHE=0 BLIT_EXTERNAL_POINTER_PORT=19282 \
        ruby tools/tests/external_pointer_composition_gate.rb
    run_gate sdl_background_batch_gate.rb ruby tools/tests/sdl_background_batch_gate.rb
    run_gate sdl_graphics_cache_gate.rb ruby tools/tests/sdl_graphics_cache_gate.rb
    run_gate kitty_query_gate.rb ruby tools/tests/kitty_query_gate.rb
    run_gate kitty_placement_gate.rb ruby tools/tests/kitty_placement_gate.rb
    run_gate graphics_reset_gate.rb ruby tools/tests/graphics_reset_gate.rb
    run_gate sixel_decoder_gate.rb ruby tools/tests/sixel_decoder_gate.rb
    run_gate sixel_composition_gate.rb ruby tools/tests/sixel_composition_gate.rb
    run_gate graphics_trace_gate.rb ruby tools/tests/graphics_trace_gate.rb
    run_gate sync_output_gate.rb ruby tools/tests/sync_output_gate.rb
    run_gate cell_paint_gate.rb ruby tools/tests/cell_paint_gate.rb
    run_gate motion_trail_gate.rb ruby tools/tests/motion_trail_gate.rb
    run_gate deferred_damage_gate.rb ruby tools/tests/deferred_damage_gate.rb
    run_gate pointer_geometry_gate.rb ruby tools/tests/pointer_geometry_gate.rb
    run_gate mouse_report_gate.rb ruby tools/tests/mouse_report_gate.rb
    run_gate key_encoding_gate.rb ruby tools/tests/key_encoding_gate.rb
    run_gate input_stream_text_gate.rb ruby tools/tests/input_stream_text_gate.rb
    run_gate keyboard_settings_gate.rb ruby tools/tests/keyboard_settings_gate.rb
    run_gate pointer_settings_gate.rb ruby tools/tests/pointer_settings_gate.rb
    run_gate display_settings_gate.rb ruby tools/tests/display_settings_gate.rb
    run_gate sdl_wallpaper_damage_gate.rb ruby tools/tests/sdl_wallpaper_damage_gate.rb
    run_gate sdl_graphics_band_gate.rb ruby tools/tests/sdl_graphics_band_gate.rb
    run_gate sixel_slot_gate.rb ruby tools/tests/sixel_slot_gate.rb
    run_gate kms_compose_gate.sh "$ROOT"/tools/tests/kms_compose_gate.sh
    run_gate pointer_art_gate.sh sh "$ROOT/tools/tests/pointer_art_gate.sh"
    run_gate pointer_appearance_gate.rb ruby "$ROOT/tools/tests/pointer_appearance_gate.rb"
    run_gate pointer_pixel_gate.rb ruby "$ROOT/tools/tests/pointer_pixel_gate.rb"
    run_gate kms_graphics_clip_gate.sh sh "$ROOT/tools/tests/kms_graphics_clip_gate.sh"
    run_gate kms_hotplug_gate.rb ruby "$ROOT"/tools/tests/kms_hotplug_gate.rb
    run_gate kms_hotplug_cycles.rb ruby "$ROOT"/tools/tests/kms_hotplug_cycles.rb
    run_gate surface_damage_gate.sh sh "$ROOT"/tools/tests/surface_damage_gate.sh
    run_gate screenshot_overlay_gate.sh "$ROOT"/tools/tests/screenshot_overlay_gate.sh
    run_gate render_state_agreement_gate.rb ruby tools/tests/render_state_agreement_gate.rb
    run_gate pane_split_geometry_gate.rb ruby tools/tests/pane_split_geometry_gate.rb
    run_gate display_layout_gate.rb ruby tools/tests/display_layout_gate.rb
}

suite_gles() {
    run_gate sdl_gles_sixel_composition env \
        BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT" BLIT_GATE_LIBS="$LIBS" \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 ruby tools/tests/sixel_composition_gate.rb
    run_gate sdl_gles_glyphs env \
        BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT" BLIT_GATE_LIBS="$LIBS" \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 ruby tools/tests/sdl_glyph_render_gate.rb
    run_gate sdl_gles_graphics_bands env \
        BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT" BLIT_GATE_LIBS="$LIBS" \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 ruby tools/tests/sdl_graphics_band_gate.rb
    run_gate sdl_gles_motion_trails env \
        BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT" BLIT_GATE_LIBS="$LIBS" \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 ruby tools/tests/motion_trail_gate.rb
    run_gate sdl_gles_wallpaper_damage env \
        BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT" BLIT_GATE_LIBS="$LIBS" \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 ruby tools/tests/sdl_wallpaper_damage_gate.rb
    run_gate sdl_gles_scene_and_metrics env \
        BLIT_TEST_VIDEO_DRIVER=offscreen BLIT_TEST_RENDERER=opengles2 \
        LIBGL_ALWAYS_SOFTWARE=1 sh tools/tests/surface_damage_gate.sh
}

suite_hibr() {
    run_gate auxiliary_native_gate.rb ruby tools/tests/auxiliary_native_gate.rb
    run_gate auxiliary_desktop_gate.rb env BLIT_NATIVE_AUXILIARY_DESKTOP=1 BLIT_NATIVE_AUXILIARY_RESIZE=1 ruby tools/tests/auxiliary_native_gate.rb
    run_gate native_hold_observer_gate.rb ruby tools/tests/native_hold_observer_gate.rb
    run_gate native_join_pty_gate.rb ruby tools/tests/native_join_pty_gate.rb
    run_gate hibr_singleton_gate.rb ruby tools/tests/hibr_singleton_gate.rb
    run_gate hibr_join_viewport_gate.rb ruby tools/tests/hibr_join_viewport_gate.rb
    run_gate hibr_join_frame_barrier_gate.rb ruby tools/tests/hibr_join_frame_barrier_gate.rb
    run_gate hibr_split_capture_gate.rb ruby tools/tests/hibr_split_capture_gate.rb
    run_gate hibr_join_placement_gate.sh "$ROOT"/tools/tests/hibr_join_placement_gate.sh
    # Needs only the Hibr headers, not a Hibr process: it compiles the shipped
    # display provider into a harness. Skips (77) without BLIT_HIBR_ROOT.
    run_gate adapter_coalesce_gate.sh "$ROOT"/tools/tests/adapter_coalesce_gate.sh
    run_gate adapter_input_gate.sh "$ROOT"/tools/tests/adapter_input_gate.sh
    run_gate adapter_provider_gate.sh "$ROOT"/tools/tests/adapter_provider_gate.sh
    run_gate desktop_repaint_gate.rb ruby tools/tests/desktop_repaint_gate.rb
    run_gate output_occupancy_gate.rb ruby tools/tests/output_occupancy_gate.rb
    run_gate hibr_resume_guard_gate.sh "$ROOT"/tools/tests/hibr_resume_guard_gate.sh
    # QEMU is the repeatable multi-display oracle and is never implied by a
    # local run; invoke tools/tests/qemu_hibr_lifecycle_gate.sh directly.
    skip_gate qemu_hibr_lifecycle_gate.sh "QEMU oracle, run explicitly"
}

suite_lifecycle() {
    run_gate backend_selection_gate.rb ruby tools/tests/backend_selection_gate.rb
    run_gate backend_comparison_cleanup_gate.rb ruby tools/tests/backend_comparison_cleanup_gate.rb
    run_gate auxiliary_policy_gate.rb ruby tools/tests/auxiliary_policy_gate.rb
    run_gate login_desktop_observer_gate.rb ruby tools/tests/login_desktop_observer_gate.rb
    run_gate login_identity_gate.rb ruby tools/tests/login_identity_gate.rb
    run_gate login_manager_gate.sh "$ROOT"/tools/tests/login_manager_gate.sh
    run_gate restart_gate.sh "$ROOT"/tools/tests/restart_gate.sh
    run_gate shutdown_gate.sh "$ROOT"/tools/tests/shutdown_gate.sh
    run_gate shutdown_key_gate.rb ruby tools/tests/shutdown_key_gate.rb
    run_gate update_idle_timeout_gate.sh "$ROOT"/tools/tests/update_idle_timeout_gate.sh
    run_gate update_reset_gate.sh "$ROOT"/tools/tests/update_reset_gate.sh
    run_gate update_restart_gate.sh "$ROOT"/tools/tests/update_restart_gate.sh
    run_gate update_cancel_gate.sh "$ROOT"/tools/tests/update_cancel_gate.sh
}

suite_packaging() {
    if [ -n "$PACKAGE" ]; then
        run_gate package_artifact_gate.sh "$ROOT"/tools/tests/package_artifact_gate.sh "$PACKAGE"
    else
        skip_gate package_artifact_gate.sh "set BLIT_GATE_PACKAGE to a .deb"
    fi
}

suite_timing() {
    run_gate frame_rate_gate.rb ruby tools/tests/frame_rate_gate.rb
    if start_server; then
        run_gate terminal_soak_gate.rb env \
            BLIT_TEST_SERVER="http://127.0.0.1:$PORT" \
            BLIT_SOAK_PID="$server_pid" \
            ruby tools/tests/terminal_soak_gate.rb
        stop_server
    else
        skip_gate terminal_soak_gate.rb "server would not start"
    fi
    # Physical multi-display and the live screenshot gate assert against a
    # named running host, not a local fixture.
    if [ -n "$LIVE" ]; then
        run_gate physical_multi_display_gate.rb env \
            BLIT_GATE_SERVER="$LIVE" ruby tools/tests/physical_multi_display_gate.rb
        run_gate screenshot_nonblocking_gate.sh \
            "$ROOT"/tools/tests/screenshot_nonblocking_gate.sh "$LIVE" display-1
    else
        skip_gate physical_multi_display_gate.rb "set BLIT_GATE_LIVE to a host URL"
        skip_gate screenshot_nonblocking_gate.sh "set BLIT_GATE_LIVE to a host URL"
    fi
}

[ "$#" -gt 0 ] || usage
for argument in "$@"; do
    case "$argument" in
        -h|--help) usage ;;
        schema|terminal|font|renderer|gles|hibr|lifecycle|packaging|timing|all) ;;
        *) echo "run-gates: unknown suite '$argument'" >&2; usage ;;
    esac
done

test -x "$BINARY" || {
    echo "run-gates: server binary not executable: $BINARY" >&2
    echo "run-gates: build it with tools/build-tcc.sh or set BLIT_GATE_BINARY" >&2
    exit 2
}
test -f "$FONT" || {
    echo "run-gates: font not found: $FONT (set BLIT_GATE_FONT)" >&2
    exit 2
}

printf 'run-gates: binary=%s\n' "$BINARY"
printf 'run-gates: version=%s font=%s\n' \
    "$(sed -n '1p' "$ROOT/packaging/VERSION")" "$(basename "$FONT")"
printf '\n'

for argument in "$@"; do
    case "$argument" in
        all)
            suite_schema; suite_terminal; suite_font; suite_renderer
            suite_hibr; suite_lifecycle; suite_packaging; suite_timing
            ;;
        *) "suite_$argument" ;;
    esac
done

printf '\n'
if [ "$fail" -eq 0 ]; then
    printf 'RUN_GATES_PASS pass=%d skip=%d fail=0\n' "$pass" "$skip"
    exit 0
fi
printf 'RUN_GATES_FAIL pass=%d skip=%d fail=%d failed:%s\n' \
    "$pass" "$skip" "$fail" "$failed_gates"
exit 1
