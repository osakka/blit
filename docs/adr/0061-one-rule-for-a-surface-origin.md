# ADR 0061: A surface's origin on an output is computed in one place

- Status: accepted
- Date: 2026-10-10
- Scope: surface placement, pointer hit testing, both renderers

Update 2026-10-17: the single-origin decision remains accepted.
[ADR 0123](0123-centred-grids-and-complete-font-rasters.md) supersedes its
four-pixel margin: the complete-cell grid already owns centering, so the extra
inset is now zero. Historical defect/evidence below is retained as history.

## Context

Three pieces of code needed to know where a surface sits on an output, and
each worked it out for itself:

| | pane surface | surface with no pane |
| --- | --- | --- |
| SDL renderer | pane cell position + 4 px inset | inset, stacked below **every** surface on the output, in frame-plan order |
| KMS renderer | pane position scaled, + 4 px | the inset corner, always -- so two of them drew on top of each other |
| `mouse_target` | pane position, **no inset** | inset, stacked below the **pane-less** surfaces, in session-array order |

The hit test's missing inset meant every click and motion report inside a pane
was computed from a point four pixels left and up of where the pointer was: at
an 8x16 cell, the right half of each column landed in the next column and the
bottom quarter of each row in the next row. The three stacking rules meant a
pane-less surface created alongside a paned one was drawn 768 pixels down --
off a 768-pixel display -- while the hit test still believed it was at the
corner.

Only the live Hibr desktop's shape (one pane-less surface, no panes) made all
three agree, which is why this survived.

## Options considered

1. **Keep the three computations and align them by inspection.** This is what
   the code already claimed to do; they drifted anyway, and the drift is
   invisible until something has both a pane and a pane-less surface.
2. **Store the origin on the surface** and recompute it on topology, session
   and pane changes. Rejected: derived state with three invalidation triggers,
   to replace a calculation that is cheap.
3. **One function, called by both renderers and the hit test.** Chosen.

## Decision

`surface_origin_px` is the only place a surface's origin is computed:

- A surface attached to a pane sits at its pane's cell position plus
  `BLIT_SURFACE_INSET_PX`.
- A surface with no pane sits at the inset, plus the stacked height of the
  pane-less surfaces that precede it on the same output. **Panes do not
  contribute to that stack**: panes partition the display, and a surface with
  no pane is laid out from the display's own corner.
- The order is **surface-array order**, not frame-plan order. A client's frame
  plan is a request about presentation, not a statement about layout, and
  letting it reorder the stack made placement depend on the order a client
  happened to list surfaces in.
- Callers pass their own cell metrics, so a renderer working in scaled device
  pixels and a hit test working in unscaled surface pixels get the same rule.
- **The inset belongs to the surface it surrounds.** A pointer inside it
  reports the nearest edge cell rather than being discarded, so no click is
  lost between the pane edge and the first cell.
- Surfaces in panes are hit-tested before pane-less ones, because a pane-less
  surface starts at the same corner as pane (0,0).

## Consequences

Placement and hit testing cannot disagree without the single function being
wrong, which a gate can hold. A pane-less surface is now reachable by pointer
wherever it is drawn, and two of them no longer overlap on the KMS path.

This is a behaviour change for any client that relied on frame-plan order to
stack pane-less surfaces. Nothing in the tree or in Hibr did: Hibr runs one
pane-less desktop per display.

**Not addressed.** A `font_scale` other than 1 still insets by the same four
device pixels on both paths, so the renderer and the hit test differ by
`4 - 4/scale` pixels there. The inset is not a scaled quantity today and
making it one is a separate change; it is recorded on the geometry issues.

## Evidence

`tools/tests/pointer_geometry_gate.rb`. Two panes are created side by side so
the pane under test has a non-zero origin; a session in the second pane enables
SGR mouse reporting and echoes what it receives with `cat -v`, so the report
the server wrote to the PTY is read back as ordinary cells rather than through
a framebuffer capture (whose own reliability is #45). A click six pixels into
that surface's first cell must report column 1, row 1.

Against the pre-change binary it reports **column 2, row 2**, and the
pane-less surface is drawn off the display instead of at its corner.
