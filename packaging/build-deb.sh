#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUNDLE=$ROOT/build/blit-desktop-current
OUT=${1:-$ROOT}

if [ ! -x "$BUNDLE/bin/blit-server" ]; then
	echo "blit: build/blit-desktop-current is incomplete" >&2
	exit 1
fi

version=$(sed -n 's/^blit desktop bundle \([^ -]*\).*/\1/p' "$BUNDLE/VERSION")
if [ -z "$version" ]; then
	echo "blit: cannot determine version from $BUNDLE/VERSION" >&2
	exit 1
fi

pkgver=${version}-1
stage=$(mktemp -d "${TMPDIR:-/tmp}/blit-deb.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM

mkdir -p "$stage/usr/lib/blit" "$stage/usr/bin" \
	"$stage/usr/share/doc/blit"
cp -a "$BUNDLE/." "$stage/usr/lib/blit/"
# Shell launchers are test/deployment conveniences, not part of the server
# runtime.  Keep the installed boundary to the TCC-built executable and the
# data/configuration it consumes; Hibr is an external executable.
rm -f "$stage/usr/lib/blit/bin/attach-join" \
	"$stage/usr/lib/blit/bin/attach-primary" \
	"$stage/usr/lib/blit/bin/blit-diagnose" \
	"$stage/usr/lib/blit/bin/hibr-display" \
	"$stage/usr/lib/blit/bin/hibr-join" \
	"$stage/usr/lib/blit/bin/run-desktop" \
	"$stage/usr/lib/blit/bin/run-shell" \
	"$stage/usr/lib/blit/bin/start-blit"
# The repository documentation and OpenAPI schema are authoritative.  The
# bundle is an installable runtime artifact, not a second documentation tree.
if [ -d "$ROOT/docs" ]; then
	rm -rf "$stage/usr/lib/blit/docs"
	cp -a "$ROOT/docs" "$stage/usr/lib/blit/docs"
fi
if [ -d "$ROOT/openapi" ]; then
	rm -rf "$stage/usr/lib/blit/openapi"
	cp -a "$ROOT/openapi" "$stage/usr/lib/blit/openapi"
fi
cp -a "$BUNDLE/lib/libSDL3.so.0.4.16" "$stage/usr/lib/"
ln -s /usr/lib/libSDL3.so.0.4.16 "$stage/usr/lib/libSDL3.so.0"
ln -s /usr/lib/libSDL3.so.0 "$stage/usr/lib/libSDL3.so"
ln -s /usr/lib/blit/bin/blit-server "$stage/usr/bin/blit"
cp -a "$BUNDLE/README.md" "$BUNDLE/VERSION" "$stage/usr/share/doc/blit/"
if [ -d "$ROOT/docs" ]; then
	cp -a "$ROOT/docs" "$stage/usr/share/doc/blit/"
else
	cp -a "$BUNDLE/docs" "$stage/usr/share/doc/blit/"
fi

mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<EOF
Package: blit
Version: $pkgver
Section: misc
Priority: optional
Architecture: amd64
Maintainer: ITD Labs <itdlabs@localhost>
Depends: libc6, libdrm2
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
echo "$output"
