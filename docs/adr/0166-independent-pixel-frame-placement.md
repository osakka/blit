# ADR 0166: preserve pixel-frame destination independently of source

- Status: accepted; in use since 0.1.583
- Date: 2026-10-20
- Tracking: [Blit 111](https://git.home.arpa/itdlabs/blit/issues/111),
  [Hibr 319](https://git.home.arpa/itdlabs/hibr/issues/319)

Hibr's display image callback provides both a destination in cells (`h`,`w`)
and source RGB dimensions (`iw`,`ih`). Blit's provider adapter discarded the
destination and its stream consumer always used source-sized placement. At
non-default cell metrics these dimensions need not agree. Correct this in
Blit's adapter and protocol, without editing Hibr or altering browser zoom.

OpenAPI defines optional paired `placement_width`/`placement_height`, in
terminal pixels, bounded 1..65535. Legacy frames omit both and retain their
existing placement. Source width/height still govern allocation and payload
length. The adapter multiplies requested destination cells by current cell
metrics and rejects empty/oversized destinations before transport. The server
validates the pair before replacing active frame state; subsequent frames
reuse the same existing retained-image/composition path. No second renderer
or browser-specific scale compensation is introduced. Package server and
adapter together: an older server cannot honour the new fields.

The browser separately uses 8x16 CSS pixels/cell for its viewport. Correctly
fitting that screenshot to larger cells can still magnify webpage content.
Hibr 319 requests an explicit pixel-native viewport policy and matching hit
testing, including cell-metric changes without row/column changes. This is
not evidence that Hibr causes all observed rendering problems.

Tests decode the production adapter's masked WebSocket metadata at 8x16,
9x17,12x23,16x32, independently of a 2x2 RGB source. The black-box pixel-frame
gate fails against 582 and passes against 583. It verifies legacy placement,
same-source reuse, four-colour presented quadrant pixels, destination growth
and shrink with old extent cleared, and invalid pair/value rejection. Local
SDL software and offscreen OpenGL ES2 pass; renderer identity is asserted.
Desktop OpenGL cannot initialize on this offscreen driver and is not passed.
The existing native compositor clipping oracle also passes fractional scaling,
RGB/RGBA, negative origins and bounded damage; this is not a live-native
browser/font-resize acceptance run. No live font, desktop or Chrome changes.
