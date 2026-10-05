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

test -f "$stage/blit-stage/mods/console-blit.so"
test -f "$stage/blit-stage/lib/ctl.hibr"
echo "staged Hibr $(sha256sum "$stage/blit-stage/hibr" | cut -d ' ' -f 1)"
