# ADR 0099: Use a ping-pong scene cache for SDL damage composition

- Status: accepted
- Retired by [ADR 0184](0184-sdl-is-removed.md): the SDL code it describes was removed.
- Date: 2026-10-16
- Scope: SDL3 scene composition, rapid terminal updates, and 60 Hz deadline

## Context

The live SDL3/OpenGLES2 host exposed a reproducible failure under rapid
pointer and Hibr surface updates. A controlled API workload produced about
55 pointer updates per second but only about 28 presentations per second and
16 additional missed 60 Hz deadlines in five seconds. The existing policy
disabled incremental content composition on OpenGLES2 because a single
render-target texture did not reliably preserve its contents when rebound.
That policy was pixel-safe but forced a full two-pane redraw for every changed
surface, which is the wrong trade-off during a fast window move.

## Options considered

1. Keep rebuilding every surface on every content update. This preserves
   pixels but exceeds the display budget under sustained Hibr damage.
2. Re-enable incremental drawing into the same render-target texture. This
   improves cadence but reintroduces the measured stale-pixel/corruption
   failure on the live GLES2 backend.
3. Maintain two scene render-target textures, copy the complete last scene
   into the work texture, apply the damage there, and swap only after the
   frame is complete.

## Decision

Choose option 3. `scene_texture` is the immutable presented scene and
`scene_work_texture` is the next-frame target. A content frame copies the
presented texture into the work texture before applying cell, graphics, and
overlay composition. The pointers swap immediately before
`SDL_RenderPresent`, so presentation sees one complete scene. A graphics or
topology change still forces a full rebuild; ordinary cell damage does not.

`BLIT_SDL_SCENE_CACHE=0` remains an explicit diagnostic fallback to the full
rebuild path. `BLIT_SDL_SCENE_CACHE=1` enables the ping-pong path when both
textures are available. No Hibr source or protocol behavior changes.

## Consequences

- SDL backends no longer depend on render-target persistence for incremental
  composition.
- Fast surface updates can repaint only the changed rows while retaining the
  complete wallpaper and other pane content.
- Two output-sized textures are held per SDL output, increasing GPU memory
  usage by two framebuffers.
- Full rebuild, graphics replacement, and topology transitions remain
  deliberately conservative and are still measured separately.

## Verification

- TCC compilation must succeed.
- `tools/run-gates.sh hibr` and the SDL renderer gates must pass.
- The live authenticated control workload must show no new stale pixels and
  must meet the display refresh deadline under rapid surface updates.
- A live screenshot and `render_path` trace must show `incremental-damage`
  for ordinary changed cells and a complete scene after a forced refresh.
