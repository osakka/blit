# ADR 0243: Glyph multiplexing -- one registry, adopted protocols, published

- Status: accepted as direction (the owner, 2026-11-21); both open
  decisions made (below): the icons moved to U+101000 and Blit Arabic
  Modern retired, in 0.3.131. Step 1, the registry, built in 0.3.130: one manifest
  (`assets/fonts/fonts.source.json`, the Arabic manifest folded in), a
  generated header (`src/blit_font_registry.h`), no face file named in
  `src/`, `/v1/fonts` reporting it; every font gate's output byte for byte
  as before (`font_registry_gate`). Step 2, the Glyph Protocol's `s` and
  `q`, built in 0.3.133 (below, "Step 2 as built"; `glyph_protocol_gate`).
  Step 3's set level, the amendment's registry `sets` and the HTTP lookup,
  built in 0.3.134 (below, "Step 3's set level as built";
  `glyph_sets_gate`); its in-band `names` capability is still a draft
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
   Blit-numbered OSC, same grammar. The draft is
   `docs/protocols/glyph-sets.md` (amendment below).
4. **Per-cell face: ECMA-48 FNT and SGR 10-19.** FNT binds an alternative
   font to a face number from the registry (`CSI 2 ; 7 SP D`; amended
   below: a set is not a face); SGR 11-19 select per
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

## Step 2 as built (0.3.133)

The Glyph Protocol v1.18 (pinned in `docs/protocols/glyph-sets.md`,
Appendix C), as its specification defines it; the sequences and answers are
in `docs/terminal.md` ("Glyph Protocol").

- **Where.** An APC whose body begins `25a1;` (its §3.1) is answered by
  `terminal_apply_glyph_protocol` in `src/blit_server.c`; every other APC is
  Kitty graphics, as before. Replies are written as each request is read,
  like DA1's and XTGETTCAP's, so `s` then DA1 is answered in that order.
- **`s`:** `fmt=` empty, no `cap`. Blit accepts no registrations: its old
  icon range still draws inside the block during the transition, and a
  registration and an old icon must never compete for a codepoint
  (glyph sets §9). `grapheme` is not claimed: answering it needs the
  terminal's own segmentation of the cluster in the current mode (§5.3),
  which the parser does only by writing cells; `q ; grapheme=` is answered
  `reason=unsupported`, as §5.4 and §11 item 10 require.
- **`q ; cp=`:** `blit_raster_cell_covers` (`src/blit_raster.c`) asks the
  cell's own drawing path for the current font -- the wide PSF, the Arabic
  word face, the raster's stack, the PSF -- through the calls the renderers
  make, so the stack's order lives only in `raster_get_drawn`. The block
  and the old range (`blit_icon_codepoints.h`) answer `status=` empty; a
  control or a format character (a selector, a joiner) too. A lookup may
  draw and cache the glyph, so a session gets 256 a second and the rest
  `reason=unknown` (§5.4; SR-16).
- **`r` and `c`.** An empty `fmt=` means "every `r` will be rejected"
  (§3.3), and clearing an empty slot "MUST return `status=0`" (§7.2), so
  both are answered: `r` with `unsupported_format` in the block and
  `out_of_namespace` outside it (§6.2's order), `c` with `status=0`, both
  honouring `reply=`.
- **Safety (SR-4).** A reply is the framing, the specification's names and
  hex printed from a parsed number. A message with a byte outside printable
  ASCII, an unknown verb, or an `s`, `q` or `c` over 1024 bytes gets no
  reply.

## Step 3's set level as built (0.3.134)

The amendment below, "A set is a table of names, not a face", built as far
as HTTP; the in-band verbs (`q ; name=`, `n`) and the `names` capability
are not, and nothing in band changed.

- **The registry.** `fonts.source.json` has a `sets` level: the registered
  owner tokens (`blit`, `emoji`, `nerd`), the two scopes, and one entry per
  names source -- `blit` (owner `blit`, scope `terminal-local`, face
  `blit-icons`, names the catalogue's `icons`, `text_glyphs` and
  `cell_geometry`, version `sha256`: the catalogue's SHA-256 as packaged,
  its revision) and `nerd` (owner `nerd`, scope `portable`, face
  `symbols-nerd-font-mono`, names `nerd-glyphnames-v3.4.0.json` split by
  prefix, version `3.4.0`). The catalogue is unchanged.
- **Derived, not listed.** `tools/fonts/build-font-registry.py` derives the
  set ids from the sources (`blit`, and one `nerd-<prefix>` per prefix the
  Nerd file has: 17), checks every name against the draft's grammar (one
  name per glyph per set, no codepoint in U+100000-U+100494, the Nerd
  file's own METADATA version equal to the registry's), and writes the
  sources, the 18 sets and the retired names to `src/blit_font_registry.h`.
  Codepoint ranges are derived from the names at start and reported with
  a set's listing, never declared.
- **Names at run time, not compiled in.** `src/blit_glyph_sets.h` reads the
  two packaged names files once at start into one table sorted by set and
  name (bytewise), resolves aliases (a glyph's canonical name is the
  bytewise smallest of its names in its set) and the ranges, and answers
  every lookup by binary search. Compiling the names in was the
  alternative: it would duplicate the packaged catalogue the blit set's
  version must hash (the same file `GET /v1/fonts/icons` serves and Hibr
  reads), and every catalogue edit would need the header regenerated. The
  cost, measured: 11,594 names (830 in `blit`, 10,764 in the Nerd sets) in a table of about 460 KB (24-byte records, a names pool, the replacements and ranges); about 16 ms once at start in the tcc build (13 ms read outside the server, 7-8 ms built with gcc -O2; 3 ms of it the catalogue's SHA-256), and about 0.6 MB more resident (24.4 against 23.9 MB at idle beside 0.3.133); a lookup is a binary search. A source that cannot be read is not
  served and the diagnostics log says why; the server starts.
- **HTTP.** `GET /v1/fonts` gains `sets` (`{id, owner, scope, version,
  count, faces}`); `GET /v1/fonts/sets` lists them; `GET
  /v1/fonts/sets/{set}` pages a set's listed names (at most 128 a page,
  `after`, `limit`, `more`), with its ranges; `GET
  /v1/fonts/sets/{set}/{name}` answers a record (`codepoints`, `alias_of`,
  `deprecated` and `replaced_by`, `tile`), a retired name a 410 `retired`.
  The grammar is checked before any lookup: outside it, a 400
  `malformed_query` with one fixed body (nothing echoed); `unknown_set` and
  `unknown_name` are 404s. Authenticated like every route; read-only.
- **Retired names start with the lookup (the owner, 2026-11-22).** The
  catalogue's history has 345 ids removed before 0.3.134 (the 16 large
  phone icons in 0.3.5, the 288 file colourways in 0.3.8, the 35 Slate
  dark glyphs in 0.3.39). They were never addressable by name, so nothing
  can depend on them: they are not carried and answer `unknown_name`.
  From 0.3.134, a name the lookup published and the catalogue later drops
  is carried as retired (410 for good, never reused), with a replacement
  only when the owner names one; `glyph_sets_gate` holds the catalogue's
  ids since the lookup to live-or-retired.
  The scope is spelled `terminal-local` everywhere (the draft's JSON
  example said `terminal`). The registry's version rule for the catalogue
  is `sha256`; the retired names are in the registry, not the file, so a
  change to them alone would not change the version -- they change only
  with a catalogue change that removes ids. HTTP pages take the draft's
  128-entry bound; its 4096-octet bound is the in-band frame's. `emoji` is
  registered as an owner but not served: `emoji-test.txt` is not in the
  package.
- **The gate.** `glyph_sets_gate` reads the catalogue, the Nerd file and
  the registry's retired names itself and holds the server to them: the
  sets, counts and versions in `GET /v1/fonts` and `GET /v1/fonts/sets`;
  every set paged through (every listed name, codepoint, alias, tile and
  the ranges); every name looked up, deprecated and retired ones included;
  the retired names equal to the catalogue's retired codepoints and, with
  the history present, every id the catalogue ever published live or
  retired; the 11 clashes per set, with no `-`/`_` folding; the grammar
  (uppercase, too long, empty, bad hyphens, separators, encoded and raw
  control and non-ASCII bytes, bad queries) one fixed 400; the paging
  bounds; 404 `unknown_set` and `unknown_name`, 410 `retired`; the bearer
  token; 405 for other methods. It fails on 0.3.133 (no `sets`).
  `font_registry_gate` prints the same line as before.

## Decided (the owner's, 2026-11-21)

- **Blit's icon codepoints: decided (the owner, 2026-11-21), built in
  0.3.131.** Blit's icons and cell geometry were at U+100000-U+100494. The
  Glyph Protocol reserved U+100000-U+1003FF for programs' registered glyphs
  on 2026-11-15, and Nerd Fonts agreed to stay out; publishing step 2 with
  the icons there would contradict it. The set moved to U+101000-U+101494
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
  (`{"from":"100000","to":"100494","moved_to":"101000"}`) to the response,
  not to the file. `icon_transition_gate` holds every old codepoint, in
  every pen, bold and Slate plane, and a CPU and a GPU frame, to the new
  one's pixels, and fails with the mapping compiled out. Hibr switches its
  map first (to be gated on Blit 0.3.131); a later release, named in its notes,
  drops the old range (`BLIT_ICON_OLD_RANGE_DROPPED`) and the gate's old
  half. Step 3's name lookup makes the move invisible to new programs.
  - **The range's end, corrected in 0.3.136.** The last glyph that moved
    was U+100494 (now U+101494); U+100495 was the next free codepoint and
    never held a glyph. The transition range was written to U+100495, one
    past, which drew nothing while U+101495 was empty. 0.3.136 places the
    slider styles from U+101495 (ADR 0237), so the range now ends at the
    last glyph that moved: `BLIT_ICON_OLD_LAST` is U+100494,
    `deprecated_range` says `{"from":"100000","to":"100494","moved_to":"101000"}`,
    and the set-address grammar and the registry keep names out of
    U+100000-U+100494. Glyphs placed after the move (U+101495 on) have
    no old codepoint; `icon_transition_gate` checks that nothing from
    U+100495 to U+100FFF maps or draws.
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

## Amendment (2026-11-21; the set level built in 0.3.134, the in-band verbs still a draft): the set level and the standard path

- **A set is a table of names, not a face.** One file declares both:
  `fonts.source.json` gains `sets` (id, owner, scope, faces, names
  source, version, retired names). Set ids are derived from the names
  sources, never listed by hand: `blit` (the catalogue's 850 ids, its
  three lists, the catalogue itself unchanged), `nerd-<prefix>` (the 17
  prefixes of Nerd Fonts' `glyphnames.json`, names as Nerd spells them
  after the prefix), `emoji` (Unicode's `emoji-test.txt`, by a fixed
  name rule). Codepoint ranges are derived, never declared (a Nerd set
  is not one range). Retired names live in the registry, because the
  catalogue's shape is frozen. The server reads the packaged names
  sources once at start; `/v1/fonts`, a new `/v1/fonts/sets/{id}` and the
  in-band answers come from the one table, and a gate holds them equal.
- **Addresses are `set/name`**, lowercase ASCII, no case folding, set ids
  1-32 and names 1-96 octets, excluding the Glyph Protocol's framing
  characters, so a reply never escapes anything. In Blit's own set a
  name is never reused; portable sets follow their owner's releases.
  Deprecated names resolve with what replaces them; retired, `retired`.
- **Codepoints are terminal-local, and the record says so.** Portable
  sets (Nerd, emoji) print the same everywhere at the same version;
  Blit's set is terminal-local and versioned by its catalogue revision.
  Recordings, copies and a second tmux client show whatever their
  terminal draws at the codepoint. A lookup never answers a codepoint
  in U+100000-U+1003FF, and Blit accepts no registrations until a
  release has dropped its old icon range.
- **FNT's second parameter is a number** ("a register which is to be
  established", ECMA-48 §8.3.53; no such register was found). It cannot
  carry a set id. Step 4 therefore binds a *face*: each registry face
  gets a stable number, never reused, reported by `/v1/fonts`; its
  in-band form is step 4's to define, not the names capability's.
- **What "standard" means here, and the one path taken.** No body
  allocates escape sequences or registers terminal extensions in
  practice: ECMA-48 was last revised in 1991 and its committee is
  inactive; xterm's control-sequence document describes xterm; the
  freedesktop terminal-wg repository has accepted nothing since 2019;
  terminfo user capabilities have no registry; Unicode's private-use
  registries are for constructed scripts. The extensions other terminals
  adopted (kitty's keyboard and graphics protocols, the Glyph Protocol)
  became standards by a published specification, a reference
  implementation, a detection handshake and adoption. So Blit's path is
  that one, upstream first: the owner offers the `names` capability, the
  registered owner tokens and the set table to the Glyph Protocol's
  specification, whose `cap` list is its extension point (v1.14 added
  `grapheme`, raised on Contour's tracker by a library author).
  "Registered" means listed
  there. Done means: the capability is in that specification, Blit
  answers it, a conformance probe any terminal can run is published,
  and at least one other terminal implements it. The known objection --
  the specification withdrew `cp=auto` (v1.6) because a codepoint
  learned from a reply does not survive replay -- is answered in the
  draft: a name's answer is fixed by a published table and version, not
  by session state, and portable sets give every terminal the same
  answer. If upstream declines, Blit ships the same verbs under OSC
  7701 with the same document, and the "standard" is Blit's document,
  probe and any adopter.

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
- **A terminal-wg proposal as the registration path:** its process
  exists on paper (a merge request into `proposals/`), but nothing has
  been accepted since the repository opened in 2019. An issue there
  linking to the published capability costs nothing and may be filed by
  the owner for visibility; it is not where the standard lives.
- **One `nerd` set holding every Nerd name:** it would hide Nerd Fonts'
  own 17 sets behind prefixes inside names; the address would no longer
  say which set.
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
