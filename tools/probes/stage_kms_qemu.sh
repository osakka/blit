#!/bin/sh
set -eu

# Refresh a VKMS capture initroot with the exact TCC-built Blit runtime and
# every helper consumed by qemu_kms_capture_init.sh. This keeps the visual
# fixture from silently depending on stale or incomplete historical trees.
stage=${1:?usage: stage_kms_qemu.sh INITROOT}
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
binary=${BLIT_QEMU_BINARY:-$repo_root/build/blit/blit-server}
version=$(sed -n '1p' "$repo_root/packaging/VERSION")

test -d "$stage/blit-stage"
test -x "$binary"
test -n "$version"
cp "$binary" "$stage/blit-stage/blit-server"
strings "$stage/blit-stage/blit-server" | grep -Fx "$version" >/dev/null
cp "$repo_root/assets/fonts/Blit-Hibr-Terminus16.psf" \
    "$stage/blit-stage/fonts/Blit-Hibr-Terminus16.psf"

tcc "$script_dir/pty_input_echo.c" -o "$stage/blit-stage/pty-input-echo"
tcc "$script_dir/http_put.c" -o "$stage/blit-stage/http-put"
tcc "$script_dir/ppm_assert.c" -o "$stage/blit-stage/ppm-assert"
cp "$script_dir/qemu_kms_capture_init.sh" "$stage/init"

test -x "$stage/blit-stage/blit-server"
test -x "$stage/blit-stage/pty-input-echo"
test -x "$stage/blit-stage/http-put"
test -x "$stage/blit-stage/ppm-assert"
echo "staged Blit $version $(sha256sum "$stage/blit-stage/blit-server" | cut -d ' ' -f 1)"
