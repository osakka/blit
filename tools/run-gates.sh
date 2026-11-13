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
  font        glyph coverage, UTF-8 snapshots, font API, raster paths
  renderer    native KMS pixel, damage, pointer and graphics composition
  renderer-gles  the renderer gates that depend on the compositor, on GLES;
              sets BLIT_GATE_BACKEND=gles, run it by itself
  hibr        singleton ownership and resume fail-closed behaviour
  lifecycle   restart, shutdown, supervised update handling
  packaging   the .deb artifact gate (needs BLIT_GATE_PACKAGE)
  timing      measured presentation cadence and the bounded PTY soak
  all         every suite above

The display is native KMS on the DRM mock (ADR 0184); BLIT_GATE_BACKEND=gles
runs the same suites through the GLES compositor (surfaceless, needs EGL).

Environment:
  BLIT_GATE_JOBS     gates run at once (default min(4, nproc/2)); 1 is serial.
                     Load-sensitive gates always run alone, after the rest
  BLIT_GATE_TIMEOUT  seconds one gate may take (default 300; a few known long
                     gates have more). A gate over it is killed and FAILs
  BLIT_GATE_PORT_OFFSET  added to every fixed gate port (stride 1000 per
                     worker); give each checkout or agent running gates at
                     the same time its own, e.g. 0, 4000, 8000
  BLIT_GATE_BINARY   server under test  (default build/blit/blit-server)
  BLIT_GATE_FONT     PSF font           (default the bundled Hibr font)
  BLIT_GATE_OUTLINE  outline font; with BLIT_GATE_COLOUR enables raster module gate
  BLIT_GATE_COLOUR   colour bitmap font; requires FreeType headers/library
  PKG_CONFIG_PATH    where pkg-config finds the build dependencies
                     (tools/build-flags.sh, docs/toolchain.md)
  BLIT_GATE_BACKEND  native (default) or gles
  BLIT_GATE_SERVER_ENV  NAME=value words given to every gate server. Gate
                     servers inherit no other BLIT_*, HIBR_*, LISTEN_* or
                     WAYLAND_DISPLAY and open no real input device
                     (tools/tests/gate_env.rb, .py, .sh)
  BLIT_GATE_PACKAGE  .deb path; enables the packaging artifact gate
  BLIT_GATE_LIVE     live base URL; enables the live-host-only gates
  BLIT_GATE_LIVE_DMABUF  live host whose GPU imports dmabufs (BLIT_AUTH_TOKEN
                     its API token); enables frame_dmabuf_live_gate (ADR 0223)
  BLIT_SOAK_SECONDS  soak duration      (default 120, gate's own default)
  BLIT_HIBR_ROOT     Hibr checkout providing the headers the adapter gate
                     compiles against (default /tmp/hibr-<packaging/HIBR_VERSION>
                     when present; without it that gate skips)

Every report line ends with the gate's wall time; the run ends with the 20
slowest gates and the time per suite (ADR 0216).

Exit status is 0 only when every gate that ran reported success. A skipped
gate is not a pass and is counted separately.
EOF
    exit 2
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GATE_DISPLAY_SOURCE=$ROOT/tools/tests/gate_display.sh
. "$ROOT/tools/tests/gate_display.sh"
cd "$ROOT"

BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}
# assets/fonts is the checked-in artifact the Debian package installs. The
# build tree carries a copy that is not regenerated with it and has been
# observed stale and short of glyphs the manifest gate requires, so a local
# run must not reach for it by default.
FONT=${BLIT_GATE_FONT:-$ROOT/assets/fonts/Blit-Terminus16.psf}
PACKAGE=${BLIT_GATE_PACKAGE:-}
LIVE=${BLIT_GATE_LIVE:-}
LIVE_DMABUF=${BLIT_GATE_LIVE_DMABUF:-}
BASE_PORT=${BLIT_GATE_BASE_PORT:-19400}
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

# ADR 0216: a gate runner that is measured, bounded and parallel.
#
#   BLIT_GATE_JOBS         gates run at once (default min(4, nproc/2)); 1 is
#                          the serial run this script always was.
#   BLIT_GATE_TIMEOUT      seconds a gate may take (default 300; a few known
#                          long gates have their own, see gate_timeout).
#   BLIT_GATE_PORT_OFFSET  added to every fixed gate port, so a second
#                          checkout or agent can run at the same time. Each
#                          parallel worker adds its own stride of 1000 to it.
default_jobs=$(( $(nproc 2>/dev/null || echo 2) / 2 ))
[ "$default_jobs" -le 4 ] || default_jobs=4
[ "$default_jobs" -ge 1 ] || default_jobs=1
JOBS=${BLIT_GATE_JOBS:-$default_jobs}
PORT_OFFSET=${BLIT_GATE_PORT_OFFSET:-0}
GATE_PORT_STRIDE=1000
# The fixed gate ports span 18995..19900 (tools/tests/gate_port.rb).
GATE_PORT_TOP=19900
case $JOBS in ''|*[!0-9]*|0) echo "run-gates: BLIT_GATE_JOBS must be a positive integer" >&2; exit 2 ;; esac
case $PORT_OFFSET in ''|*[!0-9]*) echo "run-gates: BLIT_GATE_PORT_OFFSET must be a non-negative integer" >&2; exit 2 ;; esac
case ${BLIT_GATE_TIMEOUT:-300} in ''|*[!0-9]*|0) echo "run-gates: BLIT_GATE_TIMEOUT must be a positive integer (seconds)" >&2; exit 2 ;; esac
# Stay under the kernel's ephemeral range (32768+), or a gate's port could be
# taken by an outgoing connection.
if [ $((GATE_PORT_TOP + PORT_OFFSET + (JOBS - 1) * GATE_PORT_STRIDE)) -ge 32768 ]; then
    echo "run-gates: BLIT_GATE_PORT_OFFSET=$PORT_OFFSET with $JOBS jobs reaches the ephemeral port range; lower one of them" >&2
    exit 2
fi
export BLIT_GATE_PORT_OFFSET=$PORT_OFFSET
# The helper that owns a server for gates that assert against one.
export BLIT_GATE_BINARY="$BINARY" BLIT_GATE_FONT="$FONT"

# Seconds a gate may take before it is killed and reported as a failure.
# BLIT_GATE_TIMEOUT, when set, applies to every gate; otherwise the default is
# 300 s, with room for the gates that spend their time waiting on purpose.
gate_timeout() {
    if [ -n "${BLIT_GATE_TIMEOUT:-}" ]; then
        echo "$BLIT_GATE_TIMEOUT"
        return
    fi
    case $1 in
    terminal_soak_gate.rb) echo $(( ${BLIT_SOAK_SECONDS:-120} + 300 )) ;;
    package_artifact_gate.sh) echo 1200 ;;
    output_occupancy_gate.rb|restart_after_update_gate.rb|hibr_identity_refresh_gate.rb) echo 600 ;;
    # CPU-bound, not waiting: 170 to 235 s alone and past 300 s when several
    # gates share the machine, so the default is too tight for it.
    emoji_presentation_gate.rb) echo 600 ;;
    *) echo 300 ;;
    esac
}

now_ms() {
    _ns=$(date +%s%N)
    echo $((_ns / 1000000))
}

fmt_ms() {
    printf '%d.%ds' $(($1 / 1000)) $(($1 % 1000 / 100))
}

report() {
    # name verdict detail
    printf '%-38s %-5s %s\n' "$1" "$2" "$3"
}

# One gate is one task, numbered in the order the suites call run_gate and
# skip_gate. Every process that walks the suites numbers them identically,
# which is what lets workers share the work without sharing a queue file:
#
#   inline  (JOBS=1)   run each gate where it is called and report it: the
#                      serial runner this script was.
#   worker             claim task N (mkdir is atomic), run it if it is in this
#                      worker's lane, store its result.
#   print              the coordinator: wait for task N's result and report it
#                      in suite order, so the report reads the same as a
#                      serial run however the gates were scheduled.
MODE=inline
WORKER_LANE=parallel
idx=0
gpid=''
cursuite=''

skip_gate() {
    idx=$((idx + 1))
    [ "$MODE" != worker ] || return 0
    skip=$((skip + 1))
    report "$1" SKIP "$2"
    printf '%s %s %s\n' 0 "$cursuite" "$1" >>"$tmpdir/timing"
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

# Run one gate under a timeout and store what happened. The caller has already
# decided it is runnable here. timeout puts the gate in its own process group
# and signals the whole group, so a hung gate's blit-server goes with it.
# The gate runs in the background and is waited for so that a signal to this
# script is handled at once and takes the gate down with it.
execute_gate() {
    g_idx=$1
    g_name=$2
    g_limit=$(gate_timeout "$g_name")
    shift 2
    g_out=$tmpdir/out.$g_idx
    g_t0=$(now_ms)
    timeout -k 10 "$g_limit" "$@" </dev/null >"$g_out" 2>&1 &
    gpid=$!
    wait "$gpid" && rc=0 || rc=$?
    gpid=''
    g_ms=$(( $(now_ms) - g_t0 ))
    out=$(cat "$g_out")
    rm -f "$g_out"
    last=$(printf '%s' "$out" | tail -1 | cut -c1-72)
    g_log=''
    if [ "$rc" -eq 0 ]; then
        g_verdict=PASS
        g_detail=$last
    elif [ "$rc" -eq 77 ]; then
        # 77 is the established "not applicable on this host" status; the
        # physical multi-display gate already uses it.
        g_verdict=SKIP
        g_detail="not applicable here (77): $last"
    else
        # Keep the whole output of a failing gate, and name the file.  This
        # reported only the last line, so a gate that failed once in a suite
        # run and passed on its own left nothing to diagnose -- which is
        # An intermittent gate makes every later run ambiguous; losing its
        # message makes it permanent.
        g_verdict=FAIL
        if [ "$rc" -eq 124 ] || { [ "$rc" -eq 137 ] && [ "$g_ms" -ge $((g_limit * 1000)) ]; }; then
            # Not "failed", not retried: it took longer than it is allowed to.
            g_detail="rc=$rc TIMEOUT after ${g_limit}s: $last"
        else
            g_detail="rc=$rc $last"
        fi
        g_log="${BLIT_GATE_FAILURE_DIR:-/tmp}/blit-gate-$g_name.$$.out"
        printf '%s\n' "$out" >"$g_log" 2>/dev/null || g_log=''
    fi
    # The result is complete before .done exists; the coordinator reads only
    # after it.
    printf '%s\n' "$g_name" >"$tmpdir/res/$g_idx.name"
    printf '%s\n' "$g_verdict" >"$tmpdir/res/$g_idx.verdict"
    printf '%s\n' "$g_detail" >"$tmpdir/res/$g_idx.detail"
    printf '%s\n' "$g_ms" >"$tmpdir/res/$g_idx.ms"
    printf '%s\n' "$g_log" >"$tmpdir/res/$g_idx.log"
    : >"$tmpdir/res/$g_idx.done"
}

# Report task $1 from its stored result, in the format this script has always
# used, with the time the gate took appended.
print_gate() {
    p_idx=$1
    p_name=$(cat "$tmpdir/res/$p_idx.name")
    p_verdict=$(cat "$tmpdir/res/$p_idx.verdict")
    p_detail=$(cat "$tmpdir/res/$p_idx.detail")
    p_ms=$(cat "$tmpdir/res/$p_idx.ms")
    p_log=$(cat "$tmpdir/res/$p_idx.log")
    printf '%s %s %s\n' "$p_ms" "$cursuite" "$p_name" >>"$tmpdir/timing"
    case $p_verdict in
    PASS) pass=$((pass + 1)) ;;
    SKIP) skip=$((skip + 1)) ;;
    FAIL)
        fail=$((fail + 1))
        failed_gates="$failed_gates $p_name"
        ;;
    esac
    report "$p_name" "$p_verdict" "$p_detail ($(fmt_ms "$p_ms"))"
    if [ "$p_verdict" = FAIL ] && [ -n "$p_log" ]; then
        printf '%s\n' "    output: $p_log" >&2
    fi
}

# Coordinator: wait for task $1. A scheduler that is gone without having
# produced it is a failure of the runner, said as such.
wait_gate() {
    w_idx=$1
    w_name=$2
    while [ ! -f "$tmpdir/res/$w_idx.done" ]; do
        if ! kill -0 "$sched_pid" 2>/dev/null && [ ! -f "$tmpdir/res/$w_idx.done" ]; then
            printf '%s\n' "$w_name" >"$tmpdir/res/$w_idx.name"
            printf 'FAIL\n' >"$tmpdir/res/$w_idx.verdict"
            printf 'runner: the worker exited without a result\n' >"$tmpdir/res/$w_idx.detail"
            printf '0\n' >"$tmpdir/res/$w_idx.ms"
            : >"$tmpdir/res/$w_idx.log"
            return
        fi
        sleep 0.2
    done
}

# Gates whose result does not depend on which compositor presents: they build
# and drive their own C probe, run the CPU path on purpose, or never open a
# display. `renderer-gles` leaves them out; `renderer` and `all` still run them.
# Not a coverage cut: the native pass proves them, and the compositor cannot
# change what they assert.
backend_independent() {
    case $1 in
    graphics_ingestion|graphics_placement_damage|pixel_frame_gate.rb|\
    input_identity_gate.sh|pointer_curve_gate.sh|audio_control_gate.sh|\
    evdev_char_gate.sh|native_vt_kernel_gate.sh|vt_watchdog_report_gate.sh|\
    touchpad_gate.sh|motion_trail_gate.rb|graphics_band_gate.rb|\
    kms_compose_gate.sh|gles_compose_gate.sh|pointer_art_gate.sh|\
    kms_graphics_clip_gate.sh|kms_hotplug_cycles.rb|kms_pointer_phase_gate.rb|\
    surface_damage_gate.sh|kms_hotplug_gate.rb|display_layout_gate.rb) return 0 ;;
    esac
    return 1
}
ONLY_SENSITIVE=''

gate_lane() {
    lane=$1
    name=$2
    shift 2
    # Left out of every mode alike, so the task numbers still agree.
    if [ -n "$ONLY_SENSITIVE" ] && backend_independent "$name"; then
        return 0
    fi
    idx=$((idx + 1))
    case $MODE in
    inline)
        execute_gate "$idx" "$name" "$@"
        print_gate "$idx"
        ;;
    worker)
        [ "$lane" = "$WORKER_LANE" ] || return 0
        mkdir "$tmpdir/claim.$idx" 2>/dev/null || return 0
        execute_gate "$idx" "$name" "$@"
        ;;
    print)
        wait_gate "$idx" "$name"
        print_gate "$idx"
        ;;
    esac
}

# A gate that can run beside others.
run_gate() {
    gate_lane parallel "$@"
}

# A gate whose result depends on the machine being quiet -- a cadence, a
# frame rate, a "was it saved within N ms", a soak. It runs in the timing
# lane: alone, after every parallel gate has finished. Under BLIT_GATE_JOBS=1
# there is nothing to be alone from and it runs where it is called.
run_serial_gate() {
    gate_lane serial "$@"
}

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/blit-run-gates.XXXXXX")
mkdir "$tmpdir/res"
: >"$tmpdir/timing"
sched_pid=''
cleanup() {
    trap - EXIT HUP INT TERM
    [ -z "$gpid" ] || kill -TERM "$gpid" 2>/dev/null || true
    if [ -n "$sched_pid" ]; then
        kill -TERM "$sched_pid" 2>/dev/null || true
        wait "$sched_pid" 2>/dev/null || true
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' HUP TERM

suite_schema() {
    run_gate package_entrypoint_policy_gate.rb ruby tools/tests/package_entrypoint_policy_gate.rb
    run_gate api_schema_gate.rb ruby tools/tests/api_schema_gate.rb
    run_gate run_gates_gate.rb ruby tools/tests/run_gates_gate.rb
    run_gate gate_env_gate.rb ruby tools/tests/gate_env_gate.rb
    run_gate documentation_gate.rb ruby tools/tests/documentation_gate.rb
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
    run_gate rectangle_copy_gate.rb ruby tools/tests/rectangle_copy_gate.rb
    run_gate kitty_keyboard_gate.rb ruby tools/tests/kitty_keyboard_gate.rb
    run_gate terminal_conformance.rb sh "$ROOT"/tools/tests/gate_with_server.sh \
        $((BASE_PORT + 1)) ruby tools/tests/terminal_conformance.rb
    run_gate session_lifecycle_gate.sh "$ROOT"/tools/tests/session_lifecycle_gate.sh
    run_gate session_write_resize_gate.rb ruby tools/tests/session_write_resize_gate.rb
    run_gate layout_reassert_gate.rb ruby tools/tests/layout_reassert_gate.rb
    run_gate terminal_ingest_gate.rb ruby tools/tests/terminal_ingest_gate.rb
    run_gate child_environment_gate.rb ruby tools/tests/child_environment_gate.rb
}

suite_font() {
    run_gate raster_blend ruby tools/tests/raster_blend_gate.rb
    run_gate font_api_gate.rb ruby tools/tests/font_api_gate.rb
    run_gate font_identity_gate.rb ruby tools/tests/font_identity_gate.rb
    run_gate wide_glyph_gate.rb ruby tools/tests/wide_glyph_gate.rb
    run_gate glyph_render_gate.rb ruby tools/tests/glyph_render_gate.rb
    run_gate hibr_font_manifest_gate.py python3 tools/tests/hibr_font_manifest_gate.py
    run_gate font_fit_gate.py python3 tools/tests/font_fit_gate.py
    run_gate font_assets_gate.rb ruby tools/tests/font_assets_gate.rb
    run_gate font_discovery_gate.rb ruby tools/tests/font_discovery_gate.rb
    run_gate spleen_master_gate.rb ruby tools/tests/spleen_master_gate.rb
    run_gate font_reference_gate.rb ruby tools/tests/font_reference_gate.rb
    run_gate powerline_geometry_gate.rb ruby tools/tests/powerline_geometry_gate.rb
    run_gate bar_corner_gate.rb ruby tools/tests/bar_corner_gate.rb
    run_gate window_glyph_gate.rb ruby tools/tests/window_glyph_gate.rb
    run_gate control_glyph_gate.rb ruby tools/tests/control_glyph_gate.rb
    run_gate catalogue_fields_gate.rb ruby tools/tests/catalogue_fields_gate.rb
    run_gate outline_catalogue_gate.rb ruby tools/tests/outline_catalogue_gate.rb
    run_gate custom_icon_gate.rb ruby tools/tests/custom_icon_gate.rb
    run_gate icon_clip_gate.rb ruby tools/tests/icon_clip_gate.rb
    run_gate pixel_font_stack_gate.rb ruby tools/tests/pixel_font_stack_gate.rb
    run_gate custom_icon_api_gate.rb ruby tools/tests/custom_icon_api_gate.rb
    run_gate arabic_raster_gate.rb ruby tools/tests/arabic_raster_gate.rb
    run_gate arabic_word_gate.py python3 tools/tests/arabic_word_gate.py
    run_gate arabic_kms_ink ruby tools/tests/arabic_backend_gate.rb
    run_gate emoji_corpus_gate.rb ruby tools/tests/emoji_corpus_gate.rb
    run_gate emoji_presentation_gate.rb ruby tools/tests/emoji_presentation_gate.rb
    run_gate cell_cluster_gate.rb ruby tools/tests/cell_cluster_gate.rb
    run_gate emoji_layout_gate.rb ruby tools/tests/emoji_layout_gate.rb
    run_gate emoji_layout_api_gate.rb ruby tools/tests/emoji_layout_api_gate.rb
    run_gate emoji_pty_api_gate.rb ruby tools/tests/emoji_pty_api_gate.rb
    run_gate emoji_transition_gate.rb ruby tools/tests/emoji_transition_gate.rb
    run_gate harfbuzz_identity_gate.rb ruby tools/tests/harfbuzz_identity_gate.rb
    run_gate emoji_cluster_raster_gate.rb ruby tools/tests/emoji_cluster_raster_gate.rb
    run_gate font_cache_workload.rb ruby tools/probes/font_cache_workload.rb --check
    run_gate emoji_font_audit_gate.rb ruby tools/tests/emoji_font_audit_gate.rb
    run_gate modern_nerd_gate.sh sh tools/tests/modern_nerd_gate.sh
    run_gate font_fallback_gate.sh sh tools/tests/font_fallback_gate.sh
    run_gate font_raster_gate.sh sh tools/tests/font_raster_gate.sh \
        "$FONT" "${BLIT_GATE_OUTLINE:-$ROOT/assets/fonts/text/Hack-Regular.ttf}" \
        "${BLIT_GATE_COLOUR:-$ROOT/assets/fonts/NotoColorEmoji.ttf}"
    run_gate font_hinting_gate.sh sh tools/tests/font_hinting_gate.sh \
        "$FONT" "${BLIT_GATE_OUTLINE:-$ROOT/assets/fonts/text/Hack-Regular.ttf}" \
        "${BLIT_GATE_COLOUR:-$ROOT/assets/fonts/NotoColorEmoji.ttf}"
    run_gate font_hinting_api_gate.rb ruby tools/tests/font_hinting_api_gate.rb
    run_gate font_icon_colour_gate.sh sh tools/tests/font_icon_colour_gate.sh \
        "$FONT" "${BLIT_GATE_OUTLINE:-$ROOT/assets/fonts/text/Hack-Regular.ttf}" \
        "${BLIT_GATE_COLOUR:-$ROOT/assets/fonts/NotoColorEmoji.ttf}"
    run_gate font_icon_shape_gate.sh sh tools/tests/font_icon_shape_gate.sh \
        "$FONT" "${BLIT_GATE_OUTLINE:-$ROOT/assets/fonts/text/Hack-Regular.ttf}" \
        "${BLIT_GATE_COLOUR:-$ROOT/assets/fonts/NotoColorEmoji.ttf}"
    run_gate font_icon_colour_settings_gate.rb ruby tools/tests/font_icon_colour_settings_gate.rb
    run_gate font_kms_raster sh tools/tests/font_backend_raster_gate.sh
    run_gate retro_outline_raster sh tools/tests/font_raster_gate.sh \
        "$FONT" "$ROOT/assets/fonts/text/3270-Regular.ttf" \
        "$ROOT/assets/fonts/NotoColorEmoji.ttf" 20
    run_gate retro_outline_kms env BLIT_GATE_OUTLINE="$ROOT/assets/fonts/text/3270-Regular.ttf" \
        sh tools/tests/font_backend_raster_gate.sh
    run_gate modern_font_api_gate.rb ruby tools/tests/modern_font_api_gate.rb
    run_gate host_font_probe_gate.rb ruby tools/tests/host_font_probe_gate.rb
}

suite_renderer() {
    run_gate graphics_ingestion ruby tools/tests/graphics_ingestion_gate.rb
    run_gate graphics_placement_damage ruby tools/tests/graphics_placement_damage_gate.rb
    run_gate pixel_frame_gate.rb ruby tools/tests/pixel_frame_gate.rb
    run_gate hidden_cursor_damage_gate.rb ruby tools/tests/hidden_cursor_damage_gate.rb
    run_gate evdev_scan_gate.rb ruby tools/tests/evdev_scan_gate.rb
    run_serial_gate evdev_open_latency_gate.rb ruby tools/tests/evdev_open_latency_gate.rb
    run_gate input_identity_gate.sh "$ROOT"/tools/tests/input_identity_gate.sh
    run_gate pointer_curve_gate.sh "$ROOT"/tools/tests/pointer_curve_gate.sh
    run_gate audio_control_gate.sh "$ROOT"/tools/tests/audio_control_gate.sh
    run_gate evdev_char_gate.sh "$ROOT"/tools/tests/evdev_char_gate.sh
    run_gate native_vt_kernel_gate.sh sh tools/tests/native_vt_kernel_gate.sh
    run_gate vt_watchdog_report_gate.sh sh "$ROOT"/tools/tests/vt_watchdog_report_gate.sh
    run_gate input_device_api_gate.rb ruby tools/tests/input_device_api_gate.rb
    run_gate input_device_config_gate.rb ruby tools/tests/input_device_config_gate.rb
    run_gate touchpad_gate.sh "$ROOT"/tools/tests/touchpad_gate.sh
    run_gate tap_setting_gate.rb ruby tools/tests/tap_setting_gate.rb
    run_serial_gate settings_persistence_gate.rb ruby tools/tests/settings_persistence_gate.rb
    run_gate login_policy_gate.rb ruby tools/tests/login_policy_gate.rb
    run_gate input_hotplug_cycle_gate.rb ruby tools/tests/input_hotplug_cycle_gate.rb
    run_gate input_device_route_gate.rb ruby tools/tests/input_device_route_gate.rb
    run_gate audio_device_api_gate.rb ruby tools/tests/audio_device_api_gate.rb
    run_gate audio_card_gate.rb ruby tools/tests/audio_card_gate.rb
    run_gate display_device_api_gate.rb ruby tools/tests/display_device_api_gate.rb
    run_gate pointer_target_gate.rb ruby tools/tests/pointer_target_gate.rb
    run_gate kitty_query_gate.rb ruby tools/tests/kitty_query_gate.rb
    run_gate kitty_placement_gate.rb ruby tools/tests/kitty_placement_gate.rb
    run_gate kitty_frame_edit_gate.rb ruby tools/tests/kitty_frame_edit_gate.rb
    run_gate kitty_delete_gate.rb ruby tools/tests/kitty_delete_gate.rb
    # ADR 0226: waits on frames to order pictures by when they were shown.
    run_serial_gate graphics_memory_gate.rb ruby tools/tests/graphics_memory_gate.rb
    run_serial_gate graphics_blink_gate.rb ruby tools/tests/graphics_blink_gate.rb
    run_serial_gate kms_frame_timing_gate.rb ruby tools/tests/kms_frame_timing_gate.rb
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
    run_gate pointer_lock_gate.rb ruby tools/tests/pointer_lock_gate.rb
    run_gate pointer_lock_desktop_gate.rb ruby tools/tests/pointer_lock_desktop_gate.rb
    run_gate key_encoding_gate.rb ruby tools/tests/key_encoding_gate.rb
    run_gate input_stream_text_gate.rb ruby tools/tests/input_stream_text_gate.rb
    run_gate keyboard_settings_gate.rb ruby tools/tests/keyboard_settings_gate.rb
    run_gate pointer_settings_gate.rb ruby tools/tests/pointer_settings_gate.rb
    run_gate display_settings_gate.rb ruby tools/tests/display_settings_gate.rb
    run_gate wallpaper_damage_gate.rb ruby tools/tests/wallpaper_damage_gate.rb
    run_gate graphics_band_gate.rb ruby tools/tests/graphics_band_gate.rb
    run_gate pane_effects_gate.rb ruby tools/tests/pane_effects_gate.rb
    run_serial_gate presentation_effects_gate.rb ruby tools/tests/presentation_effects_gate.rb
    run_serial_gate tube_effects_gate.rb ruby tools/tests/tube_effects_gate.rb
    run_serial_gate burn_in_gate.rb ruby tools/tests/burn_in_gate.rb
    run_serial_gate mask_gate.rb ruby tools/tests/mask_gate.rb
    run_serial_gate performance_panel_gate.rb ruby tools/tests/performance_panel_gate.rb
    run_gate overlay_translucency_gate.rb ruby tools/tests/overlay_translucency_gate.rb
    run_gate under_text_picture_gate.rb ruby tools/tests/under_text_picture_gate.rb
    run_gate display_zoom_gate.rb ruby tools/tests/display_zoom_gate.rb
    run_serial_gate zoomed_effects_gate.rb ruby tools/tests/zoomed_effects_gate.rb
    run_serial_gate settings_writer_gate.rb ruby tools/tests/settings_writer_gate.rb
    run_gate output_capture_gate.rb ruby tools/tests/output_capture_gate.rb
    run_gate braille_gate.rb ruby tools/tests/braille_gate.rb
    run_gate window_style_gate.rb ruby tools/tests/window_style_gate.rb
    run_gate icon_pen_gate.rb ruby tools/tests/icon_pen_gate.rb
    run_gate glyph_edge_sweep_gate.rb ruby tools/tests/glyph_edge_sweep_gate.rb
    run_gate control_style_gate.rb ruby tools/tests/control_style_gate.rb
    run_gate sixel_slot_gate.rb ruby tools/tests/sixel_slot_gate.rb
    run_gate kms_compose_gate.sh "$ROOT"/tools/tests/kms_compose_gate.sh
    run_gate gles_compositor_gate.rb ruby "$ROOT/tools/tests/gles_compositor_gate.rb"
    run_gate gles_compose_gate.sh sh "$ROOT/tools/tests/gles_compose_gate.sh"
    run_gate pointer_art_gate.sh sh "$ROOT/tools/tests/pointer_art_gate.sh"
    run_gate pointer_appearance_gate.rb ruby "$ROOT/tools/tests/pointer_appearance_gate.rb"
    run_gate pointer_shape_gate.rb ruby "$ROOT/tools/tests/pointer_shape_gate.rb"
    run_gate kms_graphics_clip_gate.sh sh "$ROOT/tools/tests/kms_graphics_clip_gate.sh"
    run_gate kms_hotplug_gate.rb ruby "$ROOT"/tools/tests/kms_hotplug_gate.rb
    run_gate kms_hotplug_cycles.rb ruby "$ROOT"/tools/tests/kms_hotplug_cycles.rb
    run_gate kms_pointer_phase_gate.rb ruby "$ROOT"/tools/tests/kms_pointer_phase_gate.rb
    run_gate surface_damage_gate.sh sh "$ROOT"/tools/tests/surface_damage_gate.sh
    run_gate screenshot_overlay_gate.sh "$ROOT"/tools/tests/screenshot_overlay_gate.sh
    run_gate render_state_agreement_gate.rb ruby tools/tests/render_state_agreement_gate.rb
    run_gate pane_split_geometry_gate.rb ruby tools/tests/pane_split_geometry_gate.rb
    run_gate display_layout_gate.rb ruby tools/tests/display_layout_gate.rb
}

suite_hibr() {
    run_gate auxiliary_native_gate.rb ruby tools/tests/auxiliary_native_gate.rb
    run_gate auxiliary_desktop_gate.rb env BLIT_NATIVE_AUXILIARY_DESKTOP=1 BLIT_NATIVE_AUXILIARY_RESIZE=1 ruby tools/tests/auxiliary_native_gate.rb
    run_gate native_hold_observer_gate.rb ruby tools/tests/native_hold_observer_gate.rb
    run_gate native_join_pty_gate.rb ruby tools/tests/native_join_pty_gate.rb
    run_gate hibr_singleton_gate.rb ruby tools/tests/hibr_singleton_gate.rb
    run_gate hibr_join_viewport_gate.rb ruby tools/tests/hibr_join_viewport_gate.rb
    run_gate hibr_join_frame_barrier_gate.rb ruby tools/tests/hibr_join_frame_barrier_gate.rb
    run_gate hibr_frame_barrier_scope_gate.rb ruby tools/tests/hibr_frame_barrier_scope_gate.rb
    run_gate font_resize_views_gate.rb ruby tools/tests/font_resize_views_gate.rb
    run_gate frame_descriptor_gate.sh sh "$ROOT"/tools/tests/frame_descriptor_gate.sh
    run_gate frame_dmabuf_gate.sh sh "$ROOT"/tools/tests/frame_dmabuf_gate.sh
    # ADR 0228 phase 1: blit-compositor and the WINDOW wire against a fake
    # desktop with the in-repo stand-in client, then Blit's window=0. Needs
    # wayland-server, wayland-client, wayland-scanner and wayland-protocols
    # (tools/build-flags.sh).
    run_gate compositor_gate.sh sh "$ROOT"/tools/tests/compositor_gate.sh
    # ADR 0228 phase 2, milestone A: an X program in a window through the
    # compositor's Xwayland, keys and mouse, with the in-repo X stand-in.
    # Skips (77) without Xwayland 23.1 or later on PATH, or without libxcb
    # (tools/build-flags.sh).
    run_gate xwayland_gate.sh sh "$ROOT"/tools/tests/xwayland_gate.sh
    # ADR 0228 phase 1, milestone B: GET /v1/input/keymap compiles with
    # libxkbcommon and types what Blit types. Needs xkbcommon
    # (tools/build-flags.sh).
    run_gate keymap_gate.sh sh "$ROOT"/tools/tests/keymap_gate.sh
    # ADR 0228: the INPUT routes to blit-compositor's input sockets, and a
    # routed picture never freed for room (ADR 0226).
    run_gate frame_input_gate.sh sh "$ROOT"/tools/tests/frame_input_gate.sh
    run_serial_gate graphics_memory_frames_gate.sh sh "$ROOT"/tools/tests/graphics_memory_frames_gate.sh
    # ADR 0223: import, drawing and RELEASE need a GPU that imports dmabufs,
    # so they run against a named live host, never implied by a local run.
    if [ -n "$LIVE_DMABUF" ]; then
        run_serial_gate frame_dmabuf_live_gate.sh sh "$ROOT"/tools/tests/frame_dmabuf_live_gate.sh "$LIVE_DMABUF"
    else
        skip_gate frame_dmabuf_live_gate.sh "set BLIT_GATE_LIVE_DMABUF to a host whose GPU imports dmabufs"
    fi
    run_gate hibr_split_capture_gate.rb ruby tools/tests/hibr_split_capture_gate.rb
    run_gate hibr_join_placement_gate.sh "$ROOT"/tools/tests/hibr_join_placement_gate.sh
    # Needs only the Hibr headers, not a Hibr process: it compiles the shipped
    # display provider into a harness. Skips (77) without BLIT_HIBR_ROOT.
    run_gate adapter_coalesce_gate.sh "$ROOT"/tools/tests/adapter_coalesce_gate.sh
    run_gate adapter_input_gate.sh "$ROOT"/tools/tests/adapter_input_gate.sh
    run_gate adapter_provider_gate.sh "$ROOT"/tools/tests/adapter_provider_gate.sh
    run_gate desktop_repaint_gate.rb ruby tools/tests/desktop_repaint_gate.rb
    run_serial_gate output_occupancy_gate.rb ruby tools/tests/output_occupancy_gate.rb
    run_gate hibr_resume_guard_gate.sh "$ROOT"/tools/tests/hibr_resume_guard_gate.sh
    # QEMU is the repeatable multi-display oracle and is never implied by a
    # local run; invoke tools/tests/qemu_hibr_lifecycle_gate.sh directly.
    skip_gate qemu_hibr_lifecycle_gate.sh "QEMU oracle, run explicitly"
}

suite_lifecycle() {
    run_gate native_child_provider_gate.rb ruby tools/tests/native_child_provider_gate.rb
    run_gate api_command_report_gate.rb ruby tools/tests/api_command_report_gate.rb
    run_gate service_boundary_gate.rb ruby tools/tests/service_boundary_gate.rb
    run_gate session_log_gate.rb ruby tools/tests/session_log_gate.rb
    run_gate tcc_fault_trace_gate.rb ruby tools/tests/tcc_fault_trace_gate.rb
    run_gate auxiliary_policy_gate.rb ruby tools/tests/auxiliary_policy_gate.rb
    run_gate login_desktop_observer_gate.rb ruby tools/tests/login_desktop_observer_gate.rb
    run_gate login_identity_gate.rb ruby tools/tests/login_identity_gate.rb
    run_gate login_manager_gate.sh "$ROOT"/tools/tests/login_manager_gate.sh
    run_gate login_seat_gate.sh "$ROOT"/tools/tests/login_seat_gate.sh
    run_gate restart_gate.sh "$ROOT"/tools/tests/restart_gate.sh
    run_gate shutdown_gate.sh "$ROOT"/tools/tests/shutdown_gate.sh
    run_gate shutdown_key_gate.rb ruby tools/tests/shutdown_key_gate.rb
    run_gate update_idle_timeout_gate.sh "$ROOT"/tools/tests/update_idle_timeout_gate.sh
    run_gate update_reset_gate.sh "$ROOT"/tools/tests/update_reset_gate.sh
    run_serial_gate restart_after_update_gate.rb ruby tools/tests/restart_after_update_gate.rb
    run_gate hibr_identity_refresh_gate.rb ruby tools/tests/hibr_identity_refresh_gate.rb
    run_gate update_cancel_gate.sh "$ROOT"/tools/tests/update_cancel_gate.sh
}

suite_packaging() {
    if [ -n "$PACKAGE" ]; then
        run_gate package_artifact_gate.sh "$ROOT"/tools/tests/package_artifact_gate.sh "$PACKAGE"
        # Needs a real package pair: the upload, split into one staged file
        # per package (ADR 0232), and the exit that restarts the unit.
        run_gate update_restart_gate.sh "$ROOT"/tools/tests/update_restart_gate.sh
    else
        skip_gate package_artifact_gate.sh "set BLIT_GATE_PACKAGE to a .deb"
    fi
}

suite_timing() {
    run_serial_gate performance_baseline_gate.rb ruby tools/tests/performance_baseline_gate.rb
    run_serial_gate pointer_cadence_observation_gate.rb ruby tools/tests/pointer_cadence_observation_gate.rb
    run_serial_gate frame_rate_gate.rb ruby tools/tests/frame_rate_gate.rb
    run_serial_gate terminal_soak_gate.rb sh "$ROOT"/tools/tests/gate_with_server.sh \
        $((BASE_PORT + 2)) ruby tools/tests/terminal_soak_gate.rb
    # Physical multi-display and the live screenshot gate assert against a
    # named running host, not a local fixture.
    if [ -n "$LIVE" ]; then
        run_serial_gate physical_multi_display_gate.rb env \
            BLIT_GATE_SERVER="$LIVE" ruby tools/tests/physical_multi_display_gate.rb
        run_serial_gate screenshot_nonblocking_gate.sh \
            "$ROOT"/tools/tests/screenshot_nonblocking_gate.sh "$LIVE" display-1
    else
        skip_gate physical_multi_display_gate.rb "set BLIT_GATE_LIVE to a host URL"
        skip_gate screenshot_nonblocking_gate.sh "set BLIT_GATE_LIVE to a host URL"
    fi
}

# Test hook for tools/tests/run_gates_gate.rb: a file that defines suite_fake
# lets the runner itself be tested on gates that do nothing but behave. It is
# not part of `all`.
FAKE_SUITE=${BLIT_GATE_FAKE_SUITE:-}
if [ -n "$FAKE_SUITE" ]; then
    . "$FAKE_SUITE"
fi

[ "$#" -gt 0 ] || usage
for argument in "$@"; do
    case "$argument" in
        -h|--help) usage ;;
        schema|terminal|font|renderer|renderer-gles|hibr|lifecycle|packaging|timing|all) ;;
        fake) [ -n "$FAKE_SUITE" ] || { echo "run-gates: unknown suite 'fake'" >&2; usage; } ;;
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
printf 'run-gates: jobs=%s timeout=%ss port_offset=%s\n' \
    "$JOBS" "${BLIT_GATE_TIMEOUT:-300}" "$PORT_OFFSET"
printf '\n'

SUITES="$*"
# renderer-gles is the renderer suite's compositor-sensitive gates on GLES; the
# compositor is a property of the whole invocation, so run it by itself.
case " $SUITES " in
*" renderer-gles "*) BLIT_GATE_BACKEND=gles; export BLIT_GATE_BACKEND ;;
esac
run_suite() {
    cursuite=$1
    case $1 in
    renderer-gles)
        ONLY_SENSITIVE=1
        suite_renderer
        ONLY_SENSITIVE=''
        ;;
    *) "suite_$1" ;;
    esac
}
run_suites() {
    idx=0
    for argument in $SUITES; do
        case "$argument" in
            all)
                run_suite schema; run_suite terminal; run_suite font; run_suite renderer
                run_suite hibr; run_suite lifecycle; run_suite packaging; run_suite timing
                ;;
            *) run_suite "$argument" ;;
        esac
    done
}

# Workers: JOBS of them take the parallel lane, then one takes the timing lane
# by itself. All run in a scheduler subshell so that the coordinator, below,
# can report in suite order while they work.
worker() {
    MODE=worker
    WORKER_LANE=$1
    BLIT_GATE_PORT_OFFSET=$2
    export BLIT_GATE_PORT_OFFSET
    trap '[ -z "$gpid" ] || kill -TERM "$gpid" 2>/dev/null; exit 143' HUP TERM
    run_suites
}
schedule() {
    sched_workers=''
    trap 'for p in $sched_workers; do kill -TERM "$p" 2>/dev/null; done; wait; exit 143' HUP TERM
    k=0
    while [ "$k" -lt "$JOBS" ]; do
        worker parallel $((PORT_OFFSET + k * GATE_PORT_STRIDE)) &
        sched_workers="$sched_workers $!"
        k=$((k + 1))
    done
    wait $sched_workers
    sched_workers=''
    worker serial "$PORT_OFFSET" &
    sched_workers=$!
    wait $sched_workers
}

run_start=$(now_ms)
if [ "$JOBS" -gt 1 ]; then
    schedule &
    sched_pid=$!
    MODE=print
fi
run_suites
if [ -n "$sched_pid" ]; then
    wait "$sched_pid" 2>/dev/null || true
    sched_pid=''
fi
run_ms=$(( $(now_ms) - run_start ))

# Where the time went. Printed before the verdict, which stays the last line.
printf '\nrun-gates: slowest gates\n'
sort -rn "$tmpdir/timing" | head -20 | while read -r t_ms t_suite t_name; do
    printf '  %9s  %-38s %s\n' "$(fmt_ms "$t_ms")" "$t_name" "$t_suite"
done
printf 'run-gates: time per suite (sum of the gates in it)\n'
awk '{ if (!($2 in n)) order[++k] = $2; n[$2]++; t[$2] += $1 }
     END { for (i = 1; i <= k; i++) printf "  %-10s %3d gates %9.1fs\n", order[i], n[order[i]], t[order[i]] / 1000 }' \
    "$tmpdir/timing"
printf 'run-gates: wall %s, gate time %s, jobs=%s\n' "$(fmt_ms "$run_ms")" \
    "$(fmt_ms "$(awk '{ s += $1 } END { printf "%d", s }' "$tmpdir/timing")")" "$JOBS"

printf '\n'
if [ "$fail" -eq 0 ]; then
    printf 'RUN_GATES_PASS pass=%d skip=%d fail=0\n' "$pass" "$skip"
    exit 0
fi
printf 'RUN_GATES_FAIL pass=%d skip=%d fail=%d failed:%s\n' \
    "$pass" "$skip" "$fail" "$failed_gates"
exit 1
