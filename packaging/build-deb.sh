#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUNDLE=$ROOT/build/blit-desktop-current
RUNTIME_BINARY=$ROOT/build/blit/blit-server
UPDATE_HELPER=$ROOT/build/blit/blit-update-helper
HIBR_ADAPTER=$ROOT/build/blit/console-blit.so
HIBR_ENTRYPOINT=$ROOT/hibr/blit-session.hibr
HIBR_RUNTIME_RC=$ROOT/hibr/blit-runtime-console.hibr
HIBR_VERSION_FILE=$ROOT/packaging/HIBR_VERSION
OUT=${1:-$ROOT}
VERSION_FILE=$ROOT/packaging/VERSION

if [ ! -x "$RUNTIME_BINARY" ] || [ ! -x "$UPDATE_HELPER" ] ||
   [ ! -x "$BUNDLE/bin/blit-server" ] ||
   [ ! -f "$VERSION_FILE" ] || [ ! -f "$HIBR_ENTRYPOINT" ] ||
   [ ! -f "$HIBR_ADAPTER" ] || [ ! -f "$HIBR_VERSION_FILE" ]; then
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
mkdir -p "$stage/usr/lib/blit/bin" "$stage/usr/lib/blit/lib" \
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
cp -a "$HIBR_ADAPTER" "$stage/usr/lib/blit/hibr/console-blit.so"
cp -a "$HIBR_ENTRYPOINT" "$stage/usr/share/blit/hibr/blit-session.hibr"
cp -a "$HIBR_RUNTIME_RC" "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
cp -a "$ROOT/hibr/blit-live-image-probe.hibr" \
    "$stage/usr/share/blit/probes/blit-live-image-probe.hibr"
chmod 0755 "$stage/usr/share/blit/hibr/blit-session.hibr"
chmod 0644 "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
chmod 0755 "$stage/usr/share/blit/probes/blit-live-image-probe.hibr"
# Hibr's stock session resolves desktop.hibr and its sibling directories from
# its own $0 directory. The Blit launcher stays in a separate integration
# directory, so provide only symlinks back to Hibr's installed tree; never
# copy or fork Hibr's desktop implementation.
for desktop_entry in \
    desktop.hibr session.hibr apps colours control-panel control-strip \
    desk-accessories img lang lib login savers themes widgets wm; do
    ln -s "/usr/share/hibr/desktop/$desktop_entry" \
        "$stage/usr/share/blit/hibr/$desktop_entry"
done
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
cp -a "$BUNDLE/lib/libSDL3.so.0.4.16" "$stage/usr/lib/blit/lib/"
ln -s libSDL3.so.0.4.16 "$stage/usr/lib/blit/lib/libSDL3.so.0"
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
ln -s /usr/lib/blit/bin/blit-server "$stage/usr/bin/blit"
cp -a "$BUNDLE/README.md" "$stage/usr/share/doc/blit/"
cp -a "$VERSION_FILE" "$stage/usr/share/doc/blit/VERSION"
cp -a "$HIBR_VERSION_FILE" "$stage/usr/share/doc/blit/HIBR_VERSION"
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
BLIT_UPDATE_TOKEN_FILE=/etc/blit/update.token
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
Depends: libc6, libdrm2, zlib1g
Suggests: hibr
Description: API-driven SDL3/KMS text display server
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
# The runtime boundary is deliberately an allowlist.  Reject any shell
# implementation or Hibr launcher if either ever reappears in the package.
if dpkg-deb --contents "$output" | grep -Eq '(\.sh$|/((start|run|attach|hibr)-|((start|run|attach|hibr))$))'; then
	rm -f "$output"
	echo "blit: runtime package contains a shell implementation or Hibr launcher" >&2
	exit 1
fi
echo "$output"
