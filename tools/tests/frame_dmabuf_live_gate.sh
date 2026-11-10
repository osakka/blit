#!/bin/sh
# ADR 0223: frames by dmabuf on a live host with a GPU that can import them.
#   frame_dmabuf_live_gate.sh HOST
# HOST is an address (port 8080) or a base URL; BLIT_AUTH_TOKEN is its API
# token, BLIT_GATE_LIVE_USER the account the producer runs as (default
# $USER) and BLIT_GATE_LIVE_DISPLAY the display it is shown on (default the
# first). Builds the test producer here (tcc, libc only) and runs it there.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
[ "$#" -eq 1 ] || { echo "usage: $0 HOST" >&2; exit 2; }
mkdir -p build/tests
cflags=$(tools/build-flags.sh --cflags gles)
# shellcheck disable=SC2086
tcc -Wall $cflags tools/tests/frame_dmabuf_producer.c -ldl \
    -o build/tests/frame_dmabuf_producer
python3 tools/tests/frame_dmabuf_live_gate.py "$1" build/tests/frame_dmabuf_producer
