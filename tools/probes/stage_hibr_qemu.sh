#!/bin/sh
set -eu

# Stage the current Hibr runtime and canonical Blit binary into an existing
# QEMU initramfs tree. Hibr remains an external dependency: this copies its
# released artifacts and never edits the Hibr checkout. The Blit binary is
# always replaced and version-checked, so stale test images cannot pass.

stage=${1:?usage: stage_hibr_qemu.sh INITROOT HIBR_ROOT}
hibr_root=${2:?usage: stage_hibr_qemu.sh INITROOT HIBR_ROOT}
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
blit_binary=${BLIT_QEMU_BINARY:-$repo_root/build/blit/blit-server}
expected_version=$(sed -n '1p' "$repo_root/packaging/VERSION")

test -x "$blit_binary"
test -n "$expected_version"
test -d "$stage/blit-stage/mods"
cp "$blit_binary" "$stage/blit-stage/blit-server"
test -x "$stage/blit-stage/blit-server"
strings "$stage/blit-stage/blit-server" | grep -Fx "$expected_version" >/dev/null
test -f "$repo_root/assets/fonts/Blit-Hibr-Terminus16.psf"
mkdir -p "$stage/blit-stage/fonts"
cp "$repo_root/assets/fonts/Blit-Hibr-Terminus16.psf" \
    "$stage/blit-stage/fonts/Blit-Hibr-Terminus16.psf"

test -x "$hibr_root/build/hibr"
test -d "$hibr_root/build/mods"
test -d "$hibr_root/examples/desktop"
cp "$hibr_root/build/hibr" "$stage/blit-stage/hibr"
cp -a "$hibr_root/build/mods/." "$stage/blit-stage/mods/"
cp -a "$hibr_root/examples/desktop/." "$stage/blit-stage/"

# Probe executables are part of the repository contract, not opaque artifacts
# copied from an earlier initramfs. Rebuild every helper used by the Hibr and
# input fixtures with the workspace TCC before staging, so a gate cannot mix
# source from one revision with binaries from another.
for probe in \
    emit_uinput:emit-uinput \
    http_put:http-put \
    pty_input_echo:pty-input-echo \
    ws_input_probe:ws-input-probe; do
    source=${probe%%:*}
    target=${probe#*:}
    tcc -std=c99 "$(dirname "$0")/$source.c" \
        -o "$stage/blit-stage/$target"
done

# The optional Blit display provider is an Hibr ABI client.  Rebuild it from
# this exact Hibr checkout instead of preserving a module compiled against a
# previous Hibr ABI; the held desktop path does not load this provider, but
# the adapter probe must always test the selected Hibr runtime honestly.
tcc -I"$hibr_root/include" -I"$hibr_root/mods" \
    -shared -fPIC -o "$stage/blit-stage/mods/console-blit.so" \
    "$(dirname "$0")/../../hibr/console-blit.c"

test -f "$stage/blit-stage/mods/console-blit.so"
test -f "$stage/blit-stage/lib/ctl.hibr"
test -x "$stage/blit-stage/emit-uinput"
test -x "$stage/blit-stage/http-put"
test -x "$stage/blit-stage/pty-input-echo"
test -x "$stage/blit-stage/ws-input-probe"
echo "staged Blit $expected_version $(sha256sum "$stage/blit-stage/blit-server" | cut -d ' ' -f 1)"
echo "staged Hibr $(sha256sum "$stage/blit-stage/hibr" | cut -d ' ' -f 1)"
