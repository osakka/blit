# ADR 0078: Hibr joins are independent viewports of one desktop

- Status: accepted
- Date: 2026-10-15
- Scope: Hibr `desktop --join`, hold-client PTYs, surface ownership, input,
  and pane presentation
- Supersedes: [ADR 0077](0077-hibr-joins-mirror-one-authoritative-desktop.md)

## Context

Hibr does not create a second desktop when `desktop --join` is invoked. Its
`hold` module attaches another terminal client to the same logical desktop,
with a client-local rectangle and an offset in the union grid. The client
receives the bytes for its own rectangle and sends its input back with that
offset applied.

The previous Blit implementation treated a joined surface as a framebuffer
mirror of the primary. That made the two panes look identical and proved only
that presentation copying worked; it was not Hibr's join contract. It also
prevented a joined client from displaying the wallpaper, menu, windows, and
cursor state that Hibr renders for its own viewport.

## Decision

- There is exactly one Hibr desktop owner for a named `--session`. The
  primary client loads Blit's `console-blit` provider and is the sole API
  author of the desktop's canonical cell and graphics state.
- Every `--join` client is a normal Hibr hold client. The Blit launcher does
  not load a second display provider or open a second provider-owned console
  for it. Hibr's hold client writes its local viewport to that client's PTY.
- Blit forces a joined API session to `surface_mode: terminal` and parses that
  PTY into the joined surface. The joined surface is therefore distinct from
  the primary surface and is never populated by copying the primary's cells.
- A joined surface remains Hibr-owned at the API boundary: direct Blit cell
  mutation returns `409 hibr_join_surface_read_only`. Clients change the
  desktop through Hibr input/control semantics or mutate the primary owner,
  never by introducing a second writer into a joined viewport.
- Pointer and keyboard hit-testing use the physical pane containing the
  event, then canonicalize the event to the primary Hibr owner. The trace
  retains both the pane `target_session` and the Hibr `input_session`.
- Blit owns physical output geometry, pane placement, frame scheduling, and
  diagnostics. Hibr owns the logical union grid, window placement, desktop
  wallpaper, and `--join` offsets. The boundary is a viewport transform, not
  a second desktop implementation.

## Consequences

The primary and each join may have different local pane sizes and font-scale
geometry while remaining one Hibr desktop. Hibr's hold stream is the source
for joined cells and protocol graphics, so wallpaper and window updates are
expected to appear in the correct joined rectangle rather than as a mirror.
The primary remains the input owner, which preserves Hibr's single focus and
window state.

The server must keep the joined PTY alive for the lifetime of the hold client,
drain it at the refresh boundary, and report a failed join if that client
exits. A local server gate can prove distinct PTYs, surface isolation, direct
write rejection, and input routing; only the exact Hibr/QEMU and physical-host
gates can prove Hibr's wallpaper, windows, protocol graphics, and placement.

## Verification

`tools/tests/hibr_join_viewport_gate.rb` creates a primary and a `--join`
fixture, verifies `surface_mode: terminal`, checks that each PTY paints a
different surface, rejects a direct joined-surface write, and verifies
joined-pane pointer routing to the primary input owner. The exact Hibr
multi-display fixture remains a required end-to-end gate; its evidence must
not be replaced by the old mirror test.
