#!/bin/sh
# tools/build-env/run.sh COMMAND [ARG...]
#
# Runs COMMAND in Blit's build environment -- Debian 13 with the packages
# in tools/build-env/packages (docs/toolchain.md, "Build environment") --
# in the checkout, mounted at /blit whichever checkout it is, and as the
# calling user, so what it writes is the user's. The fixed path makes the
# builds the same bytes from any checkout on any host: blit-server keeps
# its debug information (backtraces), which records the source paths.
# Paths given to COMMAND are relative to the checkout. The image is built when it is missing or
# when Containerfile or packages changed (its tag is their hash). The host
# needs podman; it is run through sudo where rootless podman cannot run a
# container (an unprivileged LXC host cannot mount /proc for it).
# BLIT_BUILD_ENV_PODMAN overrides the podman command ("podman" or
# "sudo -n podman"). Host networking: gates listen on 127.0.0.1 ports.
# BLIT_BUILD_ENV_ROOT=1 runs COMMAND as root in the container instead: only
# for a gate that installs packages into the throwaway container
# (update_pair_gate, ADR 0232). It must not write into the checkout.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DIR=$ROOT/tools/build-env
[ "$#" -gt 0 ] || { echo "usage: tools/build-env/run.sh COMMAND [ARG...]" >&2; exit 2; }

podman_cmd=${BLIT_BUILD_ENV_PODMAN:-}
if [ -z "$podman_cmd" ]; then
    if [ "$(id -u)" = 0 ]; then
        podman_cmd=podman
    else
        podman_cmd="sudo -n podman"
    fi
fi
command -v podman > /dev/null 2>&1 || { echo "blit: the build environment needs podman" >&2; exit 1; }

tag=$(cat "$DIR/Containerfile" "$DIR/packages" | sha256sum | cut -c1-12)
image=localhost/blit-build-env:$tag
# shellcheck disable=SC2086
if ! $podman_cmd image exists "$image"; then
    echo "blit: building the build environment $image" >&2
    $podman_cmd build --network=host -t "$image" -f "$DIR/Containerfile" "$DIR" >&2
fi

# The variables a build or a gate reads, passed by value when set: sudo
# does not pass the caller's environment on. They go before the image, the
# command after it.
set -- "$image" "$@"
for name in BLIT_GATE_TIMEOUT BLIT_GATE_PACKAGE BLIT_HIBR_ROOT; do
    eval "isset=\${$name+set}"
    [ "$isset" = set ] || continue
    eval "value=\$$name"
    set -- -e "$name=$value" "$@"
done
# shellcheck disable=SC2086
user="$(id -u):$(id -g)"
[ "${BLIT_BUILD_ENV_ROOT:-}" = 1 ] && user=0:0
exec $podman_cmd run --rm --network=host \
    --user "$user" -e HOME=/tmp -e LANG=C.UTF-8 \
    -v "$ROOT:/blit" -w /blit \
    "$@"
