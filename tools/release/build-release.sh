#!/bin/sh
# tools/release/build-release.sh OUTDIR
#
# A release's packages, built and checked in Blit's build environment
# (tools/build-env, Debian 13; docs/toolchain.md, "Build environment"), on
# any host with podman: the server, the update helper and the compositor
# built from this checkout; compositor_gate and xwayland_gate, which must
# pass (a skip is a failure here: the environment has every dependency);
# the packages (packaging/build-deb.sh) into OUTDIR; the release manifest
# releases/<version>.yaml from the server package; and
# package_artifact_gate on it. Prints the packages' sha256 last.
#
# BLIT_HIBR_ROOT is the Hibr tree of packaging/HIBR_VERSION the console
# adapter is built against (packaging/build-deb.sh); it and OUTDIR must be
# inside the checkout, which the environment sees at /blit. The packages
# are the same bytes from any checkout on any host.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$#" -eq 1 ] || { echo "usage: tools/release/build-release.sh OUTDIR" >&2; exit 2; }
mkdir -p "$1"
out=$(CDPATH= cd -- "$1" && pwd)
hibr=${BLIT_HIBR_ROOT:-}
[ -n "$hibr" ] && [ -d "$hibr" ] || { echo "blit: BLIT_HIBR_ROOT names no Hibr tree" >&2; exit 1; }
hibr=$(CDPATH= cd -- "$hibr" && pwd)
for path in "$out" "$hibr"; do
    case "$path/" in
        "$ROOT"/*) ;;
        *) echo "blit: $path is outside the checkout $ROOT" >&2; exit 1 ;;
    esac
done
# The same places as the environment sees them.
out=/blit${out#"$ROOT"}
hibr=/blit${hibr#"$ROOT"}
version=$(sed -n '1p' "$ROOT/packaging/VERSION")

BLIT_HIBR_ROOT=$hibr "$ROOT/tools/build-env/run.sh" sh -eu -c '
out=$1 version=$2
gate() {
    name=$1
    shift
    if "$@" > "$out/$name.txt" 2>&1; then
        grep -E "_GATE_PASS|_MEASURE|_SANITIZERS_OK" "$out/$name.txt" | cut -c1-120
    else
        status=$?
        tail -40 "$out/$name.txt" >&2
        echo "blit: $name ended $status (77 is a skip, a failure here)" >&2
        exit 1
    fi
}
git --no-pager log --oneline -1
tools/build-tcc.sh
tools/build-tcc.sh build/blit/blit-update-helper helper
gate compositor_gate sh tools/tests/compositor_gate.sh
gate xwayland_gate sh tools/tests/xwayland_gate.sh
rm -f "$out"/*.deb
packaging/build-deb.sh "$out"
tools/release/generate-manifest.sh "$out/blit_${version}-1_amd64.deb" "releases/$version.yaml"
gate package_gate env BLIT_GATE_PACKAGE="$out/blit_${version}-1_amd64.deb" tools/run-gates.sh packaging
grep RUN_GATES_PASS "$out/package_gate.txt"
' build-release "$out" "$version"

# ADR 0232: the real update helper and real dpkg, as root in a throwaway
# container of the same environment (a skip or failure ends the release).
pair_log=$ROOT${out#/blit}/update_pair_gate.txt
if ! BLIT_BUILD_ENV_ROOT=1 "$ROOT/tools/build-env/run.sh" \
        sh tools/tests/update_pair_gate.sh "$out/blit_${version}-1_amd64.deb" \
        > "$pair_log" 2>&1; then
    tail -40 "$pair_log" >&2
    echo "blit: update_pair_gate failed" >&2
    exit 1
fi
grep UPDATE_PAIR_GATE_PASS "$pair_log"

"$ROOT/tools/build-env/run.sh" sh -eu -c '
out=$1 version=$2
for p in blit blit-compositor; do
    dpkg-deb -f "$out/${p}_${version}-1_amd64.deb" Package Version Depends | paste -sd " "
done
cd "$out" && sha256sum blit_*.deb blit-compositor_*.deb
' build-release "$out" "$version"
