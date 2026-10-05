# Direct KMS boundary

The production server now owns this boundary in `src/blit_kms.c`. Build it
with the TCC toolchain:

```sh
tcc -std=c99 -Ivendor/SDL3/include \
  -Ibuild/deps/prefix/include -Ibuild/deps/prefix/include/libdrm \
  src/blit_server.c src/blit_kms.c \
  -Lbuild/sdl3-kms -Lbuild/deps/prefix/lib/x86_64-linux-gnu \
  -lSDL3 -ldrm -lutil -o build/blit/blit-server-kms
```

On a DRM-capable guest VT, run the production path:

```sh
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=0.0.0.0 \
  ./build/blit/blit-server-kms 8080
```

It must report connected outputs or fail explicitly with a DRM diagnostic. A
host without `/dev/dri/card0` is not a valid presentation test.

The direct-display migration begins with the DRM/KMS boundary, independently
of Hibr and SDL3. The diagnostic probe enumerates cards, connectors, and modes
without changing display state:

```sh
tcc -std=c99 \
  -Ibuild/deps/prefix/include \
  -Ibuild/deps/prefix/include/libdrm \
  tools/probes/drm_kms.c \
  -Lbuild/deps/prefix/lib/x86_64-linux-gnu -ldrm \
  -o build/blit/drm-kms-probe
sudo ./build/blit/drm-kms-probe
```

The production KMS backend uses the same inventory boundary with explicit
connector/CRTC ownership, dumb-buffer allocation, and page-flip completion.
The probe remains read-only; it is not a second presentation implementation.
If a driver does not deliver a page-flip completion within the bounded wait,
Blit records the connector and switches that output to a direct modeset path
instead of freezing the entire gateway.

The page-flip wait is bounded to 100 ms per output by default. This prevents a
lost or delayed DRM event from turning one logical frame into a multi-second
stall; a timeout is logged with the connector and that output moves to the
explicit modeset fallback. `BLIT_KMS_FLIP_TIMEOUT_MS` may set a diagnostic
budget from 1 to 1000 ms.

After the first complete composition, direct KMS preserves each scanout back
buffer and uses the canonical surface revision/damage rectangle to repaint only
changed cells. A complete redraw is retained for the first frame, output/view
set changes, and software-pointer movement so stale pixels cannot survive.

Expected output identifies every usable card and connected connector, then
lists its modes and preferred mode. A missing card, connector, or mode is a
structured capability result—not a reason to fall back silently to SDL text
rendering.
## Logical width and pointer input

Direct KMS normally uses the packaged 8-pixel-wide console font, which gives
128 columns on a 1024-pixel output. To request approximately 200 columns,
start the server with:

```sh
BLIT_KMS_CELL_WIDTH=5 ./bin/start-blit
```

The override is authoritative: pane discovery, rendering, cursor placement,
and pointer hit-testing all use the same metric. The default remains the font's
native width. The Hibr adapter receives targeted pointer motion and button
press/release events over the same stream as keyboard input; its session ID is
normalized to Blit's `session-N` stream identity before filtering.

The production launcher selects the bundled Unicode Terminus PSF font and the
server logs the selected font dimensions and glyph count. Pointer and surface
presentation use a bounded 16 ms render gate by default; set
`BLIT_RENDER_INTERVAL_MS` between 1 and 100 to measure a different hardware
trade-off without changing terminal state semantics. Set
`BLIT_KMS_TRACE_TIMING=1` to log measured KMS frame durations. The KMS path
does not submit a page flip when the output has no changed surface, cursor,
pointer, or topology state; this is the idle CPU/latency guard. A first frame,
changed pane, or pointer movement can still incur the physical driver's page
flip latency, which is reported per present and bounded by
`BLIT_KMS_FLIP_TIMEOUT_MS`.

Evdev synchronization reports are not treated as pointer motion unless the
absolute pointer position or output actually changes. This prevents devices
that emit idle `SYN_REPORT` traffic from scheduling a full pointer redraw on
every report; button and wheel events remain independently routed.

## Two-output pixel gate

The production KMS path has a separate capture fixture so visual validation is
not inferred from HTTP state or the Hibr control path. Stage the current server
and run:

```sh
cp tools/probes/qemu_kms_capture_init.sh /tmp/hibr-initroot-current/init
(cd /tmp/hibr-initroot-current && \
  find . -print | cpio -o -H newc | gzip -1 > /tmp/alpine-vkms-capture-initramfs.gz)
qemu-system-x86_64 -M q35 -m 1024 -smp 2 \
  -kernel /tmp/linux-vkms-build/arch/x86/boot/bzImage \
  -initrd /tmp/alpine-vkms-capture-initramfs.gz \
  -append 'console=ttyS0 init=/init vkms.create_default_dev=0' \
  -display none -serial stdio -monitor none -no-reboot
```

`KMS_CAPTURE_PASS` requires two guest-generated PPMs, each with a 1024×768
header and the exact 24-bit payload size, and requires distinct SHA-256 hashes
for the two outputs. The current run passed with hashes beginning `0588af44`
and `a397ab92`. The tiny VKMS kernel does not include 9p, so the PPMs are
validated over the serial log; an empty host screenshot directory is not
evidence that the frames were absent.
