#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUNDLE=$ROOT/build/blit-desktop-current
RUNTIME_BINARY=$ROOT/build/blit/blit-server
UPDATE_HELPER=$ROOT/build/blit/blit-update-helper
HIBR_ENTRYPOINT=$ROOT/hibr/blit-session.hibr
HIBR_LOGIN_WRAPPER=$ROOT/hibr/blit-login.hibr
HIBR_LOGIN_ENTRYPOINT=$ROOT/hibr/blit-login-session.hibr
HIBR_RUNTIME_RC=$ROOT/hibr/blit-runtime-console.hibr
HIBR_VERSION_FILE=$ROOT/packaging/HIBR_VERSION
HIBR_ABI_FILE=$ROOT/packaging/HIBR_ABI
OUT=${1:-$ROOT}
VERSION_FILE=$ROOT/packaging/VERSION
HIBR_ROOT=${BLIT_HIBR_ROOT:-}
# The frame channel speaks version 5 only, one grammar with no compatibility
# forms (docs/api.md, "One grammar, version 5"): a Hibr from before its own
# version 5 cannot reach this Blit's channel, so the two never install
# together. Hibr's packages carry the matching Breaks on Blit.
HIBR_FRAME_V5=0.99.482

# The adapter is part of the package's ABI boundary. Reusing a .so from the
# ignored build directory made its Hibr provenance unknowable: a package
# could carry an adapter compiled against a different checkout while the
# source and HIBR_VERSION file looked correct. Require the exact checkout and
# compile the adapter into this package's staging tree below.
if [ -z "$HIBR_ROOT" ]; then
	echo "blit: BLIT_HIBR_ROOT is required for a package build" >&2
	exit 1
fi
if [ ! -d "$HIBR_ROOT/include" ] || [ ! -d "$HIBR_ROOT/mods" ]; then
	echo "blit: Hibr headers/modules not found under $HIBR_ROOT" >&2
	exit 1
fi

if [ ! -x "$RUNTIME_BINARY" ] || [ ! -x "$UPDATE_HELPER" ] ||
   [ ! -x "$BUNDLE/bin/blit-server" ] ||
   [ ! -f "$VERSION_FILE" ] || [ ! -f "$HIBR_ENTRYPOINT" ] ||
   [ ! -f "$HIBR_LOGIN_WRAPPER" ] ||
   [ ! -f "$HIBR_LOGIN_ENTRYPOINT" ] ||
   [ ! -f "$HIBR_VERSION_FILE" ] || [ ! -f "$HIBR_ABI_FILE" ]; then
	echo "blit: canonical build outputs are incomplete" >&2
	exit 1
fi
if ! strings "$RUNTIME_BINARY" | grep -q '/v1/version'; then
	echo "blit: canonical runtime lacks the version endpoint" >&2
	exit 1
fi
# The staging copy beside the bundle's libraries is refreshed from the binary
# this build actually ships.  It used to be checked for existence and never
# updated, so it could be an arbitrarily old build -- and
# tools/tests/network_auth_gate.sh defaulted to it, which meant the gate
# covering authentication, the command allowlist and BLIT_API_TOKEN injection
# was testing a binary nobody was releasing (issue #59).  The gate now takes
# the canonical binary; this keeps the staging directory from being a second,
# stale answer to "which server is this".
cp -a "$RUNTIME_BINARY" "$BUNDLE/bin/blit-server"

# The updater is a separate root-owned service.  Reject the historical failure
# mode where the output-path argument caused the server to be copied twice.
if cmp -s "$RUNTIME_BINARY" "$UPDATE_HELPER" ||
   strings "$UPDATE_HELPER" | grep -q '/v1/version' ||
   strings "$UPDATE_HELPER" | grep -q 'blit gateway listening'; then
	echo "blit: updater output is the server binary" >&2
	exit 1
fi

# The ignored build directory is also used by historical tarball workflows,
# so do not silently accept a stale launcher alongside the native runtime.
# Hibr owns startup/session orchestration; this package owns only blit-server.
for runtime_file in "$BUNDLE"/bin/* "$BUNDLE"/lib/*; do
	[ -e "$runtime_file" ] || continue
	case "$runtime_file" in
		"$BUNDLE/bin/blit-server") ;;
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
if ! strings "$RUNTIME_BINARY" | grep -F -q "$version"; then
	echo "blit: runtime binary version does not match $VERSION_FILE ($version)" >&2
	exit 1
fi

pkgver=${version}-1
stage=$(mktemp -d "${TMPDIR:-/tmp}/blit-deb.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM

mkdir -p "$stage/usr/lib/blit" "$stage/usr/bin" \
	"$stage/usr/share/doc/blit"
# Install only the runtime boundary.  Shell launchers are test/deployment
# conveniences, not part of the server runtime; Hibr remains an external
# executable and session/orchestration layer.
mkdir -p "$stage/usr/lib/blit/bin" \
    "$stage/usr/lib/blit/hibr" \
    "$stage/usr/share/blit/probes" \
    "$stage/usr/lib/blit/fonts" "$stage/usr/share/blit/hibr" \
    "$stage/lib/systemd/system"
cp -a "$RUNTIME_BINARY" "$stage/usr/lib/blit/bin/blit-server"
cp -a "$UPDATE_HELPER" "$stage/usr/lib/blit/bin/blit-update-helper"
# The adapter is Blit-owned code compiled against the selected external Hibr
# ABI. Keep it outside Hibr's module search directory: the stock desktop's
# `need display` must continue to select Hibr's native console provider. The
# explicit Blit entrypoint loads this optional provider by absolute path.
BLIT_HIBR_ROOT="$HIBR_ROOT" "$ROOT/tools/build-tcc.sh" \
	"$stage/usr/lib/blit/hibr/console-blit.so" adapter >/dev/null
cp -a "$HIBR_ENTRYPOINT" "$stage/usr/share/blit/hibr/blit-session.hibr"
cp -a "$HIBR_LOGIN_WRAPPER" "$stage/usr/share/blit/hibr/blit-login.hibr"
cp -a "$HIBR_LOGIN_ENTRYPOINT" \
    "$stage/usr/share/blit/hibr/blit-login-session.hibr"
cp -a "$HIBR_RUNTIME_RC" "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
cp -a "$ROOT/hibr/blit-desktop-observe.hibr" "$stage/usr/share/blit/hibr/blit-desktop-observe.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-desktop-observe.hibr"
cp -a "$ROOT/hibr/blit-desktop-place.hibr" "$stage/usr/share/blit/hibr/blit-desktop-place.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-desktop-place.hibr"
cp -a "$ROOT/hibr/blit-live-image-probe.hibr" \
    "$stage/usr/share/blit/probes/blit-live-image-probe.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-session.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-login.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-login-session.hibr"
chmod 0644 "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
chmod 0755 "$stage/usr/share/blit/probes/blit-live-image-probe.hibr"
# The packaged launcher sources Hibr's installed session directly. Hibr's
# session resolves its own sibling tree from that source path, so a second
# integration tree of symlinks would be both unnecessary and a second path
# model that can silently omit a desktop component. QEMU fixtures may still
# set BLIT_HIBR_DESKTOP_SESSION to their complete staged Hibr tree.
chmod 0755 "$stage/usr/lib/blit/bin/blit-update-helper"
cp -a "$ROOT/packaging/blit-update-helper.service" \
    "$stage/lib/systemd/system/"
cp -a "$ROOT/packaging/blit.service" "$stage/lib/systemd/system/"
# ADR 0065: the unprivileged profile, shipped as a drop-in an operator
# installs rather than as the default. It is not enabled here because whether
# the blit user can reach this host's DRM device cannot be known from inside
# the package, and a gateway that comes up unable to present has no way back
# in on an API-only deployment.
install -d -m 0755 "$stage/usr/share/doc/blit/examples"
cp -a "$ROOT/packaging/blit-unprivileged.conf" \
    "$stage/usr/share/doc/blit/examples/"
# The fonts are Blit's own, from assets/fonts alone (ADR 0194). The build
# bundle's fonts directory held a stale console font and a duplicate.
if [ -d "$ROOT/assets/fonts" ]; then
	cp -a "$ROOT/assets/fonts/." "$stage/usr/lib/blit/fonts/"
fi
# ADR 0217: the 6 MB FreeMono source archive is not part of the runtime
# package. It ships as the small blit-font-sources package, and the binary
# carries a note saying where the complete corresponding source is.
font_source_rel=sources/freefont-src-20120503.tar.gz
font_source=$ROOT/assets/fonts/$font_source_rel
font_source_url=https://ftp.gnu.org/gnu/freefont/freefont-src-20120503.tar.gz
font_source_version=20120503-1
font_source_sha256=$(sed -n 's/.*"source_sha256": "\([0-9a-f]*\)".*/\1/p' \
	"$ROOT/assets/fonts/arabic-fonts.source.json")
if [ ! -f "$font_source" ] ||
   [ "$(sha256sum "$font_source" | awk '{print $1}')" != "$font_source_sha256" ]; then
	echo "blit: FreeMono source archive missing or differs from arabic-fonts.source.json" >&2
	exit 1
fi
rm -rf "$stage/usr/lib/blit/fonts/sources"
cat > "$stage/usr/lib/blit/fonts/FreeMono-SOURCE.txt" <<EOF
BlitArabicModern.ttf is derived from GNU FreeMono 20120503, which is
GPLv3-or-later with the GNU font exception (see FreeMono-LICENSE.txt and
FreeMono-README.txt).

The complete corresponding source (the upstream preferred-form archive,
including sfd/FreeMono.sfd and the FontForge build scripts) is:

  package:  blit-font-sources $font_source_version
            /usr/share/doc/blit-font-sources/freefont-src-20120503.tar.gz
  upstream: $font_source_url
  sha256:   $font_source_sha256

Blit's own derivation script is tools/fonts/build-arabic-profile.py in the
Blit source tree; assets/fonts/arabic-fonts.source.json records the pinned
hashes of the source and of the generated font.
EOF
chmod 0644 "$stage/usr/lib/blit/fonts/FreeMono-SOURCE.txt"
# The repository documentation and OpenAPI schema are authoritative.  The
# bundle is an installable runtime artifact, not a second documentation tree.
if [ -d "$ROOT/openapi" ]; then
	cp -a "$ROOT/openapi" "$stage/usr/lib/blit/openapi"
fi
ln -s /usr/lib/blit/bin/blit-server "$stage/usr/bin/blit"
# The manual pages carry the package's own version in their title line.
for page in "$ROOT"/man/*.[1-9]; do
    section=${page##*.}
    mkdir -p "$stage/usr/share/man/man$section"
    sed "s/@VERSION@/$version/" "$page" | gzip -9n \
        > "$stage/usr/share/man/man$section/${page##*/}.gz"
done
cp -a "$ROOT/README.md" "$stage/usr/share/doc/blit/"
cat > "$stage/usr/share/doc/blit/copyright" <<EOF
Blit
Copyright (C) 2026 Omar Sakka

Blit is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. On Debian systems the licence text is in
/usr/share/common-licenses/GPL-3.

Bundled third-party material keeps its own licence, installed beside it:
the fonts under /usr/lib/blit/fonts/ (licenses/, the *-LICENSE.txt files,
README.md) and the icon artwork (MIT, /usr/lib/blit/fonts/icons/LICENSE.txt).
The Hibr adapter, /usr/lib/blit/hibr/console-blit.so, is built from Hibr's
source and is distributed under the GNU Lesser General Public License,
version 3 or later (/usr/share/common-licenses/LGPL-3).
EOF
chmod 0644 "$stage/usr/share/doc/blit/copyright"
cp -a "$VERSION_FILE" "$stage/usr/share/doc/blit/VERSION"
# What a running Blit reads to notice it was updated under it (ADR 0205).
printf '%s\n' "$version" > "$stage/usr/lib/blit/VERSION"
chmod 0644 "$stage/usr/lib/blit/VERSION"
cp -a "$HIBR_VERSION_FILE" "$stage/usr/share/doc/blit/HIBR_VERSION"
cp -a "$HIBR_ABI_FILE" "$stage/usr/share/doc/blit/HIBR_ABI"
if [ -n "${BLIT_PACKAGE_DOCUMENTATION_COMMIT:-}" ]; then
    documentation_commit=$(git -C "$ROOT" rev-parse --verify \
        "$BLIT_PACKAGE_DOCUMENTATION_COMMIT^{commit}")
    git -C "$ROOT" archive "$documentation_commit" README.md | \
        tar -x -C "$stage/usr/share/doc/blit"
    git -C "$ROOT" archive "$documentation_commit" docs | \
        tar -x -C "$stage/usr/share/doc/blit"
elif [ -d "$ROOT/docs" ]; then
	cp -a "$ROOT/docs" "$stage/usr/share/doc/blit/"
else
	cp -a "$BUNDLE/docs" "$stage/usr/share/doc/blit/"
fi
# ADR 0217: one copy. It used to be copied a second time to
# /usr/lib/blit/docs, which nothing read.

mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -eu
if command -v addgroup >/dev/null 2>&1; then
    addgroup --system blit >/dev/null 2>&1 || true
fi
if command -v adduser >/dev/null 2>&1 &&
   ! id blit >/dev/null 2>&1; then
    adduser --system --ingroup blit --no-create-home --disabled-login \
        --disabled-password blit >/dev/null 2>&1 || true
fi
runtime_user="${SUDO_USER:-}"
if [ -z "$runtime_user" ] && command -v logname >/dev/null 2>&1; then
    runtime_user=$(logname 2>/dev/null || true)
fi
if [ -n "$runtime_user" ] && id "$runtime_user" >/dev/null 2>&1 &&
   command -v usermod >/dev/null 2>&1; then
    usermod -a -G blit "$runtime_user" >/dev/null 2>&1 || true
fi

# A package install is a complete, runnable deployment. Preserve an operator's
# existing configuration and token on upgrades; generate them only once.
if [ ! -f /etc/blit/blit.env ]; then
    install -d -o root -g blit -m 0750 /etc/blit
    umask 077
    cat > /etc/blit/blit.env <<'CONFIG'
BLIT_BIND_ADDRESS=0.0.0.0
# Native KMS is the only presentation backend (ADR 0184); any other value is
# diagnosed as invalid.
BLIT_DISPLAY_BACKEND=kms
BLIT_UPDATE_TOKEN_FILE=/etc/blit/update.token
# Issue #55. This package exists to present a desktop, so an output with
# nothing on it is a fault a monitor should see rather than a warning it has
# to interpret. Blit reports the warning either way; this turns it into an
# error after a grace window long enough for a desktop to start. Remove the
# line on a host that deliberately runs Blit with no session.
BLIT_EXPECT_DESKTOP=1
# Issue #83. Keep one root-owned Hibr login manager on the primary pane. Hibr
# authenticates and drops the selected desktop or shell; Blit retains KMS and
# evdev ownership and recreates the login screen after logout.
BLIT_LOGIN_AUTOSTART=1
LANG=C.UTF-8
LC_ALL=C.UTF-8
CONFIG
    chown root:blit /etc/blit/blit.env
    chmod 0640 /etc/blit/blit.env
fi
# ADR 0065. The session API execve's a client-supplied command, so a network
# listener without authentication is remote code execution. This file asks for
# 0.0.0.0, so the install has to supply a token with it: a generated one, the
# same way the update token is generated, rather than a default value or an
# unauthenticated deployment.
#
# On an upgrade of a deployment that has none, the token is appended rather
# than left absent -- the server refuses a network bind without one, and a
# host that will not start is worse than a client that has to be told the new
# token. It is in /etc/blit/blit.env, readable by root and the blit group.
if ! grep -q '^BLIT_AUTH_TOKEN=' /etc/blit/blit.env; then
    umask 077
    printf 'BLIT_AUTH_TOKEN=%s\n' \
        "$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')" \
        >> /etc/blit/blit.env
    chown root:blit /etc/blit/blit.env
    chmod 0640 /etc/blit/blit.env
fi
if ! grep -q '^BLIT_EXPECT_DESKTOP=' /etc/blit/blit.env; then
    printf '%s\n' 'BLIT_EXPECT_DESKTOP=1' >> /etc/blit/blit.env
fi
if ! grep -q '^BLIT_LOGIN_AUTOSTART=' /etc/blit/blit.env; then
    printf '%s\n' 'BLIT_LOGIN_AUTOSTART=1' >> /etc/blit/blit.env
fi
if ! grep -q '^BLIT_DISPLAY_BACKEND=' /etc/blit/blit.env; then
    printf '%s\n' 'BLIT_DISPLAY_BACKEND=kms' >> /etc/blit/blit.env
fi
if ! grep -q '^LANG=' /etc/blit/blit.env; then
    printf '%s\n' 'LANG=C.UTF-8' >> /etc/blit/blit.env
fi
if ! grep -q '^LC_ALL=' /etc/blit/blit.env; then
    printf '%s\n' 'LC_ALL=C.UTF-8' >> /etc/blit/blit.env
fi
if [ ! -s /etc/blit/update.token ]; then
    install -d -o root -g blit -m 0750 /etc/blit
    umask 077
    od -An -N32 -tx1 /dev/urandom | tr -d ' \n' > /etc/blit/update.token
    chown root:blit /etc/blit/update.token
    chmod 0640 /etc/blit/update.token
fi
install -d -o root -g blit -m 0770 /var/lib/blit/updates
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    # Unit enablement is safe during an API-driven upgrade.  Starting either
    # unit here is not: blit is deliberately restarted by the API only after
    # dpkg has returned its result, and the helper must not restart itself from
    # inside its own dpkg child.
    systemctl enable blit-update-helper.service || true
    systemctl enable blit.service || true
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
Depends: libc6, libdrm2, zlib1g, libfreetype6 (>= 2.12.1), libharfbuzz0b (>= 6.0.0)
Suggests: hibr, blit-font-sources, blit-gamescope, blit-compositor
Breaks: hibr (<< $HIBR_FRAME_V5)
Description: API-driven native KMS text display server
 Blit provides a text-first display server with one terminal surface per
 physical display or pane, HTTP control, streaming, and Hibr integration.
 Hibr is kept as an explicit external integration because its package and
 desktop session are maintained separately.
EOF

mkdir -p "$OUT"
output=$OUT/blit_${pkgver}_amd64.deb
# Build the archive deterministically.  Without this, dpkg-deb stamps the ar
# container with the build time and the tar entries with each staged file's
# mtime, so two builds of an identical tree produce different bytes -- which
# makes the package_sha256 a release manifest records unverifiable the moment
# the artifact is gone.  The release ritual depends on being able to rebuild
# and check, and a package's timestamps carry no information worth keeping:
# SOURCE_DATE_EPOCH clamps every mtime to one value and fixes the container's.
# It is overridable, but the default is a constant rather than anything
# derived from git, because the release build happens before the commit that
# records it and a commit-derived epoch would change under its own manifest.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1577836800}
export SOURCE_DATE_EPOCH
dpkg-deb --build --root-owner-group "$stage" "$output" >/dev/null
# Reject shell implementations and retired runtime launchers. The shared
# policy distinguishes runtime entrypoints from non-executable font artwork;
# a file named fonts/icons/hibr-pen.svg is not a Hibr launcher. Consume the
# whole listing rather than grep -q closing dpkg-deb's pipe early.
if dpkg-deb --contents "$output" | grep -E -f "$ROOT/tools/release/runtime-entrypoints.pattern" >/dev/null; then
	rm -f "$output"
	echo "blit: runtime package contains a shell implementation or Hibr launcher" >&2
	exit 1
fi
echo "$output"

# ADR 0217: the font-source package, built beside the main one. Its version is
# the upstream FreeFont version, not Blit's, so it only needs publishing when
# its content changes.
sstage=$(mktemp -d "${TMPDIR:-/tmp}/blit-fontsrc-deb.XXXXXX")
trap 'rm -rf "$stage" "$sstage"' EXIT HUP INT TERM
sdoc=$sstage/usr/share/doc/blit-font-sources
mkdir -p "$sdoc" "$sstage/DEBIAN"
cp -a "$font_source" "$sdoc/freefont-src-20120503.tar.gz"
chmod 0644 "$sdoc/freefont-src-20120503.tar.gz"
cat > "$sdoc/README" <<EOF
blit-font-sources: complete corresponding source of Blit's Arabic font

This package carries freefont-src-20120503.tar.gz, the upstream preferred-form
source and build archive of GNU FreeMono 20120503, from which Blit Arabic
Modern (BlitArabicModern.ttf in the blit package) is derived.

  upstream: $font_source_url
  sha256:   $font_source_sha256

The archive contains sfd/FreeMono.sfd and the FontForge generation scripts.
Blit's derivation step is tools/fonts/build-arabic-profile.py in the Blit
source tree. The blit package suggests this package and says in
/usr/lib/blit/fonts/FreeMono-SOURCE.txt where to find the source.
EOF
cat > "$sdoc/copyright" <<EOF
GNU FreeFont (FreeMono), 20120503, is distributed under the GNU General Public
License, version 3 or later, with the GNU font embedding exception. The full
licence text and the authors' list are inside the archive and are also
installed by the blit package under /usr/lib/blit/fonts/ (FreeMono-LICENSE.txt,
FreeMono-README.txt, FreeMono-AUTHORS.txt). On Debian systems the GPL-3 text is
in /usr/share/common-licenses/GPL-3.
EOF
chmod 0644 "$sdoc/README" "$sdoc/copyright"
cat > "$sstage/DEBIAN/control" <<EOF
Package: blit-font-sources
Version: $font_source_version
Section: misc
Priority: optional
Architecture: all
Maintainer: ITD Labs <itdlabs@localhost>
Description: Source archive of GNU FreeMono, the origin of Blit Arabic Modern
 The upstream FreeFont 20120503 source archive (GPLv3-or-later with the font
 exception), kept so the complete corresponding source of the Arabic font in
 the blit package stays available without being installed with every Blit.
EOF
soutput=$OUT/blit-font-sources_${font_source_version}_all.deb
dpkg-deb --build --root-owner-group "$sstage" "$soutput" >/dev/null
echo "$soutput"

# ADR 0225: the gamescope bridge, its own package at Blit's version, suggested
# by blit. It is the only part of Blit that links PipeWire, so blit itself
# keeps no PipeWire dependency. Built into the staging tree like the adapter,
# against the headers tools/build-tcc.sh names (docs/toolchain.md).
gstage=$(mktemp -d "${TMPDIR:-/tmp}/blit-gamescope-deb.XXXXXX")
trap 'rm -rf "$stage" "$sstage" "$gstage"' EXIT HUP INT TERM
gdoc=$gstage/usr/share/doc/blit-gamescope
mkdir -p "$gstage/usr/bin" "$gdoc" "$gstage/DEBIAN"
"$ROOT/tools/build-tcc.sh" "$gstage/usr/bin/blit-gamescope" gamescope >/dev/null
chmod 0755 "$gstage/usr/bin/blit-gamescope"
cat > "$gdoc/README" <<EOF
blit-gamescope: games and Steam in a window, through a headless gamescope

  blit-gamescope [--no-place] [--steam] [-W w -H h] [-r hz] [--log file] -- command...

It starts gamescope --backend headless with the command, takes gamescope's
PipeWire video stream and hands each frame to Blit on the session's frame
channel, and names gamescope's EIS socket to Blit, which sends the pane's
keyboard and mouse there itself. Run it in a Blit session (phase 1, a plain
pane), or with --no-place inside a Hibr desktop app (phase 2), where the
desktop places the picture and sends INPUT on/off from its focus callback.

The design is ADR 0225, "Steam and games in a window, through a headless
gamescope" (docs/adr/0225-games-in-a-window-through-gamescope.md, installed
by the blit package under /usr/share/doc/blit/docs/adr/). On Debian 13
gamescope comes from trixie-backports.
EOF
cat > "$gdoc/copyright" <<EOF
blit-gamescope is part of Blit, Copyright (C) 2026 Omar Sakka, built from
src/blit_gamescope.c, src/blit_gamescope_frames.h and src/blit_gamescope_tty.h
in the Blit source tree. It is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your
option) any later version. On Debian systems the licence text is in
/usr/share/common-licenses/GPL-3.

It is compiled against the unmodified public headers of PipeWire 1.4.2
(Debian 13's libpipewire-0.3-dev and libspa-0.2-dev, Expat licence) and
links libpipewire-0.3.so.0 at run time; on Debian systems PipeWire's own
copyright file is /usr/share/doc/libpipewire-0.3-0t64/copyright.
EOF
chmod 0644 "$gdoc/README" "$gdoc/copyright"
cat > "$gstage/DEBIAN/control" <<EOF
Package: blit-gamescope
Version: $pkgver
Section: games
Priority: optional
Architecture: amd64
Maintainer: ITD Labs <itdlabs@localhost>
Depends: blit (= $pkgver), libc6, libpipewire-0.3-0, libei1, gamescope
Description: Games and Steam in a Blit window through a headless gamescope
 blit-gamescope runs a game or Steam under gamescope's headless backend and
 shows it in a Blit pane or a Hibr desktop window: frames by dmabuf over
 Blit's frame channel, keyboard and mouse sent by Blit to gamescope's EIS
 socket (ADR 0225). On Debian 13 gamescope comes from trixie-backports.
EOF
goutput=$OUT/blit-gamescope_${pkgver}_amd64.deb
dpkg-deb --build --root-owner-group "$gstage" "$goutput" >/dev/null
if dpkg-deb --contents "$goutput" | grep -E -f "$ROOT/tools/release/runtime-entrypoints.pattern" >/dev/null; then
	rm -f "$goutput"
	echo "blit: blit-gamescope package contains a shell implementation or Hibr launcher" >&2
	exit 1
fi
echo "$goutput"

# ADR 0228: Blit's Wayland compositor for programs, its own package at
# Blit's version, suggested by blit. It is the only part of Blit that links
# libwayland-server and libxkbcommon, so blit itself gains neither. Built
# by tools/build-tcc.sh from the system's protocol XML (libwayland-bin,
# wayland-protocols), into a protocol directory of its own in the staging
# area. Xwayland is Recommended, as the ADR decided: phase 2 runs X11
# programs under it; phase 1 does not start it, so it is not Depended.
cstage=$(mktemp -d "${TMPDIR:-/tmp}/blit-compositor-deb.XXXXXX")
trap 'rm -rf "$stage" "$sstage" "$gstage" "$cstage"' EXIT HUP INT TERM
cdoc=$cstage/pkg/usr/share/doc/blit-compositor
mkdir -p "$cstage/pkg/usr/bin" "$cdoc" "$cstage/pkg/DEBIAN"
BLIT_PROTOCOL_DIR="$cstage/protocol" "$ROOT/tools/build-tcc.sh" \
	"$cstage/pkg/usr/bin/blit-compositor" compositor >/dev/null
chmod 0755 "$cstage/pkg/usr/bin/blit-compositor"
cat > "$cdoc/README" <<EOF
blit-compositor: Blit's Wayland compositor for programs

  blit-compositor [--display name] [--log path]

One per desktop session, started by the desktop with the BLIT_FRAME_SOCKET
and BLIT_FRAME_TOKEN of the frame channel it serves. It listens on
\$XDG_RUNTIME_DIR/<name> (--display's, refused with status 2 when in use or
invalid; else blit-<n>, the first free n), says that name to the desktop,
and the desktop puts it in its programs' environment as WAYLAND_DISPLAY.
Its programs survive a desktop restart: when the channel closes it keeps
them and reconnects to the same socket for up to 60 seconds. It composites
nothing: each Wayland window is one picture by descriptor on Blit's frame
channel (wl_shm copied, dmabuf by its own planes), placed and sized by the
desktop, and its keyboard and mouse come straight from Blit, with the
keymap and key repeat of Blit's own keyboard settings.

The design is ADR 0228, "Blit is the compositor for programs"
(docs/adr/0228-blit-is-the-compositor-for-programs.md), and the wire is
docs/api.md, "Program windows", both installed by the blit package under
/usr/share/doc/blit/docs/. Phase 2 runs X11 programs under Xwayland, which
this package recommends.
EOF
cat > "$cdoc/copyright" <<EOF
blit-compositor is part of Blit, Copyright (C) 2026 Omar Sakka, built from
src/blit_compositor.c and src/blit_compositor_wire.h in the Blit source
tree. It is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version. On Debian systems the licence text is in
/usr/share/common-licenses/GPL-3.

Its protocol code is generated at build time by wayland-scanner from the
Wayland protocol descriptions of Debian's wayland-protocols (xdg-shell,
xdg-decoration, linux-dmabuf, presentation-time, relative-pointer,
pointer-constraints; Expat/MIT licence, see
/usr/share/doc/wayland-protocols/copyright on a system that has it). It
links libwayland-server.so.0 and libxkbcommon.so.0 at run time (MIT
licences; their own copyright files are under /usr/share/doc/).
EOF
chmod 0644 "$cdoc/README" "$cdoc/copyright"
cat > "$cstage/pkg/DEBIAN/control" <<EOF
Package: blit-compositor
Version: $pkgver
Section: x11
Priority: optional
Architecture: amd64
Maintainer: ITD Labs <itdlabs@localhost>
Depends: blit (= $pkgver), libc6, libwayland-server0, libxkbcommon0
Recommends: xwayland
Description: Blit's Wayland compositor for programs in Hibr windows
 blit-compositor is the Wayland compositor for the programs of one Blit
 desktop session: each window is a picture on Blit's frame channel, placed
 and sized by the desktop, with keyboard and mouse straight from Blit and
 Blit's keymap (ADR 0228). Xwayland, recommended, is for X11 programs.
EOF
coutput=$OUT/blit-compositor_${pkgver}_amd64.deb
dpkg-deb --build --root-owner-group "$cstage/pkg" "$coutput" >/dev/null
if dpkg-deb --contents "$coutput" | grep -E -f "$ROOT/tools/release/runtime-entrypoints.pattern" >/dev/null; then
	rm -f "$coutput"
	echo "blit: blit-compositor package contains a shell implementation or Hibr launcher" >&2
	exit 1
fi
echo "$coutput"
