# ADR 0182: desktop video off the PTY (proposed)

- Status: proposed; needs the owner's choice of option before any code
- Date: 2026-10-25
- Scope: Hibr desktop image transport, held sessions, ADR 0066, ADR 0166

## Context

ADR 0181 moved composition to the GPU; on 10.54 every frame is
GPU-composed (1.5–1.7 ms GPU average, 0 CPU fallbacks). The owner still sees
choppy YouTube. Measured during playback (0.2.18, `/v1/diagnostics/processes`):
WebKit 27% of a core, Hibr desktop 44% (one thread), `hold` 22%, Blit 12% at
12 MB/s of Kitty base64, about 13 video frames a second; no process is
saturated.

The packaged desktop deliberately runs Hibr's native terminal provider
(`hibr/blit-session.hibr`): Hibr writes the desktop, images included, to its
PTY as Kitty `f=24` base64 in 4 KB `q=2` chunks; `hold` relays it; Blit
parses it. That is the only path that keeps Hibr's union grid, wallpaper,
graphics and input semantics with one desktop state owner. Every video frame
is therefore base64-encoded, copied through two PTY hops and parsed three
times. The authenticated binary route ADR 0066 names for large images, the
WebSocket pixel frame (ADR 0166), is used only by the `api` provider
(`console-blit`), which the packaged desktop never selects:
`pixel_frames_accepted` is 0 on the host.

Kitty `t=s` (shared memory over the PTY) was prototyped and withdrawn: it
contradicts ADR 0066 (an escape is unauthenticated, Blit may run as root, the
mandated unlink lets terminal output destroy the user's other objects).

## Options

1. **Hibr's image module sends large images over Blit's pixel-frame stream
   when it runs under Blit** (`BLIT_SERVER_PORT`/surface identity are already
   exported to the PTY), keeping cells on the PTY. One authenticated binary
   copy per frame, no base64. Needs a Hibr change and an ordering contract
   between the PTY's cells and the stream's image (placement, z, deletes,
   `hold` reattach replay).
2. **Switch the packaged desktop to the `api` provider.** Reverses the
   reasons the terminal provider was chosen (single state owner, union grid,
   `hold`); large.
3. **Keep the PTY, cut cost per byte.** `o=z` for video costs more CPU than
   it saves; Blit's decode is already 3.1x cheaper (0.2.18). Bounded gain.
4. **Raise the frame rate where it is set.** Find why hibr-wpe → desktop
   delivers ~13 fps with no saturated core (a timer, `DT_TERMMS`-like pacing,
   or capture cadence). Hibr-side; independent of transport.

## Recommendation

4 first (cheap, and it is what the owner sees), then 1 for the load. Both are
Hibr-side; Blit's part of 1 is a documented ordering contract and a gate that
a pixel-frame image and PTY cells agree.

## Evidence

Host measurements above (0.2.16–0.2.18, docs/verification-status.md);
`hibr/blit-session.hibr` provider selection; ADR 0066; ADR 0166.
