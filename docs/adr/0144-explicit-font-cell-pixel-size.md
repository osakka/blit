# ADR 0144: Explicit pixel font size with one canonical sizing policy

- Status: accepted; in use since 0.1.561
- Date: 2026-10-19
- Scope: font API, cell geometry and settings; Blit#107

## Context

The operator wants font size, not screen resolution or desktop scaling,
and both retro and modern multicolour profiles. Existing fonts are bitmap
masks; an 8x16 bitmap enlarged by 1.2 resolves to 10x19 cells. A modern outline
font requires native rasterization; colour glyphs require richer glyph data.
Neither capability follows merely from adding a size field.

## Options considered

1. Keep only scale: compatible, but obscures the actual cell size.
2. Independently mutable scale and pixel size: contradictory persisted state.
3. Extend the existing font resource with one selected size policy.

## Decision

Use option 3. `PUT /v1/fonts` accepts `size_px` (integer 8..64), meaning exact
cell height in device pixels, or legacy `scale` (1.0..3.0 in 0.1 steps), not
both. Duplicate size keys are rejected. A profile-only request preserves the
current size policy. Return `active.sizing`, resolved `active.size_px`, cell
dimensions and a derived source-relative `active.scale`. Under pixel mode
the scale is diagnostic, not constrained to tenths or a second stored value.

Use one shared dimension function for geometry across KMS and SDL. Resolve
width from source-font aspect ratio with nearest-pixel rounding, minimum 1.
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
`settings_persistence_gate.rb` verifies 23px survives restart, only one policy
is saved, and rejected competing controls leave the saved revision unchanged.
TCC build, schema, split/pointer geometry and production damage fixtures pass.
Full regression and physical acceptance are pending at decision recording.

Fresh full run completes 107 pass/7 explicit skips/0 fail, including six existing
offscreen GLES2 gates. Four-PTY 120-second soak: 11,816 snapshots/1,074 resizes.
Installed Hibr 0.99.257/ABI 16 is unchanged during the run. The updated shared
font fixture also passes separately on software and actual offscreen GLES2;
three pinned-header adapter gates pass. The bounded host font-size probe
passes locally on a two-pane software fixture and restores the baseline.
Physical validation remains pending.

Package 561 is reproducible and API-deployed. On 10.54 SDL/GLES2,
`host_font_size.rb` checks 17/19/23px with unchanged two-pane physical regions,
valid grid bounds and captures, then restores baseline scale 1.0/16px. Backend,
bottom login and clean settings are verified. The captured screensaver is
not a font-aesthetic or desktop pixel oracle; those claims remain out of scope.
