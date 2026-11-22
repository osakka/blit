# ADR 0245: Installable glyph sets -- a root-owned store, a ledger of codepoints, per-user views

- Status: proposed
- Date: 2026-11-22
- Scope: the font registry's set level (`assets/fonts/fonts.source.json`,
  `src/blit_glyph_sets.h`), the raster's glyph stack (`src/blit_raster.c`),
  `/v1/fonts` and `/v1/fonts/sets`, the frame socket (ADR 0202), the Glyph
  Protocol's `q` answers (`src/blit_server.c`), a new import worker, a new
  root-owned store under `/var/lib/blit`, packaging (a system user, a
  pinned catalogue key, a possible `gpgv` dependency), and two clients
  outside Blit: a "Blit Glyphs" Control Panel pane and a `glyphs` kind in
  the hibr-apps catalogue. Extends [ADR 0243](0243-glyph-multiplexing.md)
  (one registry, sets and names); follows [ADR 0232](0232-an-update-installs-blit-and-its-compositor-together.md)'s
  amendments on how root handles files it is handed (SR-2, SR-10).

## Context

The owner's idea (2026-11-22): a separate "Blit Glyphs" pane in the Control
Panel listing every glyph set and font Blit has -- where it came from, how
many glyphs, its version, a set browsed by name, install and remove -- with
glyph sets installable from the hibr-apps catalogue, in three locations.

What exists (ADR 0243, built through 0.3.134):

- **One registry.** Every face Blit ships is declared once in
  `fonts.source.json`; `tools/fonts/build-font-registry.py` writes
  `src/blit_font_registry.h`. Its `sets` level declares two names sources
  (`blit`, the icon catalogue; `nerd`, Nerd Fonts' `glyphnames.json` split
  into 17 `nerd-*` sets). `src/blit_glyph_sets.h` reads the names files
  once at start into one sorted table (18 sets, 11,594 names); the set
  count is compiled in (`BLIT_GLYPH_SET_COUNT 18`,
  `BLIT_GLYPH_SOURCE_COUNT 2`). `GET /v1/fonts/sets/{set}/{name}` answers
  a name's codepoints; a name the lookup published and that later goes is
  retired (410, never reused; the owner, 2026-11-22, commit 5d09c24b).
- **Discovered text fonts.** `/usr/lib/blit/fonts/text` (shipped) and
  `/usr/local/lib/blit/fonts/text` (the administrator's own, root-owned;
  `BLIT_FONT_LOCAL_ROOT`) are re-read on every `GET /v1/fonts`
  (`blit_font_discover`, `src/blit_font.c`); profiles carry
  `source: shipped|local`.
- **Address rules.** [Glyph sets](../protocols/glyph-sets.md) §3.1: a set
  id starts with a registered owner token and a terminal MUST NOT serve a
  set under an unregistered one; §5: codepoints of a terminal-local set are
  the terminal's, a program caches them only keyed by set id, set version
  and terminal identity; §9: no set places a glyph in the Glyph Protocol's
  registration block, U+100000-U+1003FF.
- **The server is root** and parses fonts with FreeType and HarfBuzz in
  process. An API bearer token is a root shell today (SR-9, the accepted
  LAN-testing exception); since 0.3.135 a session run as another user gets
  no API token, only its session's frame-socket credential, which only its
  own uid can use (SR-8, ADR 0202).

Measured (2026-11-22, from `assets/fonts/nerd-glyphnames-v3.4.0.json` and
`icons/catalogue.json`):

| Range | Drawn by Blit today |
| --- | --- |
| BMP private use, U+E000-U+F8FF (6,400) | 3,499, all Nerd |
| Font Awesome, U+F000-U+F2FF | 768 of 768 |
| Codicons, U+EA60-U+EC1E | 438 of 447 |
| IcoMoon and Fontello's default start, U+E000-U+E0FF | 51 of 256 |
| Plane 15, Nerd's Material Design, U+F0001-U+F1AF0 | 6,880 of 6,896 |
| Plane 16 | 830 Blit ids in U+101000-U+101494 (and the old range, U+100000-U+100495, during the transition) |

So an icon font as its author ships it lands on codepoints Blit already
draws: Font Awesome on Font Awesome, an IcoMoon export on Nerd's Seti and
Powerline Extra glyphs. Installing one cannot mean "add the face to the
stack at its own codepoints".

## Options considered

1. **A folder users or the administrator drop fonts into, read live.**
   Simple, and the text-font discovery already works so. Refused for glyph
   sets: `/v1/fonts` re-reads folders on every request, so a user-writable
   live path hands root's FreeType any file a user drops, at a time the
   user chooses, by a path the user can swap; and nothing would allocate
   codepoints.
2. **Add an installed face to the stack at the font's own codepoints.**
   Refused: the table above. Every popular icon font collides with Nerd.
3. **Rewrite the font's cmap to Blit's codepoints.** Refused: a modified
   font is a derivative (the OFL's Reserved Font Name rule; other licences
   differ), and root would still parse the original.
4. **Blit allocates codepoints, root never parses a font, sets keep their
   owner's scope.** Taken (below).

## Decision

### 1. Scope: user sets and system sets (the owner, 2026-11-22)

Machine-wide-only was proposed and **rejected** by the owner: installing as
yourself must not break Hibr's user and system scopes. So:

- **Every installed set has a scope:** `system` (every session sees it) or
  `user` (only sessions running as that uid see it). Shipped sets are
  system.
- **Blit knows each session's uid** (the session's `user`, or the server's
  own). A session's **view** is: the shipped sets, the system sets, and
  its own uid's sets. A codepoint of a set outside the view draws as an
  uncovered codepoint (the replacement glyph, never another face) and the
  Glyph Protocol's `q ; cp=` answers it `status=` empty.
- **The ledger is global** (section 3): every installed glyph gets a
  codepoint unique on the machine, whichever scope installed it. Two users
  therefore never collide on a codepoint, and the caches keyed by
  codepoint -- the raster's (`cache_hit`, `src/blit_raster.c`; one raster
  per server, `runtime->font.raster_state`) and the GLES compositor's
  sprite atlas (`src/blit_kms_gles.c`) -- stay shared without one user's
  glyph ever drawn as another's. The view is checked **before** either
  cache, so a cached glyph is never shown to a session whose view lacks
  it.
- **Shadowing.** When a user installs a set whose id a system set already
  has, that user's sessions see their own (as Hibr's user scope comes
  before its system scope); every other session sees the system one. Each
  has its own codepoints (section 3), so a cell already showing the system
  glyph keeps showing it.

### 2. Locations

| Location | Holds | Written by | Read by root as |
| --- | --- | --- | --- |
| Shipped, `/usr/lib/blit/fonts` | every face in the registry, the names files | the package | fonts (as today) |
| Local, `/usr/local/lib/blit/fonts` | the administrator's own text fonts, in `text/` (as today) | root, by hand | fonts (as today) |
| Installed, `/var/lib/blit/glyphs` | installed glyph sets: `system/<set>/` and `users/<uid>/<set>/`, plus `ledger` | **only** Blit's import (section 5) | outlines in Blit's own format (section 6), never a font |

- The store is `root:root 0700`, its parts `0600`. The server refuses to
  use it (and says why in the diagnostics log; the shipped sets are still
  served) unless the store, `/var/lib/blit` and every parent are
  directories owned by root and writable by no one else -- the check
  `private_directory_ready` makes of its own directory in
  `src/blit_update_helper.c`, extended to every parent. It is a
  sibling of `/var/lib/blit/updates`, which is `root:blit 0770` and must
  never be its parent (SR-3, SR-10).
- **Local holds no glyph sets.** One way in means one writer of the ledger
  and one validator; a folder re-read on every request would also see a
  half-copied set. An administrator installs their own set with the
  import, as `system` (section 5, origin `own`).
- `BLIT_GLYPH_STORE` overrides the store for gates only, as
  `BLIT_FONT_LOCAL_ROOT` does for the local root.

### 3. Codepoints: allocated by Blit, from a ledger never reused

- **The block.** Installed glyphs take codepoints from
  **U+102000-U+10FFFD** (57,342 codepoints). U+101000-U+101FFF stays
  Blit's own set's room to grow (its 830 ids span U+101000-U+101494).
  - Not the Glyph Protocol's registration block, U+100000-U+1003FF: it is
    reserved for glyphs programs register at run time, and Blit's old icon
    range still draws over it during the transition (glyph sets §9); a
    lookup may never answer a codepoint there.
  - Not plane 15: Nerd's Material Design occupies U+F0001-U+F1AF0 and
    grows with every Material Design Icons release, and other icon fonts
    and users' text fonts put glyphs there; the stack would have to
    arbitrate.
  - Not the BMP's private use area: the table in the Context.
  - U+10FFFE and U+10FFFF are noncharacters.
- **The ledger** (`/var/lib/blit/glyphs/ledger`, one record per line,
  appended and `fsync`ed before an import is answered) maps
  **(scope, set id, name) to a codepoint**, scope being `system` or a uid.
  `set/name` is the durable address within a scope; the codepoint is its
  allocation there.
  - **Stable across updates.** An update keeps every name's codepoint,
    whether or not the publisher redrew the glyph (glyph sets §6: a glyph
    may be redrawn, its meaning does not change). A redraw is the one
    moment a codepoint's picture changes, so at the swap Blit drops that
    set's codepoints from the raster's cache and resets the GLES sprite
    atlas (`kms_atlas_reset`, which exists); the gate shows the new pixels
    without a restart.
  - **Stable across uninstall.** Reinstalling the set in the same scope
    gives every name its old codepoint back. Returning a codepoint to the
    address it was allocated to is not reuse.
  - **Never reused.** A codepoint, once allocated, is never given to
    another (scope, set, name), whatever is uninstalled.
  - **Per scope, so nothing leaks.** Two scopes installing the same set get
    different codepoints; no allocation reveals what another user
    installed. The cost is the ledger's space: each scope's install spends
    its own codepoints. A user who wants a set the system already has does
    not install it (the pane says "installed for everyone").
- **Allocation order.** At an import, a set's new names take the next free
  codepoints in bytewise order of name, so a first install is one
  contiguous run; names added by later updates follow wherever the ledger
  then is. Ranges stay derived and descriptive, as for shipped sets.
- **Limits that follow from a finite block that is never reused.** The
  57,342 codepoints are the machine's for its life. `system` may allocate
  up to 24,574 of them; each uid up to 8,192 in its lifetime (a setting,
  `glyph_user_quota`; the users draw on the remaining 32,768 first come,
  first served). For scale: Font Awesome Free's ~2,000 icons, installed for
  the system, use 3.5 % of the ledger; the same set installed separately
  by four users uses another 14 %; one user's 8,192 lifetime is four sets
  of that size or one of Nerd's Material Design's (6,880, which Blit
  already ships). Updates that only redraw spend nothing; only names a
  release adds are new allocations. A full ledger answers 507
  `glyph_ledger_full`, a spent quota 507 `glyph_quota_exhausted`, each
  with nothing changed. The pane shows the ledger's use. Resetting the
  ledger is an operator's act outside the API (stop Blit, remove the
  store), and breaks every printed codepoint; the documentation says so.
- **A set's version** for an installed set is the SHA-256 of its names
  file and its allocation listing in that scope (`name codepoint` lines,
  bytewise), so it differs between machines and between a user's copy and
  the system's whenever the codepoints differ, changes with every update,
  and a program that caches by set id and version (glyph sets §5) looks
  up again. The publisher's own version is reported separately as
  `release`.

### 4. One table, one namespace

- **Owner token `x`**, registered in glyph sets §3.1's table when this is
  built: "a set installed on this terminal; always terminal-local".
  Installed set ids are `x-<name>`, `<name>` the catalogue entry's name
  (`glyphs/<name>`) or the importer's choice, 1-30 octets of the set-id
  grammar (`x-` plus at most 30 is the 32-octet cap). An `x-` id can never
  equal `blit`, `nerd-*`, `emoji` or any owner registered later, because
  owners are the table's rows and `x` is one. An installed set is always
  `terminal-local`, even when it repackages a portable set: its codepoints
  are this machine's ledger's. (RFC 6648's objection to `X-` names -- that
  experiments get standardised under the wrong name -- does not arise: an
  `x-` set is never promoted; a published owner is a new row and a new
  id.)
- **The same set table.** Installed sets join `blit` and `nerd-*` in the
  one sorted table `src/blit_glyph_sets.h` builds. The shipped sources and
  sets stay compiled in; installed ones become a run-time array beside
  them (bounded: 64 sets per scope, 1,024 in all), and the table is
  rebuilt -- off the render path, swapped in whole -- at start and after
  an import or removal. Each installed set's manifest in the store uses
  the **same schema** as a registry `sets` entry
  (`{owner: "x", scope: "terminal-local", faces, names: {file, format},
  version, retired_names}`) plus `install` (section 8), so one reader
  serves both.
- **The same checks.** The loader's refusals -- a name outside the
  grammar, a duplicate, a codepoint in the registration block, a file over
  `BLIT_GLYPH_FILE_MAX` -- are the import's refusals, by the same function;
  `build-font-registry.py`'s checks of the shipped sources stay where they
  are, and a gate holds the two to the same cases.
- **Names file format `blit-glyph-names`** (JSON, at most 4 MiB): the
  publisher's `release`, and `glyphs`, each name mapped to the font's own
  codepoint (`"house": {"code": "f015"}`), with optional `aliases`,
  `deprecated` (with `replaced_by`). Every glyph is one cell, drawn fitted
  as Nerd's are. A glyph across several cells is step 5 of ADR 0243 (OSC
  66 scales any codepoint without spending one per cell), not tiles: tiles
  would spend a ledger codepoint per cell and need a slicing rule per
  glyph.

### 5. Installing is a Blit API; Blit never depends on Hibr

- **Upload, never fetch.** Blit is handed the bytes; it never fetches a
  catalogue URL. ADR 0232 refused a network fetch as root (its option 2),
  Blit has no HTTP client or TLS, and the catalogue already fetched,
  verified and placed the files as their owner. Blit verifies again
  itself, on its own copy.
- **Two transports, two identities.**
  - **HTTP, the bearer token.** `PUT`/`DELETE /v1/fonts/glyphs/system/{set}`,
    and, as an administrator's act, `/v1/fonts/glyphs/users/{uid}/{set}`.
    The bearer token is root's (SR-9), so it may act for any scope; it
    names no user. A body carries its parts in order, framed by one header
    as `PUT /v1/control/update` frames its packages:
    `X-Blit-Glyph-Parts: index=<length>:<sha256> signature=... names=...
    font=... licence=...`, summing to `Content-Length`.
  - **The frame socket, the session's uid.** A session's own channel
    (ADR 0202; `SO_PEERCRED` and the session's frame credential) gains a
    `GLYPHS` request: `import <set>` with the parts passed as open
    descriptors (`SCM_RIGHTS`), `remove <set>`, and the session's own
    reads (section 8). The uid is the peer's, never a value the request
    states, and a user-scope import or removal is possible only here (or
    by the administrator over HTTP). Every program in a session has the
    channel (`BLIT_FRAME_SOCKET` and `BLIT_FRAME_TOKEN` are in its
    environment), including sessions run as another user, which hold no
    API token (SR-8).
    Blit reads each descriptor once (a regular file, size-capped, never a
    path) into its own copy.
- **Copy, verify, then parse, one object.** Every part is copied into a
  root-owned `0700` staging directory inside the store (same filesystem,
  so the final step is a `rename`), each by one read, never by the
  caller's path (as `copy_staged` in `src/blit_update_helper.c` does since
  SR-10). The signature and hashes are checked on the copy; the copy is
  what the worker extracts (section 6); the extracted outlines are what
  root validates and moves into `system/<set>/` or `users/<uid>/<set>/`.
  Nothing is visible to any view until the ledger record is durable and
  the rename is done; a failure removes the staging copy and changes
  nothing.
- **Origins.**
  - **(a) `catalogue`, decided:** the upload carries `index.json`,
    `index.json.asc` and the entry's files. Blit requires the signature to
    verify under its own pinned copy of the catalogue key (below), finds
    the entry `glyphs/<name>` in the index, and requires every part's
    SHA-256 to equal the entry's (`sha256`, and each `parts[].sha256`, as
    `mkindex` writes them). Works for either scope: system from HTTP, user
    from the frame socket.
  - **(b) `own`, a user's own unsigned font: awaiting the owner** (below).
  - **`own` as `system`:** the administrator's own font over HTTP (the
    token is root anyway); extracted by the worker like a catalogue font,
    with no signature.
- **How Hibr's catalogue fits.** A bundle install only places files
  (verified, then `mkdir`/`rm`/`mv`) and tells running desktops `rescan`;
  it runs no install step. User scope places
  `$XDG_DATA_HOME/hibr/glyphs/<name>/`, system scope
  `$HIBR_SYSDIR/glyphs/<name>/` (`/var/lib/hibr/glyphs`). On `rescan` (and
  at start) the pane notices a placed set that Blit lacks or has at another
  release, and imports it: a user-scope set over the frame socket, a
  system one over HTTP. Blit learns of nothing by itself and reads none of
  Hibr's paths.
- **The `glyphs` catalogue kind: decided (the owner, 2026-11-22).** The
  14th kind, `glyphs/<name>`, is approved, though it touches the parked
  epic #561. An entry is a bundle of parts: the names file as the entry's
  bundle, the font and the licence as `parts`, each with its own
  `sha256`.

### 6. Root never parses a font

FreeType's TrueType bytecode interpreter, CFF charstrings and table parsers
are its largest attack surface; HarfBuzz is not needed for glyph sets at
all (glyphs are drawn per codepoint, not shaped). So an installed set is
**drawn from outlines in Blit's own format**, rasterised by FreeType's
outline renderer from an `FT_Outline` Blit builds (`FT_Outline_Render`:
the smooth rasteriser only, no font loaded), fitted to the cell as Nerd
glyphs are, unhinted.

- **`blit-glyph-outlines` 1**, the only thing root reads of a glyph:
  a fixed header (magic, version, units per em at most 16,384, glyph
  count), then per glyph its name index, advance, bounding box and 1-16
  layers, each layer a colour (the cell's foreground, or one sRGB value:
  COLR v0's model) and contours of points tagged as `FT_Outline` tags
  them -- on-curve, quadratic off-curve (TrueType `glyf`) or cubic
  off-curve (CFF, as Font Awesome's OTF fonts are) -- in signed 16-bit
  coordinates within twice the em box. FreeType's outline renderer draws
  both kinds of curve, so nothing is converted.
  Bounds checked before use: at most 4,096 points and 512 contours a
  glyph, 16 MiB a file, every count and offset inside the file, every
  contour closed and at least three points, nothing after the last glyph.
  A file that fails any check is refused whole, with the first reason.
  The validator is small, has no allocation driven by an unchecked count,
  and is fuzzed (section 11).
- **Extraction runs in a worker, never in root.** `blit-glyph-worker`, a
  new small binary (FreeType only), started by the server per import with
  the copy's descriptor and a pipe:
  - drops to a dedicated system user `blit-glyphs` (no shell, no home;
    `adduser --system` in the deb, `sysusers.d` for rpm and Arch), with
    `setgroups(0)`, `PR_SET_NO_NEW_PRIVS`, no other descriptors, an empty
    root-owned working directory, a new network namespace where
    `unshare` allows, and limits: 512 MiB address space, 20 s CPU,
    no files written (`RLIMIT_FSIZE 0`), no processes;
  - then a seccomp allow-list (read, write, `mmap`, `munmap`, `mremap`,
    `brk`, `close`, `fstat`, `lseek`, `futex`, `clock_gettime`,
    `rt_sigreturn`, `exit_group`), installed after the font is read
    into memory (`FT_New_Memory_Face`);
  - takes a single TrueType (`glyf`) or CFF font, and refuses what the
    format cannot carry, with its reason: a collection (TTC), CFF2, WOFF
    and WOFF2, a variable font, COLR v1, SVG and bitmap
    strikes (`sbix`, `CBDT`, `EBDT`) -- each would need a second drawing
    path -- and a font over 16 MiB or 65,535 glyphs; it extracts each
    named glyph's outline (`FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING`, COLR
    v0 layers where present) and writes `blit-glyph-outlines` to the pipe.
  - Root trusts nothing it says: the output is validated like a user's
    (above), and a worker that crashes, overruns a limit or writes junk is
    a refusal (`glyph_font_rejected`) with nothing changed.
  - **Cost:** this is Blit's first sandbox. It needs a seccomp filter per
    architecture (x86-64 and AArch64), a system user in three package
    formats, a gate that shows a forbidden call kills the worker, and a
    second binary in the package. The seccomp step may ship in the release
    after the worker if the filter needs its own review; the user, limits
    and namespace come with the worker.
- **What this buys, honestly.** A hostile font reaches FreeType only in
  an unprivileged, resource-capped process that can write nothing but a
  pipe; the worst it can do is make that process lie, and a lie is
  bounded outlines that root checks. Root itself parses JSON (the index,
  the names, with the bounded reader it already has) and the outline
  format. The original font stays in the store, unmodified, for its
  licence and provenance; root never opens it again.
- **(b) The user's own unsigned font: awaiting the owner.** Proposed (the
  owner's suggestion, written up): the user's own session extracts it --
  Blit ships `blit-glyph-extract`, the worker's extraction as a plain
  command a user runs as themself, with no sandbox, since a hostile font
  then compromises only the user's own process, on the user's own file --
  and passes Blit the `blit-glyph-outlines` and names files over the frame
  socket, which root validates exactly as it validates the worker's output
  (as the Glyph Protocol's registration accepts glyph data, not fonts).
  The set is user scope only, origin `own`, shown unverified, licence the
  user's text or "the user's own".
  - **Why not the worker for (b):** then every local user could hand any
    font to FreeType in Blit's sandbox at will; with (a) only fonts the
    catalogue signed reach it from a non-root caller. With (b) as
    proposed, the only glyph data an unprivileged user can ever hand root
    is the outline format. This holds fully with the native signature of
    section 7; with `gpgv`, any user can still hand `gpgv` (not FreeType)
    arbitrary signature bytes inside the same sandbox.
  - **Cost:** one more installed command and its gate; the font file is
    not stored (Blit cannot vouch for it), so the pane shows only what the
    user declared about it; colour is COLR v0 layers at most, as for (a);
    and the per-uid quota (section 3) is the only bound on how much of the
    ledger a user spends on their own glyphs.

### 7. Verifying the catalogue's signature

The hibr-apps index is signed with the hibr-apt repository key, fingerprint
`A1E3CA788A8FB02052DA22B413035BE1B1674431`, an **RSA-4096 OpenPGP** key
(checked against the keyring Hibr installs). Monocypher, which Blit vendors
for its release signatures (SR-2), does Ed25519 only, so Blit cannot verify
this signature natively.

- **As things are: `gpgv`.** Blit packages its own copy of the public key
  (`/usr/lib/blit/catalogue-keys/hibr-apps.gpg`, root, `0644`; Blit never
  reads Hibr's keyring) and runs `gpgv --status-fd 1 --keyring <that>
  index.json.asc index.json` **inside the worker's sandbox** (the same
  user, limits and namespace; `gpgv` is exec'd after the drop, so no
  seccomp), on the staging copies passed as descriptors. Root requires
  exactly one `VALIDSIG` whose primary-key fingerprint is the pinned one,
  and no `BADSIG`, `ERRSIG`, `EXPKEYSIG` or `REVKEYSIG`; then hashes every
  part itself. A `gpgv` an attacker's signature exploited can at most
  claim "valid" for an unsigned font, which then meets the same worker,
  limits and outline checks. `gpgv` becomes a dependency (deb `gpgv`,
  which apt already depends on; rpm `gnupg2`; Arch `gnupg`).
- **Cleaner: a signature Blit verifies natively.** If the catalogue's
  index also carried a detached raw Ed25519 signature
  (`index.json.sig`, 64 bytes, over the same bytes) under a key Blit pins,
  root would verify it with the Monocypher code that already verifies
  releases: no subprocess, no OpenPGP parser, no new dependency. That is
  hibr's to add (open question 2); until then, `gpgv` as above.
- **What the signature buys.** Not privilege -- a bearer token is root
  anyway (SR-9). It buys provenance (the pane says "verified, catalogue")
  and, for user scope, that only fonts the catalogue's publisher signed
  ever reach the worker from an unprivileged caller.

### 8. What the API reports, and to whom

The view is the caller's: a frame-socket caller gets its session's view;
an HTTP caller gets the system view by default and, being root's, may ask
for a session's view with `?session=<id>`.

```text
GET  /v1/fonts                          sets gain source and install
GET  /v1/fonts/sets[?session=]          the view's sets
GET  /v1/fonts/sets/{set}[?...]         a page, as today, in the view
GET  /v1/fonts/sets/{set}/{name}        a record, as today, in the view
GET  /v1/fonts/glyphs                   installed sets in every scope, the ledger's use
GET  /v1/fonts/glyphs/{scope}/{set}/licence     the licence text
PUT  /v1/fonts/glyphs/system/{set}      import (HTTP: system, or users/{uid}/{set})
DELETE /v1/fonts/glyphs/system/{set}    uninstall
frame socket (the peer's uid and session; no API token needed):
  GLYPHS sets                     the session's view, as GET /v1/fonts/sets
  GLYPHS page <set> [<after>]     a page, as GET /v1/fonts/sets/{set}
  GLYPHS lookup <set>/<name>      a record, as GET /v1/fonts/sets/{set}/{name}
  GLYPHS import <set> | remove <set>
```

The frame-socket answers are the same JSON bodies, from the same function,
so a program in a user's session -- which has no API token -- resolves
`x-fontawesome/house` there today, and through the in-band `names`
capability once it is built.

A set entry in `GET /v1/fonts` and `/v1/fonts/sets` gains `source`
(`shipped` or `installed`) and, for an installed set, `install`:

```json
{"id": "x-fontawesome", "owner": "x", "scope": "terminal-local",
 "version": "<sha-256 of its allocation listing>", "count": 2052,
 "faces": ["x-fontawesome"], "source": "installed",
 "install": {"scope": "user", "origin": "catalogue", "verified": true,
             "release": "6.7.2", "licence": "OFL-1.1 AND CC-BY-4.0",
             "glyphs": 2052, "bytes": 1184000}}
```

Profiles keep `source: shipped|local`; `registry` faces are shipped.
A lookup in an installed set answers the same record as a shipped one:

```text
GET /v1/fonts/sets/x-fontawesome/house
  -> 200 {"set":"x-fontawesome","scope":"terminal-local","version":"<sha-256>",
          "name":"house","codepoints":["102000"]}
PUT /v1/fonts/glyphs/system/x-fontawesome      (catalogue, verified)
  X-Blit-Glyph-Parts: index=48211:<sha> signature=833:<sha> names=91022:<sha>
                      font=406412:<sha> licence=4401:<sha>
  -> 201 {"set":"x-fontawesome","scope":"system","origin":"catalogue",
          "verified":true,"allocated":2052}
```

- **Errors**, each with nothing changed: 400 `malformed_query` (one fixed
  body, as today); 403 `glyph_scope` (a frame-socket caller naming
  another scope); 409 `glyph_retired_name` (an update bringing back a
  name the set retired); 413 `glyph_too_large`; 422
  `glyph_signature_invalid`, `glyph_wrong_key`, `glyph_hash_mismatch`,
  `glyph_entry_missing`, `glyph_font_rejected` (with the worker's or
  validator's reason from a fixed list), `glyph_names_invalid`; 507
  `glyph_ledger_full`, `glyph_quota_exhausted`, `glyph_store_full`.
- **The Glyph Protocol.** `q ; cp=` for a ledger codepoint answers
  `status=system` in a session whose view holds a set drawing it, and
  `status=` empty otherwise (another user's, uninstalled, or never
  allocated). The in-band `names` capability, when built (ADR 0243 step
  3), answers from the session's view, so a program in a user's session
  finds that user's sets.
- `docs/api.md` ("Glyph sets", `/v1/fonts`), glyph sets §3.1 (the `x` row)
  and §9 (the ledger's block), and `docs/terminal.md` (`q` per view) change
  in the release that builds each part, not before.

### 9. Lifetime: uninstall, updates, retired names

- **Uninstall** removes the scope's copy (the store's directory for that
  set and scope) and nothing else: a user's removal touches only theirs.
  The ledger keeps every allocation. Afterwards, in that view, the set
  answers 410 `uninstalled` and its names 410 `uninstalled`; programs print
  their fallbacks. Cells already showing its codepoints draw the
  replacement glyph in that view (a view is about what a session may draw,
  and the set is gone from it); another scope that still has the same
  glyph keeps drawing it at its own codepoint. Reinstalling restores every
  name the ledger has for that scope to its codepoint.
- **Updates.** An import of a set id the scope already has is an update,
  applied whole or not at all. Per name: kept, the same codepoint, redrawn
  or not (a redraw drops the old picture from the caches, section 3, and
  scrollback shows the new one, as for Blit's own icons); new, a new
  codepoint; dropped, **retired** for good in that set and
  scope (410 `retired`, never reused, rule of 5d09c24b). An update that
  brings a retired name back is refused (409 `glyph_retired_name`): a
  publisher who means a new glyph names it anew. The version changes
  with every update.
- **Uid reuse.** Blit cannot know a uid was deleted and reissued; a new
  account on an old uid sees its predecessor's sets, as it would see its
  files. The administrator removes them over HTTP
  (`DELETE /v1/fonts/glyphs/users/{uid}/{set}`).

### 10. Licences and limits

- **Licences.** A catalogue glyph set must carry its licence part; the
  store keeps it beside the set; `install.licence` is the entry's SPDX
  `licence`, the text is served by `/licence`, and the pane shows both.
  The font is stored unmodified; the outlines are Blit's internal drawing
  form and are never served.
- **Limits** (stated as constants, reported by `GET /v1/fonts/glyphs`):
  64 installed sets per scope, 1,024 in all; 8,192 glyphs and 16,384
  names a set; font 16 MiB, names 4 MiB, outlines 16 MiB, licence 256 KiB;
  a store of 512 MiB for `system` and 64 MiB per uid; 8,192 lifetime
  allocations per uid; the ledger's 57,342. A set over a limit is refused
  whole.

### 11. What ships, what is optional

The owner asked whether Arabic, the player glyphs and the rest need to be
installed by default. The rule: **core is whatever must draw before a
network and a sign-in exist** -- the login screen, the desktop chrome Hibr
names from the catalogue -- **plus last-resort coverage** (Unifont).
Everything else is a candidate for optional. Measured (`assets/fonts`):

| Face | MB | Face | MB |
| --- | --- | --- | --- |
| NotoColorEmoji | 10.7 | Blit-Unifont16.psf | 1.8 |
| UnifontUpper | 5.9 | NotoSansSymbols2 | 1.2 |
| Unifont | 5.3 | NotoSansMath | 1.0 |
| Symbols Nerd Font Mono | 2.5 | BlitIcons | 0.9 |
| NotoEmojiText | 2.0 | NotoSansSymbols | 0.4 |
| text/ (seven fonts) | 1.8 | NotoNaskhArabic | 0.2 |

- **Every face Blit ships today stays shipped.** An upgrade never removes a
  face a machine has.
- **Arabic** costs 0.2 MB; without it Arabic falls to Unifont, cell by cell
  and unshaped, which a terminal that claims correct script rendering
  must not do by default.
- **Emoji** is the only large face, and programs print emoji routinely.
- **The player and chrome glyphs are code**, not a font: cell geometry
  drawn by `src/blit_player_glyphs.h` (ADR 0237) from U+101314, and in
  the catalogue's `cell_geometry`; they cannot leave the `blit` set
  without breaking the frozen catalogue and their codepoints.
- **No split packages.** The updater installs and signs exactly the `blit`
  and `blit-compositor` pair (ADR 0232, SR-2): a `blit-fonts-arabic`
  would ride neither, and would triple the deb, rpm and Arch packaging.
- **Installable sets are additive:** third-party icon sets, and an app's
  own glyph set travelling with that app (the natural first case: a music
  player's own art; the app prints its fallback when the set is absent).
- **Two kinds stay separate.** A Unicode face (a script, symbols) joins the
  stack at the standard codepoints, needs no ledger, and changes what every
  codepoint draws, which is ADR 0195's stack order to decide, not an
  install; it is not part of this record. A named private set needs the
  ledger, and is what this record installs.
- If the registry ever allowed a role with no face, `font_registry_gate`
  (which holds every role to a face) and every font gate would need a
  present and an absent state. Not needed now: nothing shipped becomes
  optional.

### 12. The pane (hibr-apps; a separate bundle)

A rows pane in its own file, `control-panel/blitglyphs.hibr`, declared at
top level, literally:

```sh
command -v cp_pane > /dev/null && cp_pane blit_glyphs "Blit Glyphs" "@blit:▦" hardware
```

with `blit_glyphs_rows` built from `cp_choice`, `cp_toggle` and
`cp_absent` rows; anything that changes the machine goes through a
request in a background job, as the Blit pane's do.

- **Phase 1, read-only, works today:** one row per text font (id, title,
  `source`), per face (`registry`: id, role, file) and per set (`id`,
  `owner`, `scope`, `version`, `count`), from `GET /v1/fonts` and
  `GET /v1/fonts/sets`; a set browsed by name, a page at a time
  (`GET /v1/fonts/sets/{set}?after=`), each name drawn by printing its
  codepoint; `cp_absent` when Blit does not answer.
- **Phase 2:** `source`, `install` (scope, origin, verified, release,
  licence), the ledger's use, Remove; user-scope removal over the frame
  socket.
- **Phase 3:** Install from the catalogue, and the `rescan` import of
  section 5.

## Consequences

- Any icon font can be installed without colliding with Nerd, Blit or
  another install, and named by `x-<set>/<name>` with the same lookup,
  listing, aliases, deprecations and retirements as shipped sets.
- Root parses no installed font. The cost is a worker binary, a system
  user, Blit's first seccomp filter, and an outline format with its own
  validator; installed glyphs are unhinted, monochrome or COLR v0.
- Codepoints of installed sets are this machine's and this view's; they do
  not survive a recording replayed elsewhere (glyph sets §5, as for
  `blit`). Portable sets remain the choice for output shown elsewhere.
- The ledger is finite and never reused: 57,342 glyphs over the machine's
  life, spent per scope. A few icon sets installed for the system use a
  few percent; each user installing their own copy spends again, which is
  the price of no leak between users; abuse is bounded per uid.
- `gpgv` is a new dependency unless hibr adds a natively verifiable
  signature.
- Blit stays independent of Hibr: the catalogue and the pane are clients;
  Blit reads no Hibr path and fetches nothing.

## Release plan

| Phase | Where | Releases |
| --- | --- | --- |
| 1. Read-only pane | hibr-apps only | none of Blit's |
| 2a. Store, ledger, outline format and validator, the raster's outline path, the worker (user, limits, namespace), `PUT`/`DELETE` for `system` with origin `own`, `source`/`install` in `/v1/fonts`, the `x` owner | Blit | 1-2 |
| 2b. Views: user scope, accepted only from the administrator over HTTP (`users/{uid}/{set}`, origin `own`); the frame socket's `GLYPHS sets`, `page`, `lookup` and `remove`; per-view lookups and `q`; shadowing; seccomp | Blit | 1 |
| 3. Catalogue origin: the pinned key, `gpgv` (or native Ed25519), the index and parts checks; the frame socket's `GLYPHS import`, for catalogue sets; the `glyphs` kind in `mkindex` and `catalogue.hibr` (hibr's); the pane's Install and `rescan` import | Blit, hibr, hibr-apps | 1 of Blit's |
| (b), if the owner says yes | Blit (`blit-glyph-extract`, the frame-socket upload of outlines) | 1 |

Roughly four or five Blit releases, each with its gates passing on the
release set before the next starts.

## Open questions

1. **The owner's:** case (b), a user's own unsigned font, extracted in the
   user's session and handed to Blit as outlines (section 6). Until they
   answer, user scope accepts catalogue sets only.
2. **hibr's:** can the catalogue's index also carry a detached raw Ed25519
   signature (`index.json.sig`) under a key Blit can pin, so Blit verifies
   with Monocypher and needs no `gpgv` (section 7)?
3. **hibr's:** can the catalogue's `auto` field (as in apps entries,
   `"auto": "apps/video"`) pull a glyph set in with the app or language
   that needs it?
4. **hibr's:** can a pane reach its session's frame socket (a desktop call
   that sends a `GLYPHS` request with descriptors), or should the desktop
   itself make the user-scope import on `rescan`? Until then a pane holding
   the bearer token imports over HTTP as the administrator
   (`users/{uid}/{set}`).

## Evidence

Nothing is built; this record is `proposed`. The gates each phase must
add, and pass, before the record is accepted:

- `glyph_outline_gate` (C, light): the validator refuses every malformed
  case (counts, offsets, open contours, coordinates out of range, trailing
  bytes, too many layers, points or glyphs) and a fuzzed corpus without a
  crash under ASan; outlines the worker extracts from the shipped Nerd
  face draw within a stated tolerance of FreeType drawing the face itself.
- `glyph_worker_gate`: a hostile-font corpus (truncated tables, huge
  counts, recursive composites, a CFF charstring loop, a TTC, WOFF2, a
  variable font, COLR v1, `sbix`) is refused with its reason and nothing
  changed; a worker that opens a file, a socket or forks is killed; the
  limits hold; the worker runs as `blit-glyphs` with no capabilities.
- `glyph_ledger_gate`: allocation in name order; a name after an update
  (redrawn or not) and after uninstall and reinstall keeps its codepoint;
  two scopes never share one; a redraw's new pixels drawn without a
  restart (CPU and GLES); no codepoint ever reassigned over thousands of
  random imports, updates and removals; a retired name refused on return;
  the per-uid quota and a full ledger answer 507 with nothing changed; the
  ledger survives a restart and a crash between append and rename.
- `glyph_install_gate` (HTTP and the frame socket, scratch store): import
  and remove in both scopes; a frame-socket caller cannot act for another
  uid; views (a user's set drawn in their session and not another's, in a
  CPU and a GLES frame, the replacement glyph there, `q` empty there);
  shadowing; the view checked before the cache; the swapped-file case (a
  hook replaces the staging copy after the checks, and the checked bytes
  are what is installed, as `update_pair_gate`'s `swapped`).
- `catalogue_verify_gate`: a bad signature, a signature by another key
  (with that key in a scratch keyring beside the pinned one, so the
  fingerprint check is what refuses), a truncated `.asc`, an entry missing
  from the index, a part whose hash differs, an index edited after signing
  -- each refused with nothing changed; the signed set installs.
- `glyph_sets_gate` extended: shipped and installed sets in one listing,
  one table, one grammar; `x-` ids; per-view answers; 410 `uninstalled`
  and `retired`. `glyph_protocol_gate` extended for `q` per view.
  `font_registry_gate` prints the same line as before.
- `package_artifact_gate`: the pinned catalogue key and its fingerprint,
  the worker binary, the system user in all three formats, and the store
  created `root:root 0700`.
- `documentation_gate` for this record and every document each phase
  changes.
