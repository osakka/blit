# ADR0177: one retained graphics-damage contract

- Status: accepted for development; not deployed
- Date: 2026-10-21
- Scope: internal125/122, existing canonical graphics scene and both renderers

Compare immutable bounded placement descriptors against the last scene
actually composed for each surface. Descriptors contain image/placement IDs,
pixel generation, source/destination geometry, z, format and opacity, never
retained pixel pointers. Use one comparison in the shared graphics boundary,
not independent SDL/native damage implementations. For each changed/added/
removed descriptor, repaint its old/new coverage with all intersecting layers.
An unchanged wallpaper must not expand that coverage merely by existing.

Accumulate this damage with existing terminal damage before creating renderer
work. Preserve it while output is deferred, synchronized, disabled or held.
Advance the retained descriptor snapshot only when that surface is actually
composed. Scene targets remain the existing pixel owner; these descriptors
record presented identity, not a second mutable terminal/graphics scene.

Both renderers may trust explicitly prepared complete graphics damage.
Unknown callers retain the conservative full-scene/all-image fallback.
Topology, origins, effects, first frames and existing full-damage producers
retain their existing redraw rules. No pixels, alpha/z semantics, protocol
capabilities, framebuffer-copy policy, threading or live settings change.

Placement-only Kitty commands mark old/new placement coverage without
reuploading pixels. Image upload/delete paths initially retained conservative
full damage. The second-stage extension below removes that blanket damage.
Completely offscreen scene mutations may
repaint one harmless cell to acknowledge new scene identity; metadata-only
acknowledgement is a later optimization, not a correctness shortcut.

Require production-command damage tests and full-reference versus incremental
pixel comparisons for movement/overlap/crop/z/deletion on native/software/GL.
Keep frame-lifecycle and synchronization gates. No live performance claim or
deployment until validation; do not remove fallbacks solely to improve counts.

## Protocol-producer extension, 2026-10-21

Completed Kitty and sixel images mark their current placement coverage using
the canonical graphics collector. Kitty deletion marks each removed placement
before unlinking/freeing it. These marks wake the existing damage-driven
scheduler; the shared last-composed comparison still supplies old/revealed
coverage before composition. Do not add a second retained scene or acknowledge
the presented snapshot when parsing a protocol command. Partial uploads remain
invisible. Terminal/cursor/scroll damage continues to accumulate normally.

An unplaced, offscreen or no-op command may mark one harmless cell to retain
scheduler acknowledgement. Full-wallpaper removal and actual full-terminal
changes are allowed full coverage; only unconditional invalidation is removed.
Keep the existing fallback for callers without prepared complete damage.

Extend the production parser/render fixture with chunked uploads, multiple
placements, placement-only/data deletion and opaque/transparent sixel over
retained wallpaper. Compare every incremental frame with a forced full redraw;
check bounded producer damage and retain it across synchronization, masked
outputs and pointer-only held attempts. Native composition retains its existing
full-reference descriptor tests and deferred-flip checks. This does not change
protocol vocabulary, scanout copying, GPU architecture or live configuration.
