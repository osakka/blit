#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUNDLE=$ROOT/build/blit-desktop-current
OUT=${1:-$ROOT}
VERSION_FILE=$ROOT/packaging/VERSION

if [ ! -x "$BUNDLE/bin/blit-server" ] || [ ! -f "$VERSION_FILE" ]; then
	echo "blit: build/blit-desktop-current is incomplete" >&2
	exit 1
fi

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
	"$stage/usr/lib/blit/fonts"
cp -a "$BUNDLE/bin/blit-server" "$stage/usr/lib/blit/bin/"
cp -a "$BUNDLE/lib/libSDL3.so.0.4.16" "$stage/usr/lib/blit/lib/"
cp -a "$BUNDLE/fonts/." "$stage/usr/lib/blit/fonts/"
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
