# ADR 0241: Arabic is shaped as words and drawn as slices

- Status: accepted, landing in steps (the face first; the word path is on
  only once the renderers and their gates are in)
- Date: 2026-11-20
- Scope: `src/blit_raster.c` (a word cache beside the glyph cache, the
  word slice lookup), `src/blit_cell_word.h` (new: the key, the run
  classifier, the damage widening), `src/blit_arabic_forms.h` (new,
  generated), the raster hooks in `src/blit_font.h`, `draw_view` and the
  block cursor in `src/blit_kms.c`, `kms_sprite` and `kms_gles_view` in
  `src/blit_kms_gles.c`, the font settings in `src/blit_server.c`;
  `assets/fonts/NotoNaskhArabic-Regular.ttf`;
  `tools/tests/arabic_word_slice_gate.sh`. Supersedes the per-cell drawing
  of ADR 0174 and item 7 of ADR 0195; amends ADR 0240's Arabic exception.

## Context

The owner wants Blit's Arabic to look like a browser's. Today each cell is
one letter drawn on its own: Hibr sends visual order, one presentation form
per cell (lam-alef is one cell), marks on the cell, and Blit draws that form
from Blit Arabic Modern, a monospace face derived from GNU FreeMono (ADR
0174, 0195). Every letter gets the same width, so a word looks typed, not
written: the joins fall where two cells meet, wide letters are squeezed and
narrow ones padded.

A browser shapes the whole word in a proportional Naskh. An offline proof
did that for the Hibr specimens (HarfBuzz, Noto Naskh Arabic, alef top at
the text font's cap height, on the text baseline, each word fitted into the
cells Hibr gave it), and the owner approved the look.

Two things in Blit are built on one cell:

- The GPU compositor caches one sprite per cell, keyed by the cell's own
  content (`KmsSprite`: the scalar, up to 16 marks, size, style, font).
- `draw_view` and `kms_gles_view` draw cell by cell inside a damage
  rectangle, and the block cursor redraws the cells whose ink enters the
  cursor cell.

A shaped word is not the sum of its cells: change one letter and the other
letters' places, and maybe the word's scale, move. Blit already draws ink
that spans cells: Blit's icons that span columns (ADR 0197), where each
column draws its slice of the whole. Words do the same.

The key cannot be a `BlitCellCluster`: it holds `BLIT_CLUSTER_MAX_SCALARS`,
17 scalars (a base and 16 marks), for one cell. A word is many cells.

## Decision

**What a word is.** The renderer reads each row it draws once, left to
right, and cuts it into runs:

- *Letters:* adjacent cells whose base is an Arabic presentation form
  (U+FB50-FDFF, U+FE71-FEFC, from the pinned Unicode decomposition), the
  tatweel U+0640, or a mark (a marks-only cell: its marks go with the
  letter logically before it). Shaped right to left.
- *Digits:* adjacent Arabic-Indic digits (U+0660-0669, U+06F0-06F9) and the
  separators U+066B and U+066C. Shaped left to right, as Hibr sent them.
- *One cell:* any other Arabic cell (punctuation, a nominal letter U+06xx):
  a run of one.

A run ends at any other cell: a space, Latin, a wide cell or its
continuation, the view's edge. It does not end where colour, bold, italic,
inverse, underline or conceal change: those belong to the cell and are
applied to the cell's slice. A run longer than 32 cells is cut into pieces
of 32 counted from its right end, the word's logical start, so typing at
its left end does not move the pieces.

**The key** is the run itself, `BlitCellWord`: its kind, its cell count
(1-32) and its cells as `BlitCellCluster`s, in visual order, compared in
full (at most 32 x 17 = 544 scalars). A cache entry keeps its own copy of
the cells, sized to the run. A cell's index in the word is not part of any
cache key: a slice is a view into the word, not a second cached thing. The
GPU's sprite for a word is keyed by the word's serial, a 64-bit number the
raster gives each word canvas it draws and never reuses, so a sprite can
never stand for a different word.

**Letters back from cells.** Reverse the run's cells (visual to logical);
each presentation form becomes its letters by its compatibility
decomposition (lam-alef becomes lam and alef); the cell's marks follow its
letters. Hibr's forms are kept, not re-decided, with two invisible joiners
that HarfBuzz honours and that have no ink or width:

- where a cell's form joins a neighbour the run does not have (a word cut
  at the view's edge, or a letter before a Latin cell), a ZWJ stands in for
  that neighbour;
- where Hibr left two letters apart that would otherwise join (the first
  can join forward, the second backward), a ZWNJ keeps them apart.

A whole word gets neither: its letters are exactly the logical text.

**A scalar the Arabic face lacks.** If shaping the run gives any glyph 0,
the whole run is drawn cell by cell on today's path (`draw_cell_glyphs`:
the cluster, else base and marks from the glyph stack), never a word with
a hole in it.

**Shaping and the fit.** The word is shaped with HarfBuzz in the Arabic
face, unhinted, at the size that puts the alef's top at the text font's cap
height (`raster->cap`), on the text baseline. It must fit its N cells by
its advance and by its ink: if either is wider than N cells, the word is
shrunk as a whole to fit, never stretched; its advance is centred, then
moved only as far as needed to keep its ink inside. Its ink never leaves
its cells sideways. Above and below it may take one cell
(`raster_ink_top` and `raster_ink_bottom` stay 1).

**The canvas and the slices.** The word is drawn once into a coverage
canvas N cells wide, cropped to its ink rows. Cell i's slice is columns
[i x cw, (i+1) x cw) of that canvas at its full height: a `BlitRasterGlyph`
whose pixels point into the canvas, `width` one cell, `stride` the word's,
`ink_offset_x` 0, `ink_offset_y` the canvas's top. Every pixel of the word
belongs to exactly one slice, so slices drawn independently, each into its
own cell, make the whole word with nothing blended twice.

**Bold is made on the word**, as a variant of the canvas made when first
asked for: the renderer's one-pixel overstrike (ADR 0240) applied to the
whole canvas, so a slice's first column gets the previous cell's ink and
nothing passes the word's last column. A bold cell draws its slice from
that variant, once; the renderer adds no overstrike to word slices. A word
can be bold in some cells only.

**Italic words are drawn upright.** A Latin slant has no meaning for Naskh,
and leaning each slice by itself would break the word apart at every cell.

**The renderer.** `draw_view`, `kms_gles_view` and the block cursor ask
for an Arabic cell's ink through one helper that knows the row, not just
the cell, and find its run in the row's segmentation, made once per row per
frame. The CPU draws the slice with `draw_cell_raster`, tinted by the
cell's own foreground. The GPU keeps one sprite per word canvas variant in
a word atlas and draws each cell as a quad of its cell's sub-rectangle of
that sprite.

**Damage.** A cell's slice depends on its whole run, so a damaged rectangle
is widened, on its rows, to every run that contains or touches a damaged
column (one column either side: a cell that stopped being a letter
shortens the words beside it). The widening reads the new cells only,
which is enough because an undamaged cell did not change. The widened
rectangle then goes through `blit_ink_region` as before. The Arabic
companion's ink margins (2 cells left, 3 right, 1 above and below) stay:
a run the word face refuses is drawn per cell with the companion, whose ink
overhangs. With a word face and no companion (a pixel font), the margins
are a row above and below.

**The cursor** marks its cell: the block cursor redraws, in the cursor's
background, the part of the word inside the cursor cell (and any overhang
from the rows above and below), clipped to the cell, as for any overhanging
ink. It does not move to where the letter's ink is.

**Caches and their limits.**

- *Raster:* words have their own cache beside the glyph cache: 8192
  entries (2048 sets of 4, LRU within a set, full-key compare), 32 MiB of
  canvases, apart from the glyph cache's 4 MiB so a screen of Arabic
  cannot push the text font out. A canvas is 0.69-0.82 of its cells' area
  over the specimens (1.40 for the worst word): a full 3840x2160 screen of
  Arabic is about 7 MB. Past 8192 distinct words on screen (only small cells
  on a large screen get there) the least recently drawn are drawn again
  when next needed, about 50 us each, and the counters report it.
- *GPU:* a word is one sprite in the glyph atlas, the canvas uploaded as
  coverage and keyed by its serial; each cell draws its sub-rectangle as a
  quad. A full 1920x1080 screen of Arabic at 22 px is about 1,500 word
  sprites, not 8,500 slices. A re-drawn word has a new serial, so its old
  sprite stays until the atlas is full and emptied whole, as it is today.

**The font.** Noto Naskh Arabic 2.021 (OFL 1.1, the upstream release's
unhinted face: Blit draws Arabic unhinted) ships unmodified in
`assets/fonts/`, pinned in `arabic-fonts.source.json` like every shipped
face. It is the default Arabic face for every text font, pixel fonts
included (their cap height comes from their H, ADR 0199). Blit Arabic
Modern stays as the other choice and keeps today's per-cell drawing
exactly. The choice is a font setting, `arabic` (`naskh` or `modern`),
beside `hinting`: in `PUT /v1/fonts`, in `document.font.arabic`, reported
as `active.arabic`, and a row in the Control Panel's Blit pane.

## Not chosen

- **One sprite per cell keyed by word and index**, as first sketched: a
  full 1920x1080 screen of Arabic at 22 px is about 8,500 distinct slices,
  past the atlas's reset point of 6,144 sprites every frame. One sprite per
  word is about 1,500.
- **Widening `BlitCellCluster` or `KmsSprite` to hold a word**: both are
  copied into every slot of their caches (1,024 glyph entries, 8,192
  sprites); a 544-scalar key is 2 KB a slot for the rare word.
- **A hash of the word as the key**: the caches promise a full compare on a
  hit (`blit_cell_cluster_hash`: "callers MUST compare the complete key"); a
  collision would draw the wrong word.
- **Letting the ink overhang sideways** and widening damage and the cursor
  by it: a cursor would recolour neighbouring letters and the halo stays
  3-4 columns. Fitting by ink shrinks no specimen word more than fitting
  by advance does (same scales at every height); it only moves some.
- **The renderer's per-cell overstrike for bold words**: each cell's first
  column misses the previous cell's overstrike, a seam every cell (797
  pixels across the specimens in the prototype).
- **Re-deciding the forms with HarfBuzz alone**: a word cut at the view's
  edge grows an initial or final form Hibr did not send, and letters Hibr
  kept apart join (285 wrong forms in the prototype).
- **Stretching words (tatweel, kashida) to put each letter over its cell**:
  not what a browser does. The owner keeps the browser look; stretching may
  come later as a Control Panel choice.
- **A second bidi pass, or Hibr sending logical text**: Hibr owns order and
  cells; Blit draws what it is given.

## Consequences

- Letters are no longer one per cell. Over the specimens at 8-64 px, with
  each shipped text font, 38-45% of letters have their ink centre outside
  their own cell, up to 1.5-2.8 cells away (Hack: 41%, 1.99): a
  proportional word centred in Hibr's cells. The cursor marks the cell,
  which can hold part of a neighbouring letter.
- Naskh words are narrower than Hibr's cells (at 22 px the bismillah's
  second word is 46 of 66 px), so the gaps between words are wider than a
  browser's. Only a synthetic pair of isolated behs and, at 18 px, the
  one-cell lam-alefs (0.97) shrink.
- The first frame of a screen of new Arabic shapes each word once. On
  this host under a load average near 15: typically 30-170 us a word, but a
  mean of 350-400 us over all heights with single words taking 5-39 ms, so
  a full 1920x1080 screen at 22 px (about 1,500 words) costs between 70 ms
  and over 0.5 s once; later frames shape nothing. To be measured again on
  a quiet host and on the mini PC.
- A run longer than 32 cells is fitted piece by piece, so the join where
  two pieces meet does not connect.
- `active.arabic_companion.shaping` reports `words` or `visual-cell-marks`.
- ADR 0174's "never shrink individual letters" becomes: a word may be
  shrunk as a whole to fit its cells, never a letter alone. Its cell-local
  base and mark clusters apply to the `modern` choice only.

## Evidence

A prototype (outside the repository) linked `src/blit_raster.c` and
`src/blit_font.c` read-only, with Noto Naskh Arabic, over the six Hibr
specimens and a line of cut words, unjoined letters and a mark the face
lacks (U+08FF), every cell height 8-64 (cell width (8h+8)/16), for Hack,
3270, JetBrains Mono NL, Fira Code, IBM Plex Mono and Spleen:

- each cell drew only its own slice; against a whole-line reference that
  places every glyph directly: 0 of 23,841,312 pixels differ, for every
  font, and with no cache (every slice shaping its word again);
- ink outside its word's cells: 0 sideways, 0 past one row; measured
  overhang 0 px above the row (2 with Spleen) and at most 8-14 px below
  over all heights (Hack: 4 px at 22 px, 12 at 59-63 px);
- the letters recovered from the cells equal the specimens' logical text,
  15 words of 15;
- forms: 2,964 glyphs named for one presentation form, none joining
  differently from Hibr's cell;
- tashkeel: 855 marks in, 741 mark glyphs out (Naskh draws shadda with
  fatha as one glyph), all with ink;
- the run with U+08FF falls back to per-cell drawing at every height;
- negative controls fail: each cell shaped alone (678,397 pixels differ),
  fitting by advance only (154 pixels outside their cells), the per-cell
  bold overstrike (797 pixels), no ZWJ/ZWNJ (285 wrong forms).

Landed (2026-11-20), each failing on a broken build:

- `arabic_word_text_gate`: every specimen's runs read back as its logical
  text, 15 words of 15, no joiner in a whole word; a cut word keeps its
  joins, letters left apart stay apart (fails with the letters left in
  visual order, and without the cut word's ZWJ);
- `arabic_word_slice_gate`, every shipped text font, heights 8-64: 5,700
  words, 30,210 slices composing their words exactly, 1,425 words keeping
  their tashkeel's ink, 285 refusals of a mark the face lacks (a Latin
  acute on a beh: Noto Naskh 2.021 has every Arabic mark), no ink dropped,
  bold the whole word's overstrike, and words unlike their cells shaped one
  by one (fails with a slice one pixel off, and with words two rows low);
- `arabic_word_view_gate`, through `draw_view`: 323 words placed exactly,
  551 one-cell edits equal to clean frames, the block cursor on 1,083
  letters changing nothing outside its cell (361 edits differ without the
  damage widening);
- `gles_compose_gate`: the GPU equals the CPU for words with bold, italic
  and a colour change mid-word, the cursor mid-word, and damage on one
  letter, a letter turned into a space and the far end of a six-cell word
  (fails with a sub-rectangle one pixel off, and without the GPU's
  widening).

Not accepted until the owner has seen the look on the mini PC.
