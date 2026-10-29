# ADR0176: measured performance and bounded associative glyph reuse

- Status: accepted for 0.2 development; not deployed
- Date: 2026-10-21
- Scope: internal Blit122/123, shared raster cache, performance acceptance

## Context

Keep released/tagged 0.1.589 as the correctness checkpoint. Start package
development at 0.2.0 without claiming a released 0.2.0, changing the API
contract version, changing the saved presentation backend, or editing Hibr.
The owner wants lower CPU and smoother graphical/text updates than Kitty.
That must mean named, reproducible workloads, not universal superiority.

The released shared CPU raster cache has 128 direct-mapped slots. At 11x22px,
200 warmed passes over ASCII plus the canonical Hibr UI snapshot perform
28,800 rasterizations for 39,200 lookups. Three different full keys sharing
one bucket perform 600 rasterizations for 600 warmed lookups despite using
under 1KB. Plain printable ASCII performs none. Full-key correctness exists;
replacement policy wastes work. This is a deterministic cache finding, not
proof of its contribution to live frame time.

## Decision

Use 128 sets with four ways each in the existing backend-independent raster
owner. Compare complete cluster/column keys, retain up to four colliding
glyphs and evict the least recently used way. Bounded per-set age ranks avoid
an overflowing global timestamp. Keep the existing 4MiB resident pixel budget,
transactional failed lookup behaviour, glyph format/metrics/pixels and pointer
lifetime. Metadata grows from 128 to 512 entries; the pixel budget does not.
Font contexts remain single-owner and immutable in configuration after setup.
If large glyphs exhaust the byte budget, reclaim other resident entries in
bounded round-robin order after the new glyph has rendered successfully.
Normalize ages within affected sets. A valid supported glyph must not become
an unsupported lookup just because the cache is full. The all-size emoji
presentation gate caught this failure in the initial associative candidate;
that candidate is not accepted or deployed.

No second terminal state, protocol path, font shaping implementation or cache
owner is introduced. The SDL atlas is a different resource with in-flight
texture consumers: do not change its replacement/flush rules as part of this
CPU-cache change. Native KMS and SDL consumers use the same improved raster
owner, but that does not certify equivalent end-to-end speed.

Keeping direct mapping is rejected because it repeats proven work with spare
capacity. Unbounded caches and a full-table linear search on every lookup are
rejected for memory/lookup predictability. A global GPU-cache rewrite is not
justified by this CPU-cache experiment and belongs to later profiled work.

Measure server CPU, parser work, raster/composition, copy/upload, presentation
and input latency separately. Distinguish native CPU composition from SDL/GL,
and Hibr/hold from Blit. Reuse current diagnostic APIs before adding counters;
any new public telemetry must be schema-first. See the
[0.2 performance programme](../history/performance-0.2.md).

## Verification and limits

`ruby tools/probes/font_cache_workload.rb --check` uses production TCC raster
code, pinned repo font assets, full pixel/metric signatures and both profiles.
The three warmed workloads must have zero repeat rasterizations within the
unchanged pixel budget. Timing includes signature verification and is not a
display-FPS assertion. Source/font digests accompany every report.

The font raster gate tests five colliding keys, LRU survival/eviction and
unchanged re-rasterized pixels at every size 8..64. Emoji gates retain full
cluster collision checks; Arabic, colour, native-KMS and SDL composition gates
remain required. Existing correctness tests must pass before deployment.
Actual live CPU/latency gains require a subsequent matched workload run.
