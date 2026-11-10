# Sourced by shell gates (tools/tests/gate_display.sh sources it and runs the
# setup): the environment of every Blit process a gate starts. One rule, the
# same in all three gate languages: tools/tests/gate_env.rb (Ruby),
# tools/tests/gate_env.py (Python) and tools/tests/gate_env.sh (sh).
#
#   1. Start from the gate's own environment minus everything a Blit session,
#      Hibr or systemd puts there: every BLIT_*, HIBR_* and LISTEN_* variable,
#      WAYLAND_DISPLAY, WAYLAND_SOCKET and NOTIFY_SOCKET. PATH, HOME, LANG,
#      TMPDIR, XDG_RUNTIME_DIR and the rest stay.
#   2. BLIT_INPUT_ROOT is an empty directory of this gate's own, so the
#      server opens no real /dev/input device.
#   3. The NAME=value words of BLIT_GATE_SERVER_ENV, the one sanctioned way for
#      an operator to give gate servers a variable.
#   4. What the gate sets itself, which wins over all of the above.
#
#   gate_env_setup                     # gate_display_setup does this
#   env $GATE_SERVER_ENV BLIT_PSF_FONT="$FONT" $GATE_DISPLAY_ENV "$BINARY" "$port" &
#   sudo -n env $GATE_SERVER_ENV ...   # the same for a server run as root
#   gate_env_cleanup                   # gate_display_cleanup does this
#
# $GATE_SERVER_ENV starts with env's -u options, so it comes first after env,
# and the gate's own NAME=value words come after it -- never as assignments in
# front of env, which step 1 would remove again. $! of `env ... &` is the
# server: env execs it.

gate_env_setup() {
	GATE_ENV_INPUT=$(mktemp -d "${TMPDIR:-/tmp}/blit-gate-no-input.XXXXXX") || return 1
	# awk's ENVIRON names every exported variable exactly, whatever its value
	# holds; the names matched are plain identifiers, so they split cleanly.
	GATE_SERVER_ENV=$(awk 'BEGIN {
		for (name in ENVIRON)
			if (name ~ /^(BLIT_|HIBR_|LISTEN_)/ || name ~ /^(WAYLAND_DISPLAY|WAYLAND_SOCKET|NOTIFY_SOCKET)$/)
				printf "-u %s ", name
	}')
	GATE_SERVER_ENV="${GATE_SERVER_ENV}BLIT_INPUT_ROOT=$GATE_ENV_INPUT ${BLIT_GATE_SERVER_ENV:-}"
}

gate_env_cleanup() {
	[ -n "${GATE_ENV_INPUT:-}" ] && rm -rf "$GATE_ENV_INPUT"
	GATE_ENV_INPUT=
}
