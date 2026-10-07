#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUNDLE=$ROOT/build/blit-desktop-current
RUNTIME_BINARY=$ROOT/build/blit/blit-server
UPDATE_HELPER=$ROOT/build/blit/blit-update-helper
OUT=${1:-$ROOT}
VERSION_FILE=$ROOT/packaging/VERSION

if [ ! -x "$RUNTIME_BINARY" ] || [ ! -x "$UPDATE_HELPER" ] ||
   [ ! -x "$BUNDLE/bin/blit-server" ] ||
   [ ! -f "$VERSION_FILE" ]; then
	echo "blit: canonical build outputs are incomplete" >&2
	exit 1
fi
if ! strings "$RUNTIME_BINARY" | grep -q '/v1/version'; then
	echo "blit: canonical runtime lacks the version endpoint" >&2
	exit 1
fi

# The ignored build directory is also used by historical tarball workflows,
# so do not silently accept a stale launcher alongside the native runtime.
# Hibr owns startup/session orchestration; this package owns only blit-server.
for runtime_file in "$BUNDLE"/bin/* "$BUNDLE"/lib/*; do
	[ -e "$runtime_file" ] || continue
	case "$runtime_file" in
		"$BUNDLE/bin/blit-server"|"$BUNDLE/lib/libSDL3.so.0.4.16") ;;
		*)
			echo "blit: non-runtime artifact in build bundle: $runtime_file" >&2
			exit 1
			;;
	esac
done

version=$(sed -n '1p' "$VERSION_FILE")
if [ -z "$version" ]; then
	echo "blit: cannot determine version from $VERSION_FILE" >&2
	exit 1
fi
case "$version" in
	[0-9]*.[0-9]*.[0-9]*) ;;
	*) echo "blit: invalid version in $VERSION_FILE" >&2; exit 1 ;;
esac

pkgver=${version}-1
stage=$(mktemp -d "${TMPDIR:-/tmp}/blit-deb.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM

mkdir -p "$stage/usr/lib/blit" "$stage/usr/bin" \
	"$stage/usr/share/doc/blit"
# Install only the runtime boundary.  Shell launchers are test/deployment
# conveniences, not part of the server runtime; Hibr remains an external
# executable and session/orchestration layer.
mkdir -p "$stage/usr/lib/blit/bin" "$stage/usr/lib/blit/lib" \
    "$stage/usr/lib/blit/fonts" "$stage/lib/systemd/system"
cp -a "$RUNTIME_BINARY" "$stage/usr/lib/blit/bin/blit-server"
cp -a "$UPDATE_HELPER" "$stage/usr/lib/blit/bin/blit-update-helper"
chmod 0755 "$stage/usr/lib/blit/bin/blit-update-helper"
cp -a "$ROOT/packaging/blit-update-helper.service" \
    "$stage/lib/systemd/system/"
cp -a "$BUNDLE/lib/libSDL3.so.0.4.16" "$stage/usr/lib/blit/lib/"
cp -a "$BUNDLE/fonts/." "$stage/usr/lib/blit/fonts/"
if [ -d "$ROOT/assets/fonts" ]; then
	cp -a "$ROOT/assets/fonts/." "$stage/usr/lib/blit/fonts/"
fi
# The repository documentation and OpenAPI schema are authoritative.  The
# bundle is an installable runtime artifact, not a second documentation tree.
if [ -d "$ROOT/docs" ]; then
	cp -a "$ROOT/docs" "$stage/usr/lib/blit/docs"
fi
if [ -d "$ROOT/openapi" ]; then
	cp -a "$ROOT/openapi" "$stage/usr/lib/blit/openapi"
fi
ln -s /usr/lib/blit/lib/libSDL3.so.0.4.16 "$stage/usr/lib/libSDL3.so.0"
ln -s /usr/lib/libSDL3.so.0 "$stage/usr/lib/libSDL3.so"
ln -s /usr/lib/blit/bin/blit-server "$stage/usr/bin/blit"
cp -a "$BUNDLE/README.md" "$stage/usr/share/doc/blit/"
cp -a "$VERSION_FILE" "$stage/usr/share/doc/blit/VERSION"
if [ -d "$ROOT/docs" ]; then
	cp -a "$ROOT/docs" "$stage/usr/share/doc/blit/"
else
	cp -a "$BUNDLE/docs" "$stage/usr/share/doc/blit/"
fi

mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -eu
if command -v addgroup >/dev/null 2>&1; then
    addgroup --system blit >/dev/null 2>&1 || true
fi
runtime_user="${SUDO_USER:-}"
if [ -z "$runtime_user" ] && command -v logname >/dev/null 2>&1; then
    runtime_user=$(logname 2>/dev/null || true)
fi
if [ -n "$runtime_user" ] && id "$runtime_user" >/dev/null 2>&1 &&
   command -v usermod >/dev/null 2>&1; then
    usermod -a -G blit "$runtime_user" >/dev/null 2>&1 || true
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    systemctl enable --now blit-update-helper.service || true
fi
if [ ! -S /run/blit/update.sock ] && [ -x /usr/lib/blit/bin/blit-update-helper ]; then
    mkdir -p /run/blit
    /usr/lib/blit/bin/blit-update-helper >/var/log/blit-update-helper.log 2>&1 &
fi
exit 0
EOF
chmod 0755 "$stage/DEBIAN/postinst"
cat > "$stage/DEBIAN/control" <<EOF
Package: blit
Version: $pkgver
Section: misc
Priority: optional
Architecture: amd64
Maintainer: ITD Labs <itdlabs@localhost>
Depends: libc6, libdrm2, libgbm1, libegl1, libgles2, libgl1-mesa-dri
Suggests: hibr
Description: API-driven SDL3/KMS text display server
 Blit provides a text-first display server with one terminal surface per
 physical display or pane, HTTP control, streaming, and Hibr integration.
 Hibr is kept as an explicit external integration because its package and
 desktop session are maintained separately.
EOF

mkdir -p "$OUT"
output=$OUT/blit_${pkgver}_amd64.deb
dpkg-deb --build --root-owner-group "$stage" "$output" >/dev/null
# The runtime boundary is deliberately an allowlist.  Reject any shell
# implementation or Hibr launcher if either ever reappears in the package.
if dpkg-deb --contents "$output" | grep -Eq '(\.sh$|/((start|run|attach|hibr)-|((start|run|attach|hibr))$))'; then
	rm -f "$output"
	echo "blit: runtime package contains a shell implementation or Hibr launcher" >&2
	exit 1
fi
echo "$output"
