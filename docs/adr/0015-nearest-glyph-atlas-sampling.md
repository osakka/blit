# ADR 0015: nearest sampling for the SDL glyph atlas

- Status: accepted
- Date: 2026-10-08
- Scope: SDL3 terminal glyph presentation

## Context

The production font is an 8×16 bitmap atlas. Glyphs are packed directly next
to one another and are often scaled into the display-derived cell geometry.
SDL3's default texture scale mode is linear. At atlas boundaries that permits
the sampler to blend neighbouring glyph pixels into the requested glyph,
which presents as malformed box drawing, punctuation, or Hibr desktop icons.

## Decision

Set the glyph atlas texture to `SDL_SCALEMODE_NEAREST` immediately after it is
created. If the renderer cannot provide that mode, discard the atlas and use
the existing per-pixel fallback path rather than silently rendering filtered
terminal glyphs.

This is a sampling policy only. The PSF font, Unicode mapping, terminal cell
model, and direct-KMS renderer remain the canonical sources of glyph data.

## Verification

The TCC build must link the SDL3 scale-mode API, and the SDL glyph filter gate
must confirm that a texture can be set and read back as nearest. The existing
terminal conformance, Hibr font manifest, SDL screenshot, and physical Hibr
presentation gates remain required; this ADR does not claim physical
multi-monitor or high-refresh hardware evidence.
