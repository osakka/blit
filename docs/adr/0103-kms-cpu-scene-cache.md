# ADR 0103: Compose direct-KMS frames in a persistent CPU scene

- Status: accepted
- Date: 2026-10-16
- Scope: direct DRM/KMS page-flip composition

## Context

The direct-KMS compositor must preserve a complete scene while two scanout
buffers alternate ownership. ADR 0101 established that an incremental frame
must begin from a complete presented scene. The first implementation copied
the front dumb buffer into the back dumb buffer and then rendered individual
terminal glyph pixels into the mapped buffer.

The live fast-screen-saver workload exposed the cost of that arrangement: a
60 Hz output was taking approximately 76--79 ms per present while a rapidly
changing full-screen terminal was being composed. CPU writes scattered across
a mapped DRM dumb buffer are an especially poor target for this workload, and
the missed deadlines caused visible incomplete motion frames and artefacts.

## Decision

Each KMS output owns a persistent normal-RAM scene buffer with the same pitch
and dimensions as its scanout buffer. KMS composition renders into that scene
buffer. Before a page flip, Blit copies each row sequentially into the
currently available scanout buffer, then submits the complete buffer as one
frame. If scene allocation fails, the existing front-to-back copy path remains
the correctness-preserving fallback.

The scene buffer is invalidated by the existing full-redraw conditions. It is
not a second terminal state: cell contents, graphics placement, damage, and
z-order remain owned by the existing surface/view model.

## Consequences

- Glyph and graphics composition no longer writes scattered pixels directly
  to the mapped scanout buffer.
- Page flips still receive a complete frame, preserving atomicity and the
  stale-pixel protection from ADR 0101.
- Each submitted frame pays one sequential scene-to-scanout copy and one
  bounded scene allocation per output.
- Visual acceptance still requires a screenshot; timing acceptance requires
  the same direct-KMS lower-pane workload and measured counters.

## Verification boundary

- The KMS composition gate covers the pixel contract and scene-copy helper.
- The TCC build and focused renderer gates must pass.
- The live comparison must retain pure direct KMS, one physical output, the
  lower-half pane, and one primary Hibr session.
- No Hibr source or module is changed by this decision.
