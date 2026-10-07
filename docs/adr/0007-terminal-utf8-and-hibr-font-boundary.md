# ADR 0007: preserve UTF-8 across PTY reads and ship Hibr glyph coverage

- Status: accepted
- Date: 2026-10-07
- Scope: terminal input boundary and native 8×16 text renderer

## Context

PTY reads are arbitrary byte chunks. A multi-byte UTF-8 scalar can therefore
be split between reads. Treating an incomplete but valid prefix as malformed
text produces visible `?` cells in Hibr's desktop, even though Hibr emitted
valid UTF-8. The original bundled Terminus PSF also lacked several Unicode
symbols used by Hibr's unicode desktop and wallpaper.

## Options considered

1. Replace incomplete or unsupported text with `?`. This is simple but loses
   terminal data and makes a correct Hibr session visibly corrupt.
2. Require every deployment to install an external Unicode font. This makes
   rendering environment-dependent and breaks the turnkey package boundary.
3. Preserve incomplete UTF-8 at the PTY boundary and ship one self-contained
   8×16 PSF2 font that retains Terminus and adds only the missing Hibr glyphs.

## Decision

Choose option 3. The terminal session stores a valid UTF-8 prefix until the
next PTY read, then decodes the scalar once complete. Invalid sequences still
follow the explicit replacement policy. The checked-in
`Blit-Hibr-Terminus16.psf` preserves the bundled Terminus glyphs and adds the
missing non-Nerd Hibr desktop glyphs using the documented DejaVu build-time
source. Runtime installation has no font-tool or external font dependency.

The canonical cell retains the Unicode codepoint; the same codepoint is used
by the API snapshot, SDL renderer, direct KMS renderer, and stream events.
There is no renderer-specific glyph or terminal parser implementation.

## Consequences

- PTY chunk boundaries no longer change terminal content.
- Hibr's Unicode desktop can render deterministically at the existing 8×16
  geometry on dummy, KMS, and QEMU fixtures.
- Nerd-font private-use glyphs remain an explicit separate Hibr glyph set and
  are not falsely claimed by the default font.
- The font generator and required-glyph manifest are build-time sources; the
  PSF asset is the runtime artifact and is validated before packaging.

## Evidence

The TCC-built runtime's fragmented-UTF-8 conformance vector passes. The same
runtime, with the Hibr desktop launched through the API, reports zero visible
question-mark cells after the full initial redraw. The two-output VKMS capture
also loads the merged font and passes its pixel oracle.
