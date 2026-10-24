# Sourced by shell gates: the display a gate's server presents to, as
# tools/tests/gate_server.rb chooses it for Ruby gates (ADR 0181).
#
#   . "$ROOT/tools/tests/gate_display.sh"
#   gate_display_setup                 # once, before starting the server
#   env $GATE_DISPLAY_ENV blit-server ...
#   gate_display_cleanup               # from the gate's own exit trap
#
# BLIT_GATE_BACKEND=native (default) runs the direct-KMS compositor over the
# DRM mock with one 1024x768 display-1; gles adds the GLES compositor on the
# surfaceless platform; sdl uses SDL's dummy driver.

gate_display_root=$(CDPATH= cd -- "$(dirname -- "${GATE_DISPLAY_SOURCE:-$0}")/../.." 2>/dev/null && pwd)
[ -f "$gate_display_root/tools/tests/fixtures/drm_hotplug_mock.c" ] || gate_display_root=$(pwd)

gate_display_setup() {
	GATE_DISPLAY_DIR=
	case "${BLIT_GATE_BACKEND:-native}" in
	sdl)
		GATE_DISPLAY_ENV="SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=$gate_display_root/build/sdl3-kms"
		return 0 ;;
	native|gles) ;;
	*) echo "BLIT_GATE_BACKEND must be native, gles or sdl" >&2; return 1 ;;
	esac
	source=$gate_display_root/tools/tests/fixtures/drm_hotplug_mock.c
	digest=$(sha256sum "$source" | cut -c1-16)
	mock=${TMPDIR:-/tmp}/blit-gate-drm-mock-$(id -u)-$digest.so
	if [ ! -f "$mock" ]; then
		tcc -shared -fPIC -I"$gate_display_root/build/deps/prefix/include" \
			-I"$gate_display_root/build/deps/prefix/include/libdrm" \
			"$source" -o "$mock.$$" && mv "$mock.$$" "$mock" || return 1
	fi
	GATE_DISPLAY_DIR=$(mktemp -d "${TMPDIR:-/tmp}/blit-gate-display.XXXXXX") || return 1
	: > "$GATE_DISPLAY_DIR/drm"
	printf '1 0 0 0\n1 1024 768\n' > "$GATE_DISPLAY_DIR/state"
	GATE_DISPLAY_ENV="BLIT_DISPLAY_BACKEND=kms BLIT_DRM_DEVICE=$GATE_DISPLAY_DIR/drm BLIT_DRM_MOCK_STATE=$GATE_DISPLAY_DIR/state LD_PRELOAD=$mock LD_LIBRARY_PATH=$gate_display_root/build/sdl3-kms"
	if [ "${BLIT_GATE_BACKEND:-native}" = gles ]; then
		GATE_DISPLAY_ENV="$GATE_DISPLAY_ENV BLIT_KMS_COMPOSITOR=gles BLIT_GLES_PLATFORM=surfaceless"
	fi
}

gate_display_cleanup() {
	[ -n "${GATE_DISPLAY_DIR:-}" ] && rm -rf "$GATE_DISPLAY_DIR"
	GATE_DISPLAY_DIR=
}
