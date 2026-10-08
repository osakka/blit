# ADR 0019: Deterministic box-drawing glyph geometry

## Status

Accepted.

## Context

Hibr uses box-drawing characters for panels, menus, borders, and pane
separators. At the bundled 8×16 cell size, the fallback font and the base
Terminus mapping can alias distinct Unicode characters to the same bitmap.
That makes light, heavy, double, rounded, and dashed borders render
incorrectly even when UTF-8 decoding and SDL atlas sampling are correct.

## Decision

The font generator gives the required box-drawing characters dedicated PSF
glyph mappings and emits their 8×16 topology explicitly. Light lines use a
one-pixel stroke, heavy lines a two-pixel stroke, double lines two separated
strokes, dashed lines deterministic alternating segments, and rounded corners
use the light stroke topology at this cell size. The independent font gate
asserts the exact bitmap bytes and therefore detects both rasterizer drift and
PSF aliasing.

This applies to the checked-in runtime font only; it does not claim universal
Unicode shaping or replace Hibr's higher-level bidirectional/Arabic shaping.

## Verification

- `tools/tests/hibr_font_manifest_gate.py` must pass.
- Full terminal conformance and SDL/KMS capture gates must pass after any font
  asset regeneration.
- The production package must contain the regenerated PSF and report its
  version through `/v1/version`.
