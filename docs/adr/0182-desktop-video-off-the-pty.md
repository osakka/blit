# ADR 0182: desktop video stays on the PTY

- Status: accepted (the owner's decision, 2026-10-25). Proposed earlier the
  same day as "desktop video off the PTY"; the file keeps its name so links
  hold.
- Date: 2026-10-25
- Scope: Hibr desktop image transport, held sessions, ADR 0066, ADR 0166

## Context

The packaged desktop runs Hibr's native terminal provider
(`hibr/blit-session.hibr`). Hibr writes the desktop, pictures included, to
its PTY as Kitty `f=24` base64 in 4 KB `q=2` chunks. `hold` keeps that state
and relays it, and Blit parses it. This one stream is what keeps Hibr's
union grid, wallpaper, graphics and input semantics under one owner, and
what `hold` replays to every client that attaches: Blit, kitty, ssh.

YouTube looked choppy. The first measurements (0.2.18: about 13 fps,
12 MB/s, no saturated core) suggested the transport's cost per byte was the
limit. The ADR proposed four options:

1. Send large pictures over Blit's authenticated pixel-frame stream (ADR
   0166) when Hibr runs under Blit, and keep the cells on the PTY.
2. Switch the packaged desktop to the `api` provider.
3. Keep the PTY and cut the cost per byte.
4. Raise the frame rate where it is set.

Kitty `t=s` (shared memory over the PTY) had already been prototyped and
withdrawn under ADR 0066.

## Measurements (10.54, Blit 0.2.20, 2026-10-25)

YouTube in a standard-size window (147×39 cells of 12×24 px), Hibr Frames
For Output = 0. Frames per second are new Kitty image ids per second; Blit
presented the display once per new frame.

| Hibr Pictures detail | Frame sent | Frames/s | Base64 in | `hold` | desktop | Blit |
|---|---|---|---|---|---|---|
| Automatic | 1764×936 RGB, 4.95 MB | 6.2 | ~40 MB/s | 67% | 37% | 28% |
| Half | 882×468 RGB, 1.24 MB | 7.5 | ~12 MB/s | 20% | 33% | 11% |

Cutting the bytes by four moved the frame rate from 6.2 to 7.5 fps, and no
process was CPU-bound. **The transport is not the limit. Frame production
is.**

- The WebKit engine (hibr-browser) has no media `tap`, so YouTube cannot use
  its media player, which decodes at 24 fps and the video's own resolution.
  Instead it screenshots the whole page at the window's pixel size, which is
  why the 480p setting changed nothing.
- That fallback wakes every 250 ms.
- Detail "Automatic" halves the wallpaper but not video.

## Decision

**Option 3.** Desktop video stays on the one PTY stream. Blit's job is to be
the fastest terminal for that stream; ingest is already 3.1× cheaper since
0.2.18.

**Option 1 is rejected.**

- It is a parallel transport by construction: pictures on one channel,
  cells on another, and an ordering contract to keep them in step.
- Pictures on the pixel-frame stream never pass through `hold`, so a
  reattached or ssh client would get the cells without the pictures, unless
  `hold` learned a second channel as well.
- It would also make the desktop behave differently under Blit than on any
  other terminal. Today every terminal gets the same stream and degrades the
  same way (half blocks where no pictures are drawn).

**Option 2 is rejected.** It reverses the reasons the terminal provider was
chosen: a single state owner, the union grid, and `hold`.

**Option 4 is done, as tickets on its owners:**

- hibr-browser #2: implement `tap`/`tapseek`/`take` in the WebKit engine.
- hibr #385: pace the fallback at the frame interval, and halve video under
  Automatic.
- hibr #382: covered window pictures are dropped. It is not a frame-rate
  issue, but was found in the same investigation.

## Consequences

- The desktop renders the same everywhere: on Blit, on kitty, and through
  ssh or `hold`, including reattach.
- Projected load once `tap` lands, from today's cost per byte (`hold` about
  1.7% of a core per MB/s, Blit about 0.9%):
  - 480p at 24 fps with #385's halving: about 10 MB/s, `hold` about 20%.
  - Without halving: about 40 MB/s, `hold` about 67%.
  - Neither saturates a core.
- **Revisit trigger:** re-measure `hold` and Blit at 24 fps after `tap`
  lands. If `hold` saturates, the fix stays inside the one path: `hold`
  relaying Kitty bodies without its per-byte parse. Not a second transport.

## Evidence

- Host measurements above.
- The 0.2.16–0.2.18 entries in `docs/verification-status.md`.
- `hibr/blit-session.hibr` (provider selection).
- hibr-browser `mod/webkit.c:348-353` (no tap).
- hibr `programs/desktop/apps/Internet/youtube.hibr:794-800` (250 ms timer).
- hibr `mods/console/image.c` (halving under Automatic).
- ADR 0066; ADR 0166.
