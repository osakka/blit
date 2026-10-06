# Direct KMS boundary

The production server now owns this boundary in `src/blit_kms.c`. Build it
with the TCC toolchain:

```sh
tcc -O2 -std=c99 -Ivendor/SDL3/include \
  -Ibuild/deps/prefix/include -Ibuild/deps/prefix/include/libdrm \
  src/blit_server.c src/blit_kms.c src/blit_font.c \
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

The DRM file descriptor is part of the gateway's main `select()` set. A
pending page flip is serviced there; while one connector owns its back buffer,
Blit skips only that connector and continues servicing HTTP, PTY, input, and
the other displays. This prevents a slow monitor from blocking the gateway.
The bounded timeout still moves a connector to the explicit modeset fallback
if its completion event does not arrive.

After the first complete composition, direct KMS preserves each scanout back
buffer and uses the canonical surface revision/damage rectangle to repaint only
changed cells. A complete redraw is retained for the first frame and
output/view set changes. Pointer movement preserves the displayed front buffer,
repaints the old and new pointer rectangles at cell granularity, and does not
redraw the entire terminal; this prevents mouse motion from monopolizing a
slow DRM driver or leaving pointer trails.

When all cells in an affected view have the same effective background color,
the renderer fills the view rectangle once before drawing glyphs. Mixed
backgrounds are coalesced into horizontal runs, and inverse cells continue
through the same effective-color path so color and attribute semantics remain
exact. Native-size PSF rows are cached in the shared font object and the KMS
renderer uses a native-size bitmap path; scaled cells retain the general
scaler. These are bounded hot-path optimizations, not a second renderer.

Surface damage is consumed only after the corresponding view is submitted.
If a connector is still waiting for its page-flip event, its damage remains
pending and is retried after that event; a one-cell API mutation therefore
cannot accumulate into a historical full-row redraw.

Expected output identifies every usable card and connected connector, then
lists its modes and preferred mode. A missing card, connector, or mode is a
structured capability result—not a reason to fall back silently to SDL text
rendering.
## Logical width and pointer input

Direct KMS normally uses the packaged 8-pixel-wide console font, which gives
128 columns on a 1024-pixel output. To request approximately 200 columns,
start the server with:

```sh
BLIT_KMS_CELL_WIDTH=5 BLIT_DISPLAY_BACKEND=kms blit 8080
```

The override is authoritative: pane discovery, rendering, cursor placement,
and pointer hit-testing all use the same metric. The default remains the font's
native width. The Hibr adapter receives targeted pointer motion and button
press/release events over the same stream as keyboard input; its session ID is
normalized to Blit's `session-N` stream identity before filtering.

`BLIT_FONT_SCALE` scales the loaded glyph bitmap into the complete logical
cell in both axes; it does not merely enlarge the cell while leaving an
unscaled glyph in its upper-left corner.

The production launcher selects the bundled Unicode Terminus PSF font and the
server logs the selected font dimensions and glyph count. Presentation is
refresh-aware: each active output must schedule independently from its
selected mode refresh, and no configured render interval may intentionally
under-run that cadence. `BLIT_RENDER_INTERVAL_MS` remains a diagnostic
override for measuring a hardware trade-off, but the production scheduler
must clamp it so it cannot lower an output below its mode refresh. Set
`BLIT_KMS_TRACE_TIMING=1` to log measured KMS frame durations. The KMS path
does not submit a page flip when the output has no changed surface, cursor,
pointer, or topology state; this is the idle CPU/latency guard. A first frame,
changed pane, or pointer movement can still incur the physical driver's page
flip latency, which is reported per present and bounded by
`BLIT_KMS_FLIP_TIMEOUT_MS`.

The software pointer is an overlay, not terminal content. Each output retains
the pixels beneath its pointer and restores that underlay before drawing the
next position; pointer motion therefore cannot paint black trails over Hibr's
background or wallpaper.

If a page-flip event exceeds that timeout, Blit performs one explicit modeset
recovery for the affected output and then resumes ordinary page flips. A
timeout must never become a modeset-per-frame loop: repeated modesets blank
and visibly disturb the display while making pointer and terminal updates
appear stalled.

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
header and the exact 24-bit payload size. The TCC-built `ppm-assert` oracle
also checks black background pixels and a known `READY` glyph in the expected
cell origin on both displays. Display 47 also emits a U+2500 wide cell and
the oracle requires foreground pixels in both eight-pixel halves, proving the
renderer paints the logical two-cell width rather than only its first cell.
Distinct SHA-256 hashes remain an additional check that the two outputs are
not the same buffer. The tiny VKMS kernel does
not include 9p, so the PPMs are validated over the serial log; an empty host
screenshot directory is not evidence that the frames were absent. The same
gate checks `/v1/graphics` for the VKMS vendor/device IDs, driver, DRM device
node, and hardware-acceleration capability fields; unavailable sysfs values
are reported as `unknown` rather than invented.

The same fixture accepts `blit.output_loss=1` on the kernel command line. It
disables connector 47 during the run and requires `OUTPUT_LOSS_PASS`: the
failed connector remains in `/v1/displays` as `failed`, the other connector
remains `presenting`, and `/v1/diagnostics` reports
`display_presentation_failed` while the server stays ready.

The fixture also accepts `blit.kitty_stack=1`. This sends two overlapping
Kitty images with different z values and a two-pixel image positioned partly
outside the pane. `PPM_DISPLAY_47_WIDE_STACK_CLIP_PASS` requires the higher-z
pixel and the clipped image's visible second pixel at their exact coordinates;
the equivalent SDL3 fixture uses the BMP oracle.

## Pointer stress gate

The input-only QEMU variant starts two terminal surfaces, routes the synthetic
evdev keyboard and pointer independently, and emits 180 pointer motions at
16 ms intervals with periodic primary-button and wheel events. It must report
both `BLIT_INPUT_KEYPAD_PASS` and `BLIT_INPUT_STREAM_PASS`. With
`BLIT_KMS_TRACE_TIMING=1`, the renderer's per-present timings are also retained
in the serial log; the current TCG/VKMS run stayed below 103 ms for the normal
stress frames, with one 482 ms fixture outlier, and no multi-second redraw.
For phase diagnosis, `BLIT_KMS_TRACE_PHASES=1` additionally records wait,
glyph-draw, and page-flip intervals. The optimized PSF rasterizer's current
two-output Hibr fixture measured 56 samples with a maximum 147.739 ms glyph
draw and 154.447 ms total present under QEMU TCG; the acceptance marker and
pixel gate must still pass when timing is enabled.

The refresh gate must record, per output, selected refresh, effective
scheduler cadence, missed-deadline count, and page-flip wait time. The
acceptance run uses at least two outputs with different selected refresh
rates and requires that the faster output is not reduced to the slower
output's cadence. A result is invalid if it only demonstrates that frames
were eventually drawn; it must separate scheduler delay from physical
vblank/page-flip delay.
The input fixture also crosses the synthetic pointer from output 1 to output
2 and verifies that streamed motion changes from the first PTY session to the
second rather than being broadcast to both. It overlaps left, middle, and
right button presses, verifies motion while buttons remain held, and checks
that each release preserves the remaining-button state. The pointer PTY also
enables DEC focus reporting and must receive the focus-in sequence after the
cross-display click.
The fixture creates two independent relative mice and requires
`BLIT_INPUT_MULTI_MOUSE_PASS`, proving that Blit services every discovered
pointer FD instead of silently keeping only the first device.
