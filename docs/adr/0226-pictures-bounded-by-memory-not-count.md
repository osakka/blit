# ADR 0226: pictures bounded by memory, not by count

- Status: proposed
- Date: 2026-11-07
- Scope: graphics images and placements (Kitty, sixel, API, frame channel),
  the GPU and CPU compositors' image textures, `/v1/server` graphics limits,
  XTSMGRAPHICS, frame-channel input routes (ADR 0225)

## Context

A Blit session holds at most 16 pictures (`BLIT_GRAPHICS_MAX_IMAGES`), each
shown in at most 9 places (the default placement and 8 numbered ones,
`BLIT_GRAPHICS_MAX_PLACEMENTS`). Every kind of picture shares the 16: Kitty
and sixel images, API images, and the frame channel's live pictures. When a
17th arrives, Blit frees the oldest complete picture that nothing shows
(`graphics_reclaim_unshown`); a picture on screen is never freed, so with 16
on screen the 17th is refused. Behind the compositor the GPU path keeps 32
image textures per output (`KMS_IMAGE_TEXTURES`), recycled least recently
drawn first.

The numbers come from Blit's terminal beginnings, when a session was one
program in one pane. A Hibr desktop is one Blit session per display, and
every picture on it shares that session: the wallpaper, the browser, films,
game windows (ADR 0225), and, in Files or Pictures, one thumbnail per item.
The owner asked: what if there are 500 thumbnails on a desktop? Today they
cannot all be pictures. They could not all be drawn either: past 32 on one
output, the texture table would re-upload pictures every frame. Hibr gets
by only because it uses few pictures, and every client that wants many must
composite them into one first, which is work each client repeats and the
display server exists to do.

The reference terminal for this protocol bounds pictures by memory: kitty
keeps a storage quota per terminal (320 MB by default) and deletes the
least recently used pictures when it is exceeded, with no count limit.

## Options considered

1. **Raise the constants** (512 pictures, 32 placements, 512 textures).
   Small change. But fixed arrays of the full struct in every one of the 16
   sessions cost memory whether used or not, a count still says nothing
   about bytes (512 4096x4096 RGBA pictures is 32 GiB), and the next desktop
   use finds the next wall.
2. **Clients composite many pictures into one** (Hibr draws a thumbnail
   grid as one image). What happens today. Moves the display server's job
   into every client, re-sends the whole grid when one thumbnail changes,
   and does not help the frame channel or input routes, which are per
   picture.
3. **Bound pictures by memory, with growable tables and a high safety
   ceiling** (kitty's model, adapted to a display server with a GPU).
   Larger change across the server, both compositors and the API; bounds
   what actually runs out.

## Decision (proposed)

Option 3.

- **Tables grow.** A session's pictures and each picture's placements live
  in tables that grow on demand (doubling, freed with the session), not
  fixed arrays. Count ceilings remain only as a guard against a runaway
  program, far above any real desktop: **4096 pictures and 16384
  placements per session**, each refused past the ceiling (the Kitty reply
  `ENOSPC`, an API `409`). Frame-channel input routes (ADR 0225) follow
  pictures, one per picture, with no number of their own.
- **Memory is the limit.** Each session has a **picture budget**: the bytes
  of decoded pixels Blit keeps for it (CPU copies, sixel and Kitty
  scratch, and the GPU textures made from them). Default 256 MiB per
  session, and a **server budget** across sessions of a quarter of physical
  memory, capped at 2 GiB; both are settings. A dmabuf frame (ADR 0223) is
  the producer's memory and counts only its texture bookkeeping. When a new
  picture would exceed a budget, Blit first frees pictures nothing shows,
  least recently shown first, in that session and then across sessions for
  the server budget; if what is left is all on screen, the new picture is
  refused (`ENOSPC`). Nothing on screen is ever freed to make room.
- **The GPU keeps what it draws.** The per-output texture table becomes a
  byte-budgeted cache (default 512 MiB per GPU, a setting), recycled least
  recently drawn first, so every picture drawn in a frame keeps its
  texture and nothing is re-uploaded while it stays on screen. Small
  pictures (both sides at most 256 pixels: thumbnails, icons) share
  atlas textures the way glyphs already do (`KMS_ATLAS_SIZE`), so 500
  thumbnails are a few textures and a few draw calls, not 500. The CPU
  compositor needs no texture cache; its per-frame draw list grows like
  the tables.
- **Published limits say the truth.** `/v1/server` `graphics` reports
  `max_images` and `max_placements` (the ceilings), the budgets and their
  current use, and the existing per-picture bounds (`max_width`,
  `max_height`, `max_bytes`) unchanged. XTSMGRAPHICS answers as now. Each
  session reports its pictures' bytes, frees by reason (`unshown`,
  `budget`), and refusals by reason (`ceiling`, `budget`).
- **Source of truth.** One accounting function charges and credits a
  session's and the server's budgets; every allocation and free of picture
  memory goes through it, so the reported use and the enforced use cannot
  disagree.

## Consequences

- A desktop can show as many pictures as it has memory for: a Files window
  of 500 thumbnails is 500 pictures, drawn from a few atlas textures.
- Memory is bounded by what runs out, per session and across the server;
  an idle session costs nothing for pictures it does not have (today every
  session carries 16 picture structs).
- A program that floods pictures is stopped by the ceiling or the budget,
  and only its own unshown pictures are freed first.
- Larger change: the picture table is walked in about 70 places across the
  server, both compositors, the frame channel and the scene builder; they
  move to an iterator over the session's table. The draw list, today sized
  `BLIT_GRAPHICS_MAX_DRAWS`, grows per frame.
- Behaviour change: a 17th picture with 16 on screen is now accepted;
  freeing an unshown picture is now least recently shown, not oldest
  revision.

## Evidence (planned)

- A gate that creates 1000 small pictures in one session, places 500 on
  screen, and checks that all 500 draw (screenshot), that none is
  re-uploaded on the next frames (`graphics_upload_bytes` flat), and that
  the session's reported bytes match its pictures.
- Budget gates: a session pushed past its budget frees unshown pictures
  least recently shown first and refuses with `ENOSPC` once only visible
  pictures remain; the server budget does the same across two sessions;
  the ceiling refuses picture 4097.
- The existing graphics, frame and input gates unchanged in outcome; full
  set and GLES `renderer`.
- On 10.52 (Intel HD 515) and the mini PC: memory and frame time with 500
  thumbnails on screen, against one composited grid.
