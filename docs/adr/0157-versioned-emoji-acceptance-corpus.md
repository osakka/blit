# ADR0157: versioned emoji acceptance, separate font and terminal evidence

- Date: 2026-10-20
- Status: corpus/audit and colour asset implemented; cluster renderer pending
- Tracking: Blit107/110; execution order in docs/roadmap.md

## Decision

Define "every emoji" against Unicode Emoji18.0's official, hash-pinned
emoji-test.txt, not a scalar cmap count. Preserve its licence and qualification
metadata. RGI is3,963 fully-qualified entries plus9 components. The1,029
minimally-qualified and243 unqualified forms are additional compatibility
cases, not additional RGI emoji. The longest current sequence is10 scalars.
None of this silently changes Hibr's existing Unicode width ABI.

Use the existing single colour fallback file, updated from Noto2.042 to
unmodified Noto Emoji2D2.057 from pinned release v2026-09-24-unicode18_0.
It retains the existing FreeType-supported CBDT/CBLC format, without a second
colour renderer or font selection path. Source, licence and output hashes
remain in the canonical font provenance manifest. Do not deploy this change
merely because source assets pass an audit.

The independent stdlib-only audit calls HarfBuzz's public shaping ABI via
ctypes and reports actual font/library identities. It inspects the final
glyph IDs for every corpus entry, removing default-ignorable placeholders
after shaping. A single nonzero glyph is a necessary coverage check, not proof
of visible colour artwork or correct terminal layout. This is operator-only
diagnostic tooling, not a second production shaping implementation and not
an implicit decision to link HarfBuzz6.0.0 into the server.

## Findings and required next implementation

With audited host HarfBuzz6.0.0, old Noto2.042 misses/splits174 RGI entries
and10 minimally-qualified variants. Noto2.057 produces one nonzero glyph for
all5,244 entries. Shared raster regressions for both outline profiles pass
all57 sizes with the candidate colour asset. Existing BlitRaster still accepts
one scalar, so it DOES NOT acquire cluster support by updating this font.

Next establish one bounded cluster representation shared by PTY ingestion,
API-owned cells, snapshots/scrollback, the glyph cache and all compositors.
Test fragmented UTF-8/escape input, VS15/VS16 selection, keycaps, modifiers,
ZWJ sequences, regional/tag flags, widths/continuations, overwrite, erasure,
wrap, scroll, resize and cache identity. Select/pin the production shaping
dependency with a build/package/ABI decision before integrating it. No Hibr
edits, replacement terminal engine, per-backend shapers or silent width changes.

Closure requires actual cluster colour rasters and end-to-end terminal/output
tests, complete corpus accounting, old Nerd/geometry regressions, API/Control
Panel persistence, backend parity and operator visual acceptance. Missing
glyphs/sequences are failures or explicitly reported limitations, never passes
inferred from a font inventory or a few attractive specimen glyphs.
