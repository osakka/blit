# ADR 0243: Glyph multiplexing -- one registry, adopted protocols, published

- Status: accepted as direction (the owner, 2026-11-21); both open
  decisions made (below): the icons moved to U+101000 and Blit Arabic
  Modern retired, in 0.3.131. Step 1, the registry, built in 0.3.130: one manifest
  (`assets/fonts/fonts.source.json`, the Arabic manifest folded in), a
  generated header (`src/blit_font_registry.h`), no face file named in
  `src/`, `/v1/fonts` reporting it; every font gate's output byte for byte
  as before (`font_registry_gate`)
- Date: 2026-11-21
- Scope: the font registry (`assets/fonts/`), the raster's stack
  (`src/blit_raster.c`, `src/blit_font.c`), `/v1/fonts`, the terminal
  parser (`src/blit_server.c`), and a published protocol document in
  `docs/`. Supersedes nothing yet; extends ADR 0195 (one glyph stack) and
  ADR 0197 (icons that span cells).
- Prior art, from primary sources: the scratch report behind this record
  (kitty OSC 66 and Unicode placeholders, ECMA-48 SGR 10-19 and FNT, xterm,
  DEC DRCS and DECDHL, WezTerm, Contour, iTerm2, foot, mintty, mlterm, the
  Glyph Protocol, terminal-wg VT2D, Nerd Fonts' PUA convention, capability
  detection), summarised here with its citations kept in the commit that
  lands each step.

## Context

The owner wants Blit to serve many fonts and glyph sets at once -- text,
Arabic with tashkeel, icon sets, symbols -- and keep adding them with no
penalty; programs that know Blit should choose from any of them, and the
way they do it should be written up so other terminals can implement it.
They also want glyphs drawn across blocks of cells (1x2, 2x2, 3x2 ...), and
everything to stay standard: a terminal that does not know the extension
still shows something sane.

Today Blit already multiplexes by codepoint (ADR 0195's stack: the text
font, Blit's icons, Nerd symbols, Noto symbols and maths, emoji, Arabic,
Unifont), and programs read the icon catalogue over HTTP
(`GET /v1/fonts/icons`). But faces are named as literals in the code and
described in two manifests and a catalogue; Arabic has two faces and a
switch; and nothing is discoverable in band (over ssh, or by a program that
cannot reach Blit's API).

What others do (the prior art): terminals multiplex by codepoint from the
user's configuration, invisibly to programs. Two in-band protocols exist and
are being adopted: the Glyph Protocol (Rio's `APC 25a1`: a support check
`s`, a per-codepoint coverage query `q`, and glyph registration; shipped in
Rio, partly in Ghostty and kitty) and kitty's text sizing protocol (OSC 66:
text at a multiple of the cell size; kitty, Contour, foot for width). Per-cell
font selection is old and standard: ECMA-48's SGR 10-19 with FNT
(`CSI Ps1 ; Ps2 SP D`); mintty honours SGR 11-19. No protocol names glyph
sets or lets a program look a glyph up by name.

How each kind degrades decides its syntax: text inside a control string is
lost on a terminal that does not know it; text outside, under an attribute,
is shown plainly; a private codepoint shows as a missing glyph. So anything
that must degrade without asking is an attribute around ordinary text, and
anything carried in a payload is sent only after a handshake, with the
program printing its own fallback otherwise.

## Decision

Build in this order, each step gated, each published step written as a
protocol document under `docs/` that another terminal can implement from:

1. **One registry.** A single manifest declares every face Blit ships: its
   id, role (text, word shaping, icons, symbols, emoji, last resort), its
   coverage, how it is drawn (by cell, fitted, or shaped as words), its
   licence and hash. A generated C header gives the raster its faces, so no
   file is named twice; `/v1/fonts` reports the registry. A choice exists
   only where the registry holds two faces for one role. The gate: every
   glyph every existing font gate draws is byte-identical before and after.
2. **In-band discovery: adopt the Glyph Protocol's `s` and `q` as they
   are.** Blit answers the support check (no registration formats until it
   accepts them) and the coverage query from its stack, so programs written
   for Rio, Ghostty and kitty work with Blit unchanged.
3. **Sets and names: extend the Glyph Protocol through its capability list,
   upstream first.** An optional capability (`names`) with three verbs:
   list sets (paged), look a glyph up by set and name (one reply per
   request, in order, so a program batches its names and ends with DA1),
   list a set's glyphs (paged, for pickers). Glyphs stay codepoints in the
   text -- they survive tmux, copy, scrollback and recording -- and a name
   is resolved once, at start-up; an unknown name or no answer means the
   program prints its own plain fallback. Names follow Nerd Fonts' where a
   glyph exists there. If upstream declines, the same verbs under a
   Blit-numbered OSC, same grammar.
4. **Per-cell face: ECMA-48 FNT and SGR 10-19.** FNT binds an alternative
   font to a set id from step 3 (`CSI 2 ; 7 SP D`); SGR 11-19 select per
   cell and SGR 10 returns to the primary. An unaware terminal shows the
   text in its own font. FNT is always sent with both parameters (kitty
   takes a one-parameter `CSI n SP D` as cursor-back; Blit's own
   cursor-back no longer does, fixed with a gate before this record).
   Programs send these only after the handshake (the Linux console gives
   SGR 10-12 other meanings). Blit's cell, cells API, raster and sprite
   keys gain the face; Arabic runs end where the face changes. Agreed with
   hibr first: Hibr's cells must carry it end to end.
5. **Multi-cell glyphs: adopt kitty's OSC 66 for every block it can
   express** (scale and width: 1x1 to 7x1, s x s, s*w x s), and propose a
   rows key upstream for the rest (3x2, 1x2, anything taller than wide),
   sent only to terminals that advertise it; otherwise the program uses the
   next larger legal block, or plain text. Blit's spanning icons (ADR 0197,
   the 4x2 discs) already draw across cells.

The handshake, in one write: the Glyph Protocol's support check, an
XTGETTCAP boolean for the rows key, kitty's cursor-position probe for OSC
66, then DA1; anything unanswered when DA1's answer arrives is unsupported.

## Decided (the owner's, 2026-11-21)

- **Blit's icon codepoints: decided (the owner, 2026-11-21), built in
  0.3.131.** Blit's icons and cell geometry were at U+100000-U+100495. The
  Glyph Protocol reserved U+100000-U+1003FF for programs' registered glyphs
  on 2026-11-15, and Nerd Fonts agreed to stay out; publishing step 2 with
  the icons there would contradict it. The set moved to U+101000-U+101495
  (unclaimed), the same layout, every glyph at old + 0x1000: the catalogue's
  codepoint values only (its keys, ids and format unchanged, because Hibr
  reads the file and names every glyph by id), the icon font rebuilt from
  it (only its cmap differs), and every codepoint the code names (the bar
  corners, the window, frame, control, key and player glyphs). The
  transition: from 0.3.131 the old range draws exactly the glyph it moved
  to, mapped by one function (`src/blit_icon_codepoints.h`,
  `blit_icon_scalar`) asked where a cell's scalar enters the raster's glyph
  and pen-plane lookups and the renderers' two cell tests
  (`blit_chrome_colour`, the GLES icon pen); the cell keeps what the
  program wrote, so the cells API, copy and scrollback read it back
  unchanged. `GET /v1/fonts/icons` adds `deprecated_range`
  (`{"from":"100000","to":"100495","moved_to":"101000"}`) to the response,
  not to the file. `icon_transition_gate` holds every old codepoint, in
  every pen, bold and Slate plane, and a CPU and a GPU frame, to the new
  one's pixels, and fails with the mapping compiled out. Hibr switches its
  map first (to be gated on Blit 0.3.131); a later release, named in its notes,
  drops the old range (`BLIT_ICON_OLD_RANGE_DROPPED`) and the gate's old
  half. Step 3's name lookup makes the move invisible to new programs.
- **One Arabic face** (2026-11-21). Noto Naskh Arabic covers every
  assigned codepoint of the six Arabic blocks; Blit Arabic Modern covered
  part of the base block and none of the Supplement or Extended blocks.
  Modern is retired, as Retro was (ADR 0195), so Arabic is one path with no
  switch:
  - the face (`BlitArabicModern.ttf`), its registry entry and role
    (`arabic_cells`), its FreeMono notices and source archive, its
    generator (`build-arabic-profile.py`) and the `blit-font-sources`
    package (ADR 0217) are removed; the registry has one Arabic face,
    `noto-naskh-arabic` (`arabic_words`);
  - the raster's per-cell companion path is removed: a run the word face
    refuses is drawn cell by cell from the glyph stack, and ink margins are
    a row above and below, none sideways (ADR 0241, amended);
  - the `arabic` font setting is removed (the Control Panel's Blit pane
    drops its row, in hibr-apps); `active.arabic` reports the face (`{family, path, shaping: "words"}`,
    null when it is not attached) and a stored `font.arabic` is ignored;
  - `blit` declares no relation to `blit-font-sources`: an installed copy
    is an inert source archive and stays until removed, because a Breaks
    makes the updater's `dpkg --install` refuse the update (checked with
    dpkg 1.22 on scratch packages); nothing is gained by a Conflicts, since
    an installed copy overlaps no file. hibr-apt should keep the package while it serves a
    `blit` of 0.3.130 or earlier, the GPL source of that release's face.

## Not chosen

- **One merged font file with every glyph:** a font holds at most 65,535
  glyphs (Unifont alone has 58,910, Nerd symbols 10,413); licences differ;
  Arabic's shaping tables do not merge; Blit ships upstream files unmodified
  and pinned.
- **A per-cell "named glyph" escape:** a glyph in a control string is lost
  by every other terminal and tool; a codepoint resolved once by name is
  not.
- **A rival multi-cell protocol:** OSC 66 exists and is implemented; the
  missing case is a key, proposed upstream.
- **Discovery over HTTP only** (`/v1/fonts/icons`): not in band, so not over
  ssh and not for other terminals. It stays for Hibr and tools.

## Consequences

- Blit stays standard: every extension is behind a handshake or degrades to
  plain text.
- "Unlimited" holds: a set and a name address any glyph, with no private
  codepoint ceiling beyond Unicode's two private planes, which the registry
  allocates.
- Each added face costs open time and memory at raster open; step 1
  measures that and loads faces on first use if it grows.
- Upstream proposals (Glyph Protocol `names`, OSC 66 rows) are written and
  sent by the owner or with their approval; Blit implements the agreed
  forms.
