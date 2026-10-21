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
reuploading pixels. Existing image upload/delete full-damage paths remain
conservative in this first stage. Completely offscreen scene mutations may
repaint one harmless cell to acknowledge new scene identity; metadata-only
acknowledgement is a later optimization, not a correctness shortcut.

Require production-command damage tests and full-reference versus incremental
pixel comparisons for movement/overlap/crop/z/deletion on native/software/GL.
Keep frame-lifecycle and synchronization gates. No live performance claim or
deployment until validation; do not remove fallbacks solely to improve counts.
