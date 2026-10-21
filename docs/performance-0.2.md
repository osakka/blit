# Blit 0.2: correctness-preserving performance

Stable checkpoint: **v0.1.589**. Development candidate: **0.2.0**.
Internal tracker: Blit122; first cache change123; reuse Blit120 for solid-copy integration and104 for
backend parity. These are internal backlog identifiers, not GitHub issue links.
Nothing here authorizes silent backend/layout/font changes or Hibr code edits.

## Why Kitty is a useful reference

Kitty's [performance documentation](https://sw.kovidgoyal.net/kitty/performance/)
describes vectorized parsing and compact updates sent to the GPU. Its
[font implementation](https://github.com/kovidgoyal/kitty/blob/aef2bc23065412e95dbcdaf048c233e4537d444a/kitty/fonts.c)
has shaped-run/glyph caches and GPU sprite tracking. Learn from the architecture,
not by copying a second terminal implementation into Blit.

Our sampled live host is using **native direct-KMS CPU composition**, not SDL/GL.
One observed 1920x2160 frame at59.997Hz spent19.209ms drawing,0.984ms copying
and0.044ms submitting, drawing1944 cells. Drawing alone exceeds the16.67ms
60Hz budget. This identifies a phase worth profiling, not the expensive
function or a fair Kitty comparison. SDL/GL selection alone would not remove
CPU rasterization, uploads, excess damage or application/hold transport work.

A subsequent read-only5.091s server window on589 observed9.31% of one server
CPU core,36 presentation calls and32 missed deadlines. The last sampled frame
spent17.542ms drawing and1.081ms copying. There was no controlled workload;
do not call these active FPS, Hibr CPU, confirmed flips or performance under
dragging. Polling itself has overhead. No live settings or sessions changed.

## First measured improvement: shared raster reuse

Production cache workload,11x22px cells,200 warmed passes per case:

| Workload, each Retro and Modern | Lookups | 589 repeat rasterizations | 0.2 candidate |
| --- | ---: | ---: | ---: |
| Printable ASCII,95 keys | 19,000 | 0 | 0 |
| ASCII + canonical Hibr UI,196 keys | 39,200 | 28,800 | 0 |
| Three same-bucket keys: A, Arabic tah, colour maple leaf | 600 | 600 | 0 |

All resulting glyph pixels/metrics match their initial signatures in each
run. The196-key working set occupies47,432 pixel bytes after the change,
versus28,072 with thrashing; the existing4MiB resident pixel ceiling remains.
ASCII is the no-churn control, not an advertised speedup. Full-key retention
and four-way LRU are recorded in[ADR0176](adr/0176-measured-performance-and-raster-cache.md).
This removes proven repeated work; it does **not** yet quantify live frame gain.

Stage two/internal125 begins with[retained graphics damage](adr/0177-retained-graphics-damage.md):
placement-only commands and one shared comparison against the last composed
scene. Both renderers preserve unchanged wallpaper outside affected regions.
The new production fixture compares36 steps with full-redraw pixel oracles
on native KMS (DRM mocked), software SDL and offscreen GL. It also verifies
no movement-only texture uploads/whole-renderer clears and held/deferred
damage retention. Completed protocol-image uploads remain full-damage for
now; this is not yet the entire video-plus-drag performance fix.

Reproduce without a display or changing installed fonts:

```bash
ruby tools/probes/font_cache_workload.rb --check
```

For bounded read-only API observations, supply the existing bearer token
through `BLIT_AUTH_TOKEN` and optionally `BLIT_LIVE`, then run:

```bash
ruby tools/probes/performance_baseline.rb 10
```

It uses existing version/diagnostic/runtime endpoints. Output excludes bearer
tokens, application text and full settings. Last-frame samples can repeat;
they are not per-frame percentiles. Version/backend/geometry changes invalidate
the window; application state/font identity still require explicit control.

## Execution and acceptance order

The deeper[source-level Kitty comparison](kitty-rendering-investigation.md)
and production work-shape probe identify blanket graphics invalidation,
CPU native composition and avoidable SDL upload preparation. Prioritize the
revised work-model sequence there; glyph caching is not a complete explanation
for the owner's video-plus-window-movement workload.

1. Preserve589 tag/package/settings as rollback. Establish identical input
   traces and visual oracles before measuring each candidate.
2. Measure and remove shared CPU work: cache churn, damage area amplification,
   per-pixel composition and redundant grid/graphics scans. Profile actual
   functions rather than infer them from a phase counter.
3. Measure SDL atlas collisions, flushes, image upload/reuse and draw batching;
   improve retained GPU resources without violating in-flight ownership.
   Preserve native KMS parity and API configuration ownership.
4. Validate Hibr337's upstream0.99.283 transport support through a **new hold
   server**, only after checking the installed version. Blit120 remains open
   until real solid-drag bytes/CPU/cadence are measured. Outline stays default;
   translucent windows cannot be treated as opaque rectangle copies.
5. Run matched Blit/Kitty workloads with the same Hibr version, font/cell
   geometry, wallpaper/render mode, grid, display/GPU/refresh and input trace.
   Separate direct-client and hold runs. Include idle, small/full text updates,
   scrolling, Arabic/emoji, Kitty/sixel/half-block pictures, opaque/translucent
   movement, and one/two/three physical outputs where available.

Record CPU for Blit **and** Hibr/hold; include Kitty's compositor/GPU costs.
Measure bytes parsed, pixels/cells damaged, cache work, upload/copy volume,
input-to-visible latency and confirmed per-output flips. Parser throughput
with rendering suppressed is a different benchmark. Do not confuse refresh
timestamps, presentation calls or repeated unchanged frames with useful FPS.

Active-workload budgets are16.67ms at60Hz,8.33ms at120Hz and4.17ms at240Hz.
Report median/p95/p99 latency and missed deadlines with sampling methodology;
last-frame diagnostic polling cannot supply those distributions. Fast enough
means meeting each display's cadence with headroom and low CPU, not repeatedly
redrawing idle content above scanout speed. A screenshot match alone cannot
prove movement correctness or latency.

Each cycle requires unchanged terminal/graphics/input semantics, pixels and
saved configuration; reproducible tests; an evidence-linked ticket/ADR/doc
update; and commit/push. Release/deploy0.2 only after agreed live acceptance.
