# ADR 0144: Explicit pixel font size with one canonical sizing policy

- Status: implemented; focused and full regression pass, physical validation pending
- Date: 2026-10-19
- Scope: font API, cell geometry and settings; Blit#107

## Context

The operator wants font size, not screen resolution or desktop scaling,
and both retro and modern multicolour profiles. Existing fonts are bitmap
masks; an8x16 bitmap enlarged by1.2 resolves to10x19 cells. A modern outline
font requires native rasterization; colour glyphs require richer glyph data.
Neither capability follows merely from adding a size field.

## Options considered

1. Keep only scale: compatible, but obscures the actual cell size.
2. Independently mutable scale and pixel size: contradictory persisted state.
3. Extend the existing font resource with one selected size policy.

## Decision

Use option3. `PUT /v1/fonts` accepts `size_px` (integer8..64), meaning exact
cell height in device pixels, or legacy `scale` (1.0..3.0 in0.1 steps), not
both. Duplicate size keys are rejected. A profile-only request preserves the
current size policy. Return `active.sizing`, resolved `active.size_px`, cell
dimensions and a derived source-relative `active.scale`. Under pixel mode
the scale is diagnostic, not constrained to tenths or a second stored value.

Use one shared dimension function for geometry across KMS and SDL. Resolve
width from source-font aspect ratio with nearest-pixel rounding, minimum1.
For bitmap profiles this is sampling at that cell size, not outline
rasterization. Display mode is unchanged. Size remains global in this stage.

Persist exactly one of `size_px` or `scale` in canonical font settings.
Share the size-policy parser between live API and settings application;
reject ambiguous/invalid stored requests before changing the durable file.
Old documents and scale clients remain valid; a legacy scale write explicitly
switches back to scale mode. Existing boot defaults remain scale-based.

## Consequences

Exact integer heights no longer depend on a tenth-step approximation.
Legacy `active.scale` consumers must not assume it is always a valid scale
write: pixel mode can derive other values. Clients should inspect `sizing`
and write their chosen control only. Modern rasterization, antialiasing,
colour glyphs, emoji sequences and new profiles remain later steps under
the font evolution plan. This ADR does not adopt a font dependency.

## Evidence

The new `font_api_gate.rb` assertion fails on the old binary with
`font_configuration_empty`. It passes after implementation, checking exact
8,16,17,19,23,32,48,64 heights and actual solid-glyph pixels, invalid and
duplicate controls, profile-only retention and return to legacy scale.
`settings_persistence_gate.rb` verifies23px survives restart, only one policy
is saved, and rejected competing controls leave the saved revision unchanged.
TCC build, schema, split/pointer geometry and production damage fixtures pass.
Full regression and physical acceptance are pending at decision recording.

Fresh full run completes107pass/7explicit skips/0fail, including six existing
offscreen GLES2 gates. Four-PTY120-second soak:11,816 snapshots/1,074 resizes.
Installed Hibr0.99.257/ABI16 is unchanged during the run. The updated shared
font fixture also passes separately on software and actual offscreen GLES2;
three pinned-header adapter gates pass. The bounded host font-size probe
passes locally on a two-pane software fixture and restores the baseline.
Physical validation remains pending.
