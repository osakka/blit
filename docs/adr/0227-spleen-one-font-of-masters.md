# ADR 0227: Spleen is one text font of five masters, the master picked per size

- Status: accepted
- Date: 2026-11-08
- Scope: `src/blit_font.c` (discovery, the master rule), `src/blit_raster.c`
  (opening a font of masters, drawing a master on its grid),
  `src/blit_server.c` (`GET /v1/fonts`), `openapi/blit-0.1.yaml`, font
  assets and their manifest, packaging, the font gates
- Builds on: ADR 0195 (text fonts are discovered, one glyph stack), ADR 0218
  (the hinting setting)
- Retires: `blit-spleen16`, the fixed 16 px Spleen PSF of ADRs 0145, 0194
  and 0199

## Context

Spleen is shipped as one fixed 16 px PSF, `Blit-Spleen16.psf`: the 8x16
BDF with Terminus filling its gaps. At any other size it's a bitmap scaled
by nearest neighbour, and it is the only Spleen there is.

Upstream Spleen 2.2.0 releases five OTF "masters": 6x12, 8x16, 12x24,
16x32 and 32x64. Each is an outline font of pixel squares, so it is crisp
only at its own cell height and whole multiples of it. The larger ones are
redrawn by the author, not scaled. The owner wants one Spleen, usable at any
size like `hack`, built from those five, and one Spleen only.

What the files are, measured with FreeType 2:
- **Family names** are "Spleen 6x12" to "Spleen 32x64", style Medium,
  monospaced, with a Unicode charmap: each is a valid text font on its own.
- **The em is the cell.** units_per_EM is 1024 (1023 for 12x24), the
  advance half of it, and ascender minus descender 1024. So at an em of H
  pixels a master's cell is H tall, H/2 wide.
- **Not every pixel is a whole number of units.** 8x16, 16x32 and 32x64
  have pixels of 64, 32 and 16 units, exact. 6x12's pixel is 85.33 units
  and 12x24's 42.625, kept in whole units, so their edges are off the grid
  by up to 0.67 and 1.5 units. Rendered unhinted at their own height, 6x12's
  printable ASCII has 36 grey pixels and 12x24's 3,813.
- **Hinting moves glyphs.** FreeType's full and light hinting shift 6 of
  8x16's printable ASCII glyphs a pixel at 48 px, and 4 of 16x32's and
  32x64's at 64 px, though every pixel stays on or off.
- **6x12 is smaller:** 548 mapped scalars against 978, with no Latin
  Extended-A, for one.

## Options considered

How the five files become one font:
1. **Five fonts,** one per master. Rejected: the list grows by four entries
   that each work at one size, and the choice of size becomes the user's.
2. **A pinned manifest** naming the masters and their cell sizes. It works
   for the shipped set only, and the sizes would be a second statement of
   what the fonts already say.
3. **A directory in `text/` is one font,** its files the masters, each
   master's design cell read from the font itself. This is chosen.

Where each master's size comes from:
1. **The family name's `WxH`.** It is a convention of Spleen's names, not
   of fonts.
2. **The font's pixel grid.** The outlines are pixel squares, so the
   design height is the smallest H for which every outline point lies on a
   grid of units_per_EM / H units. This is chosen: it is what makes a master
   a master, and it reads any pixel font drawn as outlines.

## Decision

**A directory in a `text/` folder is one text font, of masters.**
- **Discovery** (`blit_font.c`, still free of FreeType) lists the directory
  once, as one profile. Its path is the directory.
- **Its id and title** are the words its masters' family names share:
  "Spleen 6x12" to "Spleen 32x64" make "Spleen", id `spleen`. That's the
  family-name rule of ADR 0195, applied to the family.
- **Each master** is an entry after it in the list, never a profile. It
  must be a usable text font (ADR 0195) on a pixel grid. One that isn't,
  or that repeats another's design height, is listed in `rejected` with its
  reason. A directory with no usable master, or whose masters share no
  family words, is rejected itself.
- **Each master's design cell** comes from the outline inspector hook, the
  FreeType side (`face_pixel_grid` in `blit_raster.c`):
  - its design height is the smallest H up to 64 for which every point of
    its printable ASCII glyphs, and its advance, lies within a sixteenth of
    a pixel of a grid of units_per_EM / H units;
  - all its segments must be straight;
  - its design width is its advance in those pixels.
  This is the one source of each master's size. Spleen's read as 6x12,
  8x16, 12x24, 16x32 and 32x64.

**One rule picks the master,** `blit_font_master_choose`, the single source
of truth. For a cell height H:
1. the largest master whose design height divides H (crisp);
2. else the largest master whose design height is below H (scaled);
3. else the smallest master.

So 12 is 6x12, 16 is 8x16, 24 and 48 are 12x24, 32 is 16x32 and 64 is
32x64, all crisp; 20 is 8x16, 28 is 12x24 and 40 is 16x32, scaled; 8 to 11
are 6x12, scaled down.

**The rule runs where a text font is opened at a size,** `blit_raster_open`:
- it reads the directory's masters (`blit_font_masters`), picks one for the
  cell height, and opens that file;
- every caller gets the rule this way: boot, a font change, a `size_px` or
  `scale` change, the settings document, and the gates;
- a display's zoom (ADR 0204) opens nothing. It enlarges the composed image
  by nearest neighbour, so a crisp size stays crisp, each pixel a larger
  square at 200, 300 and 400%.

**A master's em is its design cell.**
- It is opened at an em of the cell height, so its cell fills the
  terminal cell, and its baseline is its ascender at that scale.
- The cell width is the boot PSF's aspect, half the height (ADR 0144), the
  same as every master's.
- ADR 0195's fitting loop, which shrinks the em until the rounded-up
  ascent and descent fit, is skipped. It would put 12x24 at 23 px in a
  24 px cell.

**At an exact multiple, a master is drawn from its grid** (`draw_master_glyph`):
- each outline point is put on its design pixel, then scaled by the
  multiple, and the outline rendered;
- every device pixel is then fully on or off: the author's bitmap,
  enlarged, for 6x12 and 12x24 as for the others;
- hinting is not asked, since the grid leaves it nothing to do. The hinting
  setting (ADR 0218) still applies at scaled sizes, which are drawn as any
  outline font.
- This is handled in the raster, the one place fonts are opened and drawn,
  not as a special case elsewhere.

**Coverage:** what a master lacks comes from the glyph stack, as for any
text font. 6x12's missing Latin Extended-A comes from Unifont, fitted to the
cell.

**`GET /v1/fonts`:**
- `spleen` is listed once, with `masters`, the design cells, smallest
  first: `["6x12","8x16","12x24","16x32","32x64"]`;
- `active.master` is the master in use (`"8x16"`), and `active.master_fit`
  is `crisp` or `scaled`; both are null for any other font;
- `rasterization` stays `native-antialiased`, since scaled sizes are.

**The shipped set:** `text/spleen/` holds the five OTFs as released, pinned
in `fonts.source.json` by the tarball's URL and sha256 and each file's
sha256, with the existing `licenses/Spleen-LICENSE.txt`, the tarball's
`LICENSE` byte for byte (BSD-2-Clause).

**Retired:**
- `Blit-Spleen16.psf` and `Blit-Spleen16.source.json`;
- their builder, `tools/fonts/build-retro-font.py`, and gate,
  `retro_font_gate.py`.

The id `blit-spleen16` is retired as ADR 0194 retired ids: a saved selection
naming it falls back to the boot font, and there is no alias. Neither live
host uses it. Terminus 16 is now the only pixel font.

## Consequences

- **One Spleen, at any size from 8 to 64 px.** It's pixel-exact at 12, 16,
  24, 32, 36, 48, 60 and 64 px, and smoothly scaled from the nearest
  smaller master in between.
- **An operator can add a font of masters** by copying a directory of
  pixel-grid outline fonts into `/usr/local/lib/blit/fonts/text`.
- **The package:** the five OTFs (about 0.7 MB) replace the PSF (about
  20 KB).
- **A master at a crisp size ignores the hinting setting.** The setting's
  own description (ADR 0218) says text glyphs follow it; these glyphs have
  no in-between pixels for it to change.
- **A font change to Spleen costs a directory read and five cached
  inspections.** The grid is read once per file, when the list first finds
  it, and remembered by size and mtime like any font.

## Evidence

- **`spleen_master_gate` (new, font suite)**, against a server on the DRM
  mock:
  - `spleen` is listed once with its five masters, its path the directory;
  - no profile is a master, and none is rejected;
  - `blit-spleen16` is not listed and can't be selected;
  - the master and fit at 12, 16, 24, 32, 48 and 64 px (crisp) and 20, 28
    and 40 px (scaled) are as above, at that cell height;
  - at every crisp size the screenshot's first two text rows, and the `A`
    cell alone, hold exactly two colours, foreground and background. At
    the scaled sizes the same rows have in-between colours, which shows the
    check can see coverage;
  - a stored settings document naming `spleen` restarts on `spleen`, and
    the same document naming `blit-spleen16` restarts on the boot font.
  - With the grid path disabled, the gate fails at 24 and 48 px (12x24's
    grey edges). 6x12 at 12 px happened to hint clean.
- **`outline_catalogue_gate` and `custom_icon_gate`** open `text/spleen`
  as one font at every size from 8 to 64 px, so every master and both fits
  are covered. Every Nerd icon, Blit icon and Hibr UI glyph, ⟶ and
  Ж ש ก ა ሀ 漢 字 are drawn and not fallback.
- **`font_assets_gate`** pins every file under `text/`, the subdirectory
  included. **`package_artifact_gate`** checks the five masters are in the
  package, as committed, and that `Blit-Spleen16.psf` and its source
  manifest are not.
- **Gates that named `blit-spleen16`,** now updated:
  - `font_discovery_gate`: the one pixel profile, and the shipped text fonts
    counted with the directory as one;
  - `modern_font_api_gate`: Terminus for the bitmap policy;
  - `host_font_probe_gate`: Terminus and `spleen`;
  - `icon_clip_gate`: Spleen as its masters;
  - `pixel_font_stack_gate`: Terminus only;
  - `package_artifact_gate`.
