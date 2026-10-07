#!/bin/sh
set -eu

# Stage the current Hibr runtime into an existing Blit QEMU initramfs tree.
# Hibr remains an external dependency: this copies its released artifacts and
# never edits the Hibr checkout. Blit-specific files already in blit-stage
# (console-blit.so, adapters, fonts, and the server) are preserved.

stage=${1:?usage: stage_hibr_qemu.sh INITROOT HIBR_ROOT}
hibr_root=${2:?usage: stage_hibr_qemu.sh INITROOT HIBR_ROOT}

test -x "$hibr_root/build/hibr"
test -d "$hibr_root/build/mods"
test -d "$hibr_root/examples/desktop"
test -d "$stage/blit-stage/mods"

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

test -f "$stage/blit-stage/mods/console-blit.so"
test -f "$stage/blit-stage/lib/ctl.hibr"
test -x "$stage/blit-stage/emit-uinput"
test -x "$stage/blit-stage/http-put"
test -x "$stage/blit-stage/pty-input-echo"
test -x "$stage/blit-stage/ws-input-probe"
echo "staged Hibr $(sha256sum "$stage/blit-stage/hibr" | cut -d ' ' -f 1)"
