# ADR 0058: A surface revision counts accepted writes, not observed changes

- Status: accepted
- Date: 2026-10-10
- Scope: surface revision semantics, Hibr adapter frame coalescing

## Context

Every accepted `PUT /v1/surfaces/{id}/cells` advances `surface->revision` by
one, whatever it wrote. A request that sets a cell to the value it already
held is a revision. A deferred chunk of a multi-request frame is a revision.

Measured on the live host, that made an idle Hibr desktop advance the
revision roughly 700 times a second with nothing visibly changing:

```
+826 revisions/sec        surface_commit_hz = 1.97
+601 revisions/sec        frame_rate_hz     = 1.53
```

The cause is two multipliers on the adapter side, not a fault in the
counter. Hibr composes each frame by blanking the screen and drawing over
it, and does so whether or not anything moved — so the adapter's queue holds
the entire surface, usually with each cell written twice. The 8 KiB server
request bound then splits that one logical frame into hundreds of requests.
The adapter's own trace for one desktop startup:

```
46,365  cell request ... presentation=defer
   109  cell request ... presentation=commit
```

Roughly 425 requests per frame, at about 1.7 frames a second. That is the
700/sec.

The consequence is not cosmetic. The revision is the only identity a client
has for a surface's content. A client cannot use it to detect change,
optimistic concurrency races a counter moving faster than any client can
read and write, and the window in which a framebuffer capture agrees with a
state read (ADR 0048, `X-Blit-Capture-Revisions`) shrinks to nothing.

## Options considered

1. **Advance the revision only on change: compare each written cell to the
   one it replaces and skip the bump when they are equal.** Appealing, and it
   would make the revision mean "the content changed". Rejected because
   "accepted the write" and "the content differs" are different facts and the
   client needs the first. A client sends a write and asks what revision its
   write produced; a server that sometimes answers with the caller's own
   `base_revision` makes the receipt ambiguous between *applied* and *the
   state moved under you*. It also costs a full-cell comparison on the hot
   path to save the client a mistake the client is better placed not to make.

2. **Do not advance the revision on a deferred write; advance once at
   commit.** This is the shape the presentation model suggests: one frame,
   one revision. Rejected because a deferred write is not staged. It mutates
   the cells immediately and defers only presentation, so freezing the
   revision across an open frame would mean two reads at the same revision
   returning different cells. That breaks the revision as a content identity
   far worse than churning it does. Making deferred writes genuinely staged
   would require double-buffering every surface, which is a much larger
   change than this problem justifies.

3. **Coalesce in the adapter: do not send a write that changes nothing.**
   Chosen.

## Decision

**The server's rule is unchanged and is now written down:** one accepted cell
request advances the surface revision by exactly one, regardless of whether
the cells it carried differ from the cells they replaced, and regardless of
`presentation`. The revision counts accepted writes. A client that wants the
revision to track visible change must not send writes that change nothing.

**The adapter is where that is enforced.** `hibr/console-blit.c` keeps a
shadow of the cell content Blit has acknowledged, and reduces each frame to
its delta before sending. Two kinds of write are dropped: one that a later
write in the same frame supersedes, and one whose result the shadow already
holds. An idle desktop therefore sends nothing at all.

Three details are deliberate:

- **Write order is preserved**, not rewritten row-major. Blit writes the
  second half of a double-width glyph itself, so a blank queued before that
  glyph must stay before it or it would overtake and erase the second half.
- **The shadow is recorded per acknowledged chunk**, not per frame, so a
  failure mid-frame leaves it describing exactly what the server holds.
- **The shadow is forgotten on a revision conflict**, which is the signal
  that something other than the adapter wrote the surface — terminal output
  on the session PTY advances the revision too. Cells the current frame
  already dropped are not recovered; Hibr's next redraw re-sends them against
  a forgotten shadow, costing one frame of staleness rather than a second
  copy of the queue.
- **The shadow forgets the cell to the right of any possibly-wide write.**
  Blit resolves glyph width, and a double-width glyph owns the cell to its
  right: writing one marks that cell a continuation, and overwriting one
  clears it. The adapter carries no width table, so it declines to claim
  knowledge of the neighbour of anything that could be wide. Without this, a
  blank replacing a CJK glyph leaves the cell to its right unpainted — Blit
  clears it as a side effect and the frame that should have repainted it is
  dropped as unchanged. A full re-send papered over this; coalescing cannot.

  The test is the UTF-8 lead byte: `0xe0` or above means three or more bytes,
  which means U+0800 or above. `src/blit_unicode_width.h` has no double-width
  range below U+1100, so this covers every wide glyph while excluding the
  two-byte ones. The exclusion matters more than it looks: a Hibr wallpaper is
  32,104 cells of U+00B7, and treating those as possibly wide left one
  invalidated cell at the end of every frame — measured on the live host as
  one request a second against a screen where no cell changed at all.

A frame that fits in one request is sent as `immediate`, never `commit`.
Blit answers `409 frame_not_open` to a `commit` with no preceding `defer`,
and once coalescing works most frames are a single request. Before
coalescing this was unreachable — a full-screen redraw never fit in one
request, which is why the trace above contains no `immediate` at all.

**That makes one server change necessary.** An open frame withholds the
surface from the renderer until a `commit` closes it. Previously only
`commit` closed it, which was sufficient while every frame was chunked and
therefore ended in one. It is not sufficient now: if a frame is abandoned
after its first `defer` — a failed request, a restarted adapter — the frame
stays open, and every subsequent coalesced frame is a single `immediate`
request that would never close it. The surface would stop presenting
permanently. So `immediate` now closes the frame too, which is what
"apply and present this now" already meant.

`frame_open` is reported by `GET /v1/surfaces/{id}/summary`. It was
previously internal, which made the hazard above unobservable from outside
the server; a client that coalesces frames needs to see that its last
request closed the frame.

A cursor move with no cell change is sent as a cell request with an empty
`cells` array. The server already accepts that: a cursor block counts as the
update.

## Consequences

An idle desktop stops advancing the revision, so the revision becomes usable
for what clients need it for: detecting that the content changed.

The revision rate is now proportional to change, not to screen size. A frame
that changes one cell costs one request where it previously cost about 425.
This removes most of the adapter's HTTP traffic, which is the same traffic
that competes with input and render evidence for the server's bounded trace.

A small non-zero idle rate remains correct and expected. A desktop with a
clock in its bar changes a cell a minute, and that should cost a revision.

The adapter now holds a full-surface shadow: `rows * cols * sizeof(cb_cell)`,
about 220 KB for a 128x48 surface. That is small next to the pending queue it
feeds from.

The first frame after `cb_open` is still a full send, because the shadow
starts empty rather than assuming the surface's current content. One
full-screen frame at startup is the honest price of not guessing.

Two known inefficiencies are explicitly left alone, because neither affects
correctness and both are in the transport rather than the semantics: the
768-byte per-cell reserve in the adapter's encoder, which fills only 5,376 of
the 6,144-byte body limit, and the server's 8 KiB request buffer, which is
what forces a large frame to be chunked at all.

## Evidence

`tools/tests/adapter_coalesce_gate.sh` compiles the shipped
`hibr/console-blit.c` into a harness — included rather than linked, so the
provider's statics are callable, with the eleven Hibr symbols it needs
stubbed — and drives it against a real `blit-server`. No Hibr process is
involved, which is the point: the behaviour being gated is the adapter's and
must be assertable without one.

The measurement is the revision delta itself. Because the server advances the
revision exactly once per accepted request, the delta across a flush *is* the
number of requests the adapter sent, so the gate needs no separate counter —
and that identity is the server rule this ADR states.

On a 64x24 surface:

| frame | requests |
| --- | --- |
| first frame, empty shadow | 41 |
| the identical frame again | **0** |
| the same frame, one cell changed | **1** |
| a cursor move, no cell changed | **1** |
| a flush with nothing queued | **0** |
| twenty more identical frames | **0** |
| a chunked frame (shadow forgotten) | 41 |
| the same frame again | **0** |

Silence is asserted not to be amnesia: after the identical frame the gate
reads the surface back and requires the content to still be there, and after
the one-cell change it requires the new cell to have landed and the old one
to be gone.

The wide-glyph rule has its own vector, and it too was checked for teeth. The
gate draws U+4E2D, confirms Blit rendered it two columns wide, then sends a
frame that blanks the screen, and requires every one of the surface's 1,536
cells to carry the pen background. Without the neighbour invalidation that
reads 1,535 — one unpainted cell, beside the glyph, which is the defect
exactly.

Its converse is asserted too: a frame containing forty cells of U+00B7, sent
twice, must cost nothing the second time. With the lead-byte test at `0x80`
instead of `0xe0` that costs one request, which is how the live one-per-second
residual was found.

The `immediate`-closes-the-frame rule is asserted separately and the
assertion is checked to have teeth. The gate opens a frame by hand with a
`defer`, confirms `frame_open` is true, then sends a deliberately one-cell
frame — one request, which is the proof it went out as `immediate` rather
than as a chunked frame ending in `commit` — and requires `frame_open` to be
false again. Against a server built with the previous rule that check fails
and the gate exits 1; against this one it passes.

The gate reports 77 (not applicable) without `BLIT_HIBR_ROOT`, which
`tools/run-gates.sh` defaults to the checkout matching
`packaging/HIBR_VERSION` rather than to any newer development tree: a gate
that proved the adapter against different headers than the package ships
would be proving the wrong artifact. It runs in `tools/run-gates.sh hibr`.

## Live measurement

On `192.168.10.54`, surface 240x135, 32,400 cells.

| | 0.1.419 | 0.1.421 |
| --- | --- | --- |
| desktop startup, full screen drawn | 46,474 requests | **869** |
| idle, nothing changing | 254-323 revisions/sec | **0** |
| one clock digit changing | — | 1 request |

The startup figure is the floor rather than an inefficiency: the shadow starts
empty, so the first frame is the whole surface, and 32,400 cells at roughly 37
per request is about 875. Everything above that was the re-sending.

Idle measured as four consecutive fifteen-second windows at revision 872,
872, 872, 873. The last window is the result, not a blemish: over a following
seventy seconds the revision advanced exactly once, and a cell-by-cell diff of
the two snapshots found exactly one cell changed — `(224,0)`, `'3'` to `'4'`,
the minute digit in the bar. One cell changed, one request, one revision,
which is what "proportionate to what changed" has to mean.

The framebuffer agrees with that state. Sampling the centre of all 32,026
visible cells, applying `surface_inset_px`, finds `#0d1b2a` wallpaper on
31,745 of them and `#1b3a5c` on 231 against the 234 bar cells the state
reports.

An intermediate reading is worth recording because it is the shape of a
correct result. Before the lead-byte test was tightened to `0xe0`, the idle
rate was one revision a second with zero cells changed between samples — the
re-send of the one neighbour each frame invalidated. One a second is a
plausible-looking number for a desktop with a clock, and it would have been
easy to accept. Diffing two snapshots and finding no changed cells is what
showed it was not the clock. The measurement above is the same diff finding
that it now is.

A separate observation, not caused by this change: a desktop reattached with
`--resume` comes back blank and stays blank. The surface held 32,400 cells of
`" "` with no style at a revision of 10. Ten accepted requests cannot carry
32,400 cells — the body bound is about 43 of them — so that content did not
come from the adapter at all; it arrived through the session PTY and the
terminal engine, which advances the revision once per read batch. A fresh
`--session` on the same build draws correctly, in 887 requests, with styled
wallpaper and bar. So this is the reattach path putting a terminal stream on
an owned surface rather than the owner repainting it, which is the collision
[ADR 0057](0057-owned-surfaces-are-not-interactive-terminals.md) describes
from the other direction. It needs its own ticket and is not this one.
