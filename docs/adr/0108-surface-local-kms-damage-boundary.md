# ADR 0108: Surface-local KMS damage retains an atomic output boundary

- Status: accepted
- Note: damage accounting corrected by [ADR 0116](0116-damage-union-preserves-all-pending-work.md)
- Date: 2026-10-17
- Scope: direct-KMS terminal scene composition and refresh scheduling
- Supersedes: the full-output change rule in [ADR 0106](0106-kms-changed-terminal-views-rebuild-the-scene.md)

## Context

The direct-KMS presenter correctly rebuilt the complete 1920x2160 CPU scene
whenever any terminal view changed. That eliminated stale pixels in a narrow
test, but it also made a fast-moving terminal surface redraw the whole output
for every update. The live motion workload then missed refresh deadlines and
became visibly slow.

Blit already keeps a persistent scene buffer and each terminal surface carries
authoritative cell damage. The missing distinction was between a changed
surface and a changed surface origin or view topology.

## Decision

Keep the page flip as the atomic output boundary, but redraw only the changed
surface's authoritative damage into the persistent scene. Rebuild the complete
output when the output is first initialized, the view count changes, a surface
origin changes, or an output-wide effect/overlay revision changes.

Graphics mutations still expand the affected surface redraw to its complete
surface region, because an image can cover cells outside the text damage.

## Consequences

- Fast terminal motion no longer requires a full-output CPU redraw per update.
- The submitted scanout remains a complete scene, not a partial framebuffer.
- Pane relocation and join topology changes retain the conservative full-output
  rebuild needed to clear the old physical region.
- Hibr source and terminal ownership are unchanged.

## Verification

The KMS composition gate must cover a moving cell and a moved graphics image,
including the absence of pixels at the old position. The live direct-KMS
motion workload must report no unexpected frame starvation while the output
remains at its selected refresh rate.
