# ADR 0060: Kitty placements, source rectangles and delete targets

- Status: accepted
- Date: 2026-10-10
- Scope: Kitty graphics parsing, the graphics model, both renderers, the
  graphics diagnostics contract

## Context

[ADR 0005](0005-kitty-graphics-first.md) made Kitty graphics the first
protocol. The implementation to date stored one placement per image and read
`x`/`y` as a destination offset. Three consequences:

- `x y w h` are the Kitty **source rectangle** (a crop of the image), not a
  position. A program that cropped a sprite sheet drew the whole sheet at an
  offset.
- `a=p` with `p=` ids, and the cell-based delete targets, were ignored, so a
  program that moved or removed one placement removed or kept everything.
- Keys Blit does not implement (`t=f`, `I`, `U`, `P`, `Q`, `H`, `V`, `S`, `O`)
  were silently accepted, so a program could not tell its image never arrived.

## Options considered

1. Keep one placement per image and document the gaps. Leaves cropped output
   wrong on screen, which is the failure users see.
2. Implement the whole protocol including file/shared-memory transmission,
   unicode placeholders and animation. Large, and file and shared-memory
   media let a client name a path the server then reads.
3. Implement the placement model and the delete targets completely, and
   refuse what is not implemented with a reason. Chosen.

## Decision

- `x y w h` select the source rectangle; width and height clamp to the image,
  an origin outside it is refused (`placement-bounds`). `X Y` are the pixel
  offset within the cursor cell and must be smaller than the cell.
- The destination is the cursor cell. Unless `C=1`, the cursor then moves right
  by the placement's columns and down by its rows.
- An image has one default placement (no `p`) that is moved by each display,
  which preserves the existing Hibr behaviour, plus up to 8 numbered
  placements. Re-displaying the same `(i,p)` replaces it. A ninth is refused
  (`placement-store-full`).
- `a=d` implements targets `a i c p q x y z` and uppercase variants. Lowercase
  removes placements and keeps the data; uppercase also frees data once no
  placement remains. The default target is `a`. Any other target is refused
  (`unsupported-delete-target`) and deletes nothing.
- The keys `I P Q H V S O`, `t` other than `d`, and `U` other than 0 are
  refused (`unsupported-key`) and create no image. A refusal sets
  `graphics_diagnostics.last_error` and increments `rejected`.
- Both renderers draw from one flattened, z-sorted draw list
  (`blit_graphics_collect`), so SDL and KMS cannot disagree on placement.
- `GET` session state reports resolved placement size and the `placements`
  array with each `source` rectangle.

Hibr's emitter sends only `a=T`, `a=d,d=I` and `a=d,d=A` with no `x`/`y`, so the
meaning change does not affect it.

## Consequences

Cropping, repositioning and selective deletion now behave as the protocol
says. Unsupported media (`t=f/t/s`) and animation are an explicit, observable
refusal rather than a silent no-op. Behaviour past the surface edge is undefined by the
protocol and is not specified here; renderers clip to the surface.

## Evidence

- `tools/tests/kitty_placement_gate.rb` renders cropped, offset and numbered
  placements and asserts framebuffer pixels. Against the 0.1.421 server it
  fails the crop, offset and placement assertions.
- `tools/tests/terminal_conformance.rb` vector `kitty-placement-keys` asserts
  the source rectangle, bounds refusals, every unsupported key, numbered
  placements, each delete target and the keep-versus-free distinction. Against
  the previous release it fails because `placements` is absent.
- KMS drawing shares the draw list; confirmed on the live host (see the
  release manifest).
