# ADR 0070: the scene cache is used only where render targets are known to persist

- Status: superseded by ADR 0099
- Date: 2026-10-13
- Scope: the SDL presentation path — incremental damage, the cached scene
  texture, and what the gate suite can prove about either

> Superseded for SDL composition by [ADR 0099](0099-ping-pong-scene-cache-for-sdl-damage.md).

## Context

The SDL renderer kept the composed frame in a render-target texture and
repainted only the rows a surface reported as damaged. Everything outside
those rows was left alone, on the understanding that the texture still held
the previous frame.

That understanding is a precondition, not a property of SDL: **the backend
must preserve a render target's contents between passes.** It was never
written down and never tested, because on the renderer the gates run —
`software`, under the dummy video driver — it is true.

On the live host it is false. The host presents through `opengles2`, and a
screensaver left a growing trail of itself: cells painted that the
authoritative cell snapshot said were blank, climbing and then collapsing
whenever something forced a whole frame.

```text
stale 432 → 469 → 551 → 605 → 740 → 44   (collapse on a full redraw)
missing  16    16    36    44    56     5
```

`stale` greatly exceeding `missing` is the signature of residue; a sampling
artefact produces the two in roughly equal numbers.

Three defects were found and fixed before this one, each real and each
insufficient: the incremental clear covered the damage rectangle's columns
while the repaint covered whole rows (0.1.457), damage was discarded for
surfaces that were never painted (0.1.458), and clearing a wide glyph's
partner cell marked no damage (0.1.460). The symptom survived all three.

What finally located it was not another reading of the code. Five
reproductions were built — a batch-written sprite, a 12×8 host probe, one at
the screensaver's exact 240×135, one writing a cell per request as the live
trace showed the owner does, and one with a sparse polyline over a large
damage region. **Every one passed, and every one ran against the software
renderer.** The consistency was the finding: the fault was in the environment
the gates cannot enter, not in a shape they had not tried.

## Options considered

1. **Keep the cache and repair the incremental path.** Attractive because the
   cache is what makes pointer motion cheap. Rejected for now because there is
   nothing to repair: the path is correct given its precondition, and the
   precondition belongs to the driver. A fix would have to revalidate the
   target's contents, which costs at least what redrawing costs.
2. **Disable the cache everywhere.** Simple and uniformly correct, but it
   throws away a measured optimisation on the one backend where it is proven,
   and it would silently delete the behaviour that
   `external_pointer_composition_gate` exists to assert.
3. **Bound the damage with a periodic full redraw.** Cheap to write and
   dishonest: it converts a correctness bug into a slower correctness bug and
   leaves the precondition unexamined.
4. **Use the cache only where the precondition is known to hold.** Correct by
   default, keeps the optimisation and its gates on the backend that proves
   it, and makes the untested case the conservative one.

## Decision

Option 4. The cached scene is enabled when the active SDL renderer is
`software`, and every other backend composes a whole frame.

`BLIT_SDL_SCENE_CACHE` overrides the default **in both directions** — `1`
forces the cache on, `0` forces it off. That is deliberate: the default
encodes a measurement taken on one machine, and the next person to doubt it
should be able to repeat the measurement rather than take this record's word.

The variable is a diagnostic hook in the family of `BLIT_SDL_GLYPH_BATCH`, not
a setting. It is not in the OpenAPI contract and ADR 0067 does not persist it.

## Consequences

Every non-software backend now draws each frame whole. Measured on the live
host before the default was chosen: **3.3 ms to render and 0.6 ms to present
against a 16 ms budget, with no missed deadlines.** That is the price of a
screen that matches its own cell data, and on this hardware it is cheap.

It is not free everywhere. A slower GPU, a larger panel, or a much denser
grid could make a whole frame expensive, and nothing in the suite measures
that cost — the gates run the backend this decision exempts. Issue #80 tracks
the missing budget and the conditions under which the cache could be earned
back.

The gates are unaffected: they run `software`, so the cache stays on where it
is proven and `external_pointer_composition_gate`'s `pointer_cache_hits`
still exercises it. That is also the uncomfortable part — **this entire class
of fault is invisible to the suite**, and the suite's agreement that the
renderer is correct was only ever true of the renderer it can run. Issue #79
tracks a gate host with a GPU.

One consequence was visible to the operator immediately and is worth stating
plainly: the cache had been *hiding* upstream behaviour. When the Hibr desktop
removed its own menu bar, the stale pixels kept it on screen. With the
renderer honest, it disappears when the desktop removes it. Making the
renderer correct made an upstream problem newly visible, which is an
improvement that does not feel like one.

## Evidence

A/B on the physical host, same machine and same screensaver, with the
workload read from `GET /v1/surfaces/{id}/cells` on both sides of the capture
so that a surface drawing between the two reads is visible as `churn` rather
than as a false result:

```text
cache on     stale 170 of 329 cells drawn; 740 earlier under a busier saver
cache off    stale 0 across six samples, on a harder workload --
             10875 cells drawn and 1015 changing between the bracketing reads
```

After deployment, with no environment override set, the host reports
`renderer = opengles2`, `render paths = {'full-redraw': 6}` and
`pointer_cache_hits = 0`, confirming the default and not an override is doing
the work.

`tools/probes/host_cell_pixel_agreement.rb` is the instrument. It carries
three corrections that each produced a confident wrong answer first, and they
are recorded in the file as the reasons it is shaped the way it is: sample
every pixel of a cell rather than one, bracket the capture with two cell reads,
and exclude the software pointer. A fourth — that `?cached=1` shows the panel
while a fresh capture forces a render that repairs the state — is why the
cached form is the one the probe uses.

Local suite at the time of the decision: 63 pass, 5 skip, 0 fail, unchanged by
the default because the gates run `software`.
