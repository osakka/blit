#!/bin/sh
set -eu

# Build one canonical initroot for qemu_kms_four_output_init.sh.  The fixture
# exercises both the ordinary KMS/pixel path and the optional Hibr four-output
# path, so it must contain both sets of helpers and the exact Hibr tree.  Keep
# this composition in one staging command: mixing stage_hibr_qemu and
# stage_kms_qemu by hand is an easy way to retain a stale /init or omit the
# pixel oracle.

stage=${1:?usage: stage_four_output_qemu.sh INITROOT HIBR_ROOT}
hibr_root=${2:?usage: stage_four_output_qemu.sh INITROOT HIBR_ROOT}
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

test -d "$stage/blit-stage"
test -d "$hibr_root"

# Each component stages its own canonical helpers and performs its own version
# checks. The four-output init script is installed last and is authoritative.
"$script_dir/stage_hibr_qemu.sh" "$stage" "$hibr_root"
"$script_dir/stage_kms_qemu.sh" "$stage"
cp "$script_dir/qemu_kms_four_output_init.sh" "$stage/init"
chmod 0755 "$stage/init"

grep -q 'KMS_FOUR_OUTPUT_PASS' "$stage/init"
test -x "$stage/blit-stage/blit-server"
test -x "$stage/blit-stage/bmp-pixel-assert"
test -x "$stage/blit-stage/ppm-assert"
test -x "$stage/blit-stage/hibr"
test -f "$stage/blit-stage/mods/console-blit.so"
test -f "$stage/blit-stage/blit-session.hibr"
test -f "$stage/blit-stage/blit-stock-session.hibr"

echo "staged four-output Blit $(sed -n '1p' "$(dirname "$script_dir")/../packaging/VERSION")"
