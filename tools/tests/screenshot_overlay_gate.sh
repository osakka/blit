#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_SCREENSHOT_OVERLAY_PORT:-19196}
port=$(gate_port "$port")
log=${TMPDIR:-/tmp}/blit-screenshot-overlay.$$.log
before=${TMPDIR:-/tmp}/blit-screenshot-overlay.$$.before.bmp
after=${TMPDIR:-/tmp}/blit-screenshot-overlay.$$.after.bmp
cached=${TMPDIR:-/tmp}/blit-screenshot-overlay.$$.cached.bmp
pid=

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ]; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log" "$before" "$after" "$cached"
}
trap cleanup EXIT INT TERM

env $GATE_DISPLAY_ENV SDL_RENDERER=software \
    LD_LIBRARY_PATH=build/sdl3-kms "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/server" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 1
done
test "$ready" = 1

overlay=$(curl --connect-timeout 2 --max-time 3 --fail --silent \
    --request POST --header 'Content-Type: application/json' \
    --data '{"display_id":"display-1","x":0,"y":0,"width":12,"height":12,"color":"#ff0000","opacity":100}' \
    "http://127.0.0.1:$port/v1/overlays")
overlay_id=$(printf '%s' "$overlay" | ruby -rjson -e 'puts JSON.parse(STDIN.read).fetch("id").sub("overlay-", "")')
sleep 1
curl --connect-timeout 2 --max-time 3 --fail --silent \
    "http://127.0.0.1:$port/v1/diagnostics/screenshots/display-1" -o "$before"
curl --connect-timeout 2 --max-time 3 --fail --silent \
    "http://127.0.0.1:$port/v1/diagnostics/screenshots/display-1?cached=1" \
    -o "$cached"

curl --connect-timeout 2 --max-time 3 --fail --silent \
    --request DELETE "http://127.0.0.1:$port/v1/overlays/$overlay_id" >/dev/null
sleep 1
curl --connect-timeout 2 --max-time 3 --fail --silent \
    "http://127.0.0.1:$port/v1/diagnostics/screenshots/display-1" -o "$after"

ruby -e '
  def top_left(path)
    bytes = File.binread(path)
    raise "not BMP" unless bytes.byteslice(0, 2) == "BM"
    offset = bytes.byteslice(10, 4).unpack1("V")
    width = bytes.byteslice(18, 4).unpack1("l<")
    height = bytes.byteslice(22, 4).unpack1("l<")
    bits = bytes.byteslice(28, 2).unpack1("v")
    raise "unexpected BMP geometry" unless width > 0 && height > 0
    raise "unexpected BMP depth" unless bits == 24 || bits == 32
    bytes_per_pixel = bits / 8
    stride = ((width * bytes_per_pixel + 3) / 4) * 4
    pixel = offset + (height - 1) * stride
    bytes.byteslice(pixel, 3).bytes
  end
  before = top_left(ARGV.fetch(0))
  after = top_left(ARGV.fetch(1))
  cached = top_left(ARGV.fetch(2))
  raise "overlay was not red: #{before.inspect}" unless before == [0, 0, 255]
  raise "cached screenshot lost overlay: #{cached.inspect}" unless cached == [0, 0, 255]
  raise "red overlay survived deletion: #{after.inspect}" if after == [0, 0, 255]
  puts "SCREENSHOT_OVERLAY_GATE_PASS before_bgr=#{before.join(",")} after_bgr=#{after.join(",")}"
' "$before" "$after" "$cached"
