# ADR 0245: Installable glyph sets -- a root-owned store, a ledger of codepoints, per-user views

- Status: proposed
- Date: 2026-11-22; revised the same day with the owner's answers to its
  three open questions (hibr's proposal, approved by the owner 2026-11-22;
  "Decided (2026-11-22)" below)
- Scope: the font registry's set level (`assets/fonts/fonts.source.json`,
  `src/blit_glyph_sets.h`), the raster's glyph stack (`src/blit_raster.c`),
  `/v1/fonts` and `/v1/fonts/sets`, the frame socket (ADR 0202), the Glyph
  Protocol's `q` answers (`src/blit_server.c`), the release signature code
  (`src/blit_release_signature.h`) and its signing tool
  (`tools/release/blit-sign.c`), a new root-owned store under
  `/var/lib/blit`, a setting naming root-owned directories of system sets,
  and clients outside Blit: a "Blit Glyphs" Control Panel pane, a `glyphs`
  kind in the hibr-apps catalogue, and hold's attaching clients, which
  import a user's sets into each display's Blit. Extends
  [ADR 0243](0243-glyph-multiplexing.md) (one registry, sets and names);
  follows [ADR 0232](0232-an-update-installs-blit-and-its-compositor-together.md)'s
  amendments on Blit's release key (SR-2) and on how root handles files it
  is handed (SR-10).

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
  once at start into one sorted table (18 sets, 11,662 names); the set
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
  process: the faces its package ships and the administrator's own text
  fonts, both in root-owned directories. An API bearer token is a root
  shell today (SR-9, the accepted LAN-testing exception); since 0.3.135 a
  session run as another user gets no API token, only its session's
  frame-socket credential, which only its own uid can use (SR-8, ADR 0202).
- **Release signatures** (since 0.3.133, SR-2). The update helper installs
  only a release whose statement -- `blit-release-signature 1`, then a
  line per package -- verifies as Ed25519 (RFC 8032, Monocypher, vendored)
  under one of `/usr/lib/blit/release-keys/*.pub`; the package that ships
  every face is signed by that key. Removing a key file revokes it.
  `src/blit_release_signature.h` holds the one statement, key format and
  verifier.
- **Displays.** A held Hibr desktop has one Blit session per display, and a
  display may be another machine's Blit. Hibr's console sends pictures
  through the frame socket unheld, and through hold to each attaching
  client held (ADR 0202).

Measured (2026-11-22, from `assets/fonts/nerd-glyphnames-v3.4.0.json` and
`assets/fonts/icons/catalogue.json`):

| Range | Drawn by Blit today |
| --- | --- |
| BMP private use, U+E000-U+F8FF (6,400) | 3,499, all Nerd |
| Font Awesome, U+F000-U+F2FF | 768 of 768 |
| Codicons, U+EA60-U+EC1E | 438 of 447 |
| IcoMoon and Fontello's default start, U+E000-U+E0FF | 51 of 256 |
| Plane 15, Nerd's Material Design, U+F0001-U+F1AF0 | 6,880 of 6,896 |
| Plane 16 | 898 Blit ids in U+101000-U+1014D8 (and the old range, U+100000-U+100494, during the transition) |

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
   codepoints. (The system sources of section 2 are not this: root-owned,
   read only by the import, copied and verified before anything is parsed,
   never drawn from.)
2. **Add an installed face to the stack at the font's own codepoints.**
   Refused: the table above. Every popular icon font collides with Nerd.
3. **Rewrite the font's cmap to Blit's codepoints.** Refused: a modified
   font is a derivative (the OFL's Reserved Font Name rule; other licences
   differ). A remap table outside the font (section 6) reaches the same
   glyphs with the font unmodified.
4. **Root never parses an installed font** (this record's first draft):
   a sandboxed worker (`blit-glyph-worker`, a `blit-glyphs` system user,
   seccomp, a network namespace) extracts outlines in a new
   `blit-glyph-outlines` format that root validates and draws, and the
   catalogue's OpenPGP index signature is checked with `gpgv` under a
   catalogue key Blit pins. Superseded (the owner, 2026-11-22): Blit's
   first sandbox, a second binary, a system user in three package formats,
   an outline format with its own validator and fuzzing, a new dependency
   and a second key to pin -- all to keep root from parsing fonts that,
   once signed by Blit's own key, are as trusted as the faces it ships.
5. **A catalogue Ed25519 key pinned in Blit** (`index.json.sig`).
   Refused: a second key set to pin, rotate and revoke, and root would
   parse what the catalogue's publisher vouches for, not Blit's.
6. **Blit allocates codepoints, sets keep their owner's scope, and root
   loads a set only when Blit's release key signed it** (or the
   administrator imported it as their own act). Taken (below).

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
| Installed, `/var/lib/blit/glyphs` | installed glyph sets: `system/<set>/` and `users/<uid>/<set>/`, each the parts as imported plus Blit's `record.json`; and `ledger` | **only** Blit's import (section 5) | fonts, each loaded only after its signature and hashes verify on the bytes read (sections 6, 7) |
| System sources, `glyph_system_dirs` (default `/var/lib/hibr/glyphs`) | signed sets for every session, one `<name>/` each | root (the catalogue's system-scope install, or the administrator) | sources only: copied into the store by the import (section 5), never drawn from, never read per request |

- The store is `root:root 0700`, its parts `0600`. The server refuses to
  use it (and says why in the diagnostics log; the shipped sets are still
  served) unless the store, `/var/lib/blit` and every parent are
  directories owned by root and writable by no one else -- the check
  `private_directory_ready` makes of its own directory in
  `src/blit_update_helper.c`, extended to every parent. It is a
  sibling of `/var/lib/blit/updates`, which is `root:blit 0770` and must
  never be its parent (SR-3, SR-10).
- **System sources.** `glyph_system_dirs` is a list (empty turns it off);
  Blit's code names no Hibr path, only this default. Each directory and
  every parent must pass the same ownership check, or it is skipped and
  the diagnostics log says why. Only signed sets are taken from it (an
  `own` set comes only over HTTP). It is read at start and on a rescan
  (section 5), never per request and never at draw time; a set in it is
  copied into staging and verified before any of it is parsed, so a
  half-copied set fails its hashes and is taken at the next rescan.
- **Local holds no glyph sets.** One import means one writer of the ledger
  and one validator; a folder re-read on every request would also see a
  half-copied set. An administrator installs their own set with the
  import, as `system` (section 5, origin `own`).
- `BLIT_GLYPH_STORE` overrides the store for gates only, as
  `BLIT_FONT_LOCAL_ROOT` does for the local root.

### 3. Codepoints: allocated by Blit, from a ledger never reused

- **The block.** Installed glyphs take codepoints from
  **U+102000-U+10FFFD** (57,342 codepoints). U+101000-U+101FFF stays
  Blit's own set's room to grow (its 898 ids span U+101000-U+1014D8).
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
  Installed set ids are `x-<name>`, 1-30 octets of the set-id grammar (`x-`
  plus at most 30 is the 32-octet cap): for a signed set, the `<name>` its
  signed manifest states (section 7), which is the catalogue entry's
  (`glyphs/<name>`), so a signed set installs under one id only; for an
  `own` set, the administrator's choice. An `x-` id can never
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
  an import or removal. Each installed set's record in the store
  (`record.json`; not the signed manifest of section 7) uses the **same
  schema** as a registry `sets` entry (`{owner: "x", scope:
  "terminal-local", faces, names: {file, format}, version,
  retired_names}`) plus `install` (section 8) and the parts' SHA-256 as
  imported, so one reader serves both.
- **The same checks.** The loader's refusals -- a name outside the
  grammar, a duplicate, a codepoint in the registration block, a file over
  `BLIT_GLYPH_FILE_MAX` -- are the import's refusals, by the same function;
  `build-font-registry.py`'s checks of the shipped sources stay where they
  are, and a gate holds the two to the same cases.
- **Names file format `blit-glyph-names`** (JSON, at most 4 MiB): the
  publisher's `release`, and `glyphs`, each name mapped to the font's own
  codepoint (`"house": {"code": "f015"}`), with optional `aliases`,
  `deprecated` (with `replaced_by`). `code` is how the remap table
  (section 6) reaches the font's glyph; it is never a codepoint Blit
  draws. Every glyph is one cell, drawn fitted as Nerd's are. A glyph
  across several cells is step 5 of ADR 0243 (OSC 66 scales any codepoint
  without spending one per cell), not tiles: tiles would spend a ledger
  codepoint per cell and need a slicing rule per glyph.

### 5. Installing is a Blit API; Blit never depends on Hibr

- **Upload, never fetch.** Blit is handed the bytes; it never fetches a
  catalogue URL. ADR 0232 refused a network fetch as root (its option 2),
  Blit has no HTTP client or TLS, and the catalogue already fetched,
  verified and placed the files as their owner. Blit verifies again
  itself, on its own copy, under its own key (section 7).
- **Three ways in, one import.**
  - **HTTP, the bearer token.** `PUT`/`DELETE /v1/fonts/glyphs/system/{set}`,
    and, as an administrator's act, `/v1/fonts/glyphs/users/{uid}/{set}`.
    The bearer token is root's (SR-9), so it may act for any scope; it
    names no user. A body carries its parts in order, framed by one header
    as `PUT /v1/control/update` frames its packages:
    `X-Blit-Glyph-Parts: manifest=<length>:<sha256> signature=...
    names=... font=... licence=...`, summing to `Content-Length`; an `own`
    set (section 6) has no `manifest` or `signature`. The header's hashes
    check the transport; the trust check is the signed manifest's.
  - **The frame socket, the session's uid.** A session's own channel
    (ADR 0202; `SO_PEERCRED` and the session's frame credential) gains a
    `GLYPHS` request: `import <set>` with the parts passed as memfds
    (`SCM_RIGHTS`), sealed against shrinking, growing and writing and read
    with `pread` as ADR 0202's frames are; `remove <set>`; `rescan`; and
    the session's own reads (section 8). The uid is the peer's, never a
    value the request states. Here only signed sets, only into the peer's
    own uid's scope. Any holder of the session's frame credential may ask
    (including sessions run as another user, which hold no API token,
    SR-8); that only hold's clients and the console do is Hibr's rule
    (hibr's ADR 0050), not a check Blit makes, and needs none: a caller
    can import only what Blit's key signed, only for itself, within its
    quota.
  - **System sources, Blit itself.** As root, from `glyph_system_dirs`
    (section 2), at start and on a rescan: a frame-socket `GLYPHS rescan`
    from any session, or `POST /v1/fonts/glyphs/rescan`; one at a time,
    requests during one folded into one more after it. Per `<name>/`: a
    set whose manifest bytes equal the stored one's is unchanged and
    skipped; a new or changed one is imported as `system` with
    `install.via: "dir"`; a `dir` set whose directory is gone from a source
    that was read is uninstalled (section 9). A source that fails the
    ownership check, cannot be read or is taken out of the setting
    uninstalls nothing; its sets stay until the administrator removes them. A set imported over HTTP (`via: "http"`) is
    never replaced or removed by a rescan; a source with its id is skipped
    and logged.
- **Copy, verify, then parse, one object.** Every part is copied into a
  root-owned `0700` staging directory inside the store (same filesystem,
  so the final step is a `rename`), each by one read, never by the
  caller's path (as `copy_staged` in `src/blit_update_helper.c` does since
  SR-10). The signature and hashes are checked on the copy (section 7);
  only then is the names file parsed and the font opened (section 6), from
  the same bytes; the set is moved into `system/<set>/` or
  `users/<uid>/<set>/`. Nothing is visible to any view until the ledger
  record is durable and the rename is done; a failure removes the staging
  copy and changes nothing.
- **Origins.**
  - **(a) `catalogue`, decided:** a set signed by Blit's release key
    (section 7), as the hibr-apps catalogue carries them. Either scope:
    system from HTTP or the system sources, user from the frame socket (or
    the administrator over HTTP).
  - **(b) `own`, a user's own unsigned font: declined by the owner (2026-11-22)** (section 6).
  - **`own` as `system`:** the administrator's own font, unsigned, over
    HTTP only, `system` only, reported `verified: false` (section 6).
- **How Hibr's catalogue fits.** A bundle install only places files
  (verified under hibr's own OpenPGP index signature, then
  `mkdir`/`rm`/`mv`) and tells running desktops `rescan`; it runs no
  install step. User scope places `$XDG_DATA_HOME/hibr/glyphs/<name>/`,
  system scope `$HIBR_SYSDIR/glyphs/<name>/` (`/var/lib/hibr/glyphs`,
  `glyph_system_dirs`' default). Blit learns of nothing by itself: system
  sets reach it from its configured source on a rescan, user sets through
  each display's connection (below).
- **Import through each display's connection (decided, 2026-11-22).**
  - A held desktop has one Blit session per display, and displays may be
    remote, so a set imported once into one Blit is missing from the
    others.
  - Each of hold's attaching clients (one per display, already holding
    that session's token on its frame connection) imports the user's sets
    into its own Blit over that frame connection, the files as sealed
    memfds, the uid from `SO_PEERCRED`: at attach, and when the desktop
    relays `rescan` ("glyph sets changed") through hold to every client.
    It imports what that Blit lacks or holds at another release, removes
    the sets it imported (`via: "frame"`) that the user no longer has --
    never one the administrator imported over HTTP -- and sends `GLYPHS rescan` so that Blit
    re-reads its system sources. An unheld desktop's console does the same
    over its direct connection.
  - The frame socket is local (ADR 0202), so each client imports into the
    Blit on its own machine; how a set's files reach a client on another
    machine is hold's (hibr's). Blit does not rely on that path: it checks
    the signature itself.
  - In hibr's ADR 0050 an app's process never talks to Blit; the desktop
    is the one place platform events go. So no app and no pane imports.
  - Each Blit reports, per display, which sets the session's view holds
    with their versions and codepoints (`GLYPHS sets`, section 8; hibr
    #609, "the display reports what it can draw"), and the desktop maps
    glyph ids per display, keyed by set id, version and display as glyph
    sets §5 requires.
- **The `glyphs` catalogue kind: decided (the owner, 2026-11-22).** The
  14th kind, `glyphs/<name>`, is approved, though it touches the parked
  epic #561. An entry is a bundle of parts: the names file as the entry's
  bundle; the font, the licence, `glyphs.manifest` and `glyphs.sig` as
  `parts`, each with its own `sha256` (hibr's index check).
- **Sets travel with what needs them (decided, 2026-11-22; hibr's side).**
  An app or a lang that needs a set says `# needs: glyphs/<name>`; the
  catalogue installs the set with it, marks it `auto` in the catalogue's
  own install ledger (not Blit's codepoint ledger), and `purge` removes it
  when nothing installed needs it. Hibr builds this with the kind; Blit
  sees only the imports and removals that follow.

### 6. Root loads a signed set as it loads a shipped face

- **Why that is enough (the owner, 2026-11-22).** A set signed by Blit's
  release key (section 7) is trusted exactly as the faces Blit ships: the
  package that carries them is signed by the same key (ADR 0232, SR-2),
  and root already parses those with FreeType. So root loads a verified
  set's font with FreeType, in process, as a shipped face. There is no
  worker, no `blit-glyphs` user, no seccomp filter, no network namespace
  and no `blit-glyph-outlines` format (option 4, superseded).
- **The bytes hashed are the bytes parsed.** At import, at start and at
  each rebuild of the table (section 4), Blit reads each part of a stored
  set once into memory, verifies on those bytes (section 7, or for `own`
  the hashes in `record.json`), and hands FreeType that buffer
  (`FT_New_Memory_Face`), kept as long as the face. Nothing is opened
  again by path. Loaded fonts stay resident, bounded by the store's limits
  (section 10).
- **Re-verified at every start.** Removing a key file from
  `/usr/lib/blit/release-keys` revokes the sets it signed as it revokes
  releases: such a set is not loaded, stays in the store, and is reported
  with `install.state: "refused"` and its reason until it is removed or a
  copy signed by a current key is imported.
- **Cheap checks first.** Before FreeType sees a byte: every part's size
  (section 10), the signature, the hashes, and the names file parsed by
  the bounded reader (at most 8,192 glyphs). At open, before any glyph is
  loaded: one face (`num_faces` 1), scalable, at most 65,535 glyphs, and
  every name's `code` mapped by the cmap. Any failure refuses the set
  whole with its reason (`glyph_font_rejected`).
- **Drawn as Nerd glyphs are,** by `draw_fitted_glyph`
  (`src/blit_raster.c`), fitted to the cell, unchanged: an installed set
  gets exactly what a shipped face gets from that path and no path of its
  own.
- **The remap table: ledger codepoint to the font's glyph.** The face never
  joins the stack at its own codepoints (the Context's table, option 2).
  Per set and scope, at import and at each rebuild, off the render path,
  Blit builds one table from the ledger and the names file: each ledger
  codepoint, its name, the name's `code`, and the glyph index
  `FT_Get_Char_Index` gives for that code in the set's face; sorted by
  codepoint. A name whose `code` has no glyph refuses the set (above). The
  raster, given a codepoint in U+102000-U+10FFFD that the session's view
  holds (section 1), finds its entry by binary search and draws that face
  and glyph index; any other codepoint there is uncovered. The font's own
  cmap is read, never rewritten (option 3), and the font is drawn only
  through this table.
- **`own`: the administrator's unsigned font, accepted as their act.**
  Over HTTP only, into `system` only, never from the system sources or the
  frame socket; loaded like a local font after the same limits and
  checks, reported `verified: false` ("unverified, the administrator's" in
  the pane). Why this and not a signature:
  - The bearer token is root (SR-9). Whoever holds it can already place a
    font in `/usr/local/lib/blit/fonts/text`, which root parses unsigned
    on every `GET /v1/fonts`; refusing an unsigned set over the same
    token protects nothing.
  - Having the administrator sign with a key added to
    `/usr/lib/blit/release-keys` is refused: that directory is the
    updater's trust set, and a key in it could sign a Blit release, not
    only a glyph set. The manifest's label separates signatures, not
    keys. A separate directory of administrators' keys would be another
    key set to manage that guards nothing the token does not already
    grant.
  - At start an `own` set's parts are checked against the hashes Blit
    recorded at import; only root can write the store, so this notices a
    hand-edited store, not an attacker.
- **(b) The user's own unsigned font: declined (the owner, 2026-11-22).**
  A user installs glyph sets only from the catalogue, signed (case a); an
  administrator may import a set of their own into `system` over the
  authenticated API (origin `own`, unverified, above). No unprivileged
  path hands Blit glyph data that Blit's key did not sign, so there is no
  `blit-glyph-extract` command and the only non-root glyph input is a
  signed set. (Written up before the decision: the user's session would
  have extracted the font itself and handed root outlines.)

### 7. The glyph-set signature: Blit's release key (decided, 2026-11-22)

- **One key set with releases.** Each `glyphs/<name>` bundle carries
  `glyphs.manifest` and `glyphs.sig`: a raw Ed25519 signature (RFC 8032,
  Monocypher, `src/blit_release_signature.h`; no OpenPGP) by one
  of Blit's release keys, the set `/usr/lib/blit/release-keys/*.pub` the
  updater uses (ADR 0232's amendment for SR-2), over the manifest's exact
  bytes. No `gpgv`, no new `Depends`, no OpenPGP parser, no catalogue key
  pinned in Blit.
- **The manifest**, exactly:

  ```text
  blit-glyph-set-signature 1
  set <name> <version>
  font <sha256>
  names <sha256>
  licence <sha256>
  ```

  - Five lines in this order, each ending in one LF, nothing before or
    after; at most 512 bytes.
  - `<name>`: 1-30 octets of the set-id grammar, the set's id without
    `x-`. It must be the set being imported: the request's `{set}` or
    `<set>` without `x-`, or the system source's directory name.
  - `<version>`: the publisher's release, in the grammar the release
    statement's versions use (`blit_release_version_valid`: a digit
    first, then `0-9 A-Z a-z . + ~ - :`, at most 127 characters). It must
    equal the names file's `release`, checked once that file is parsed
    (`glyph_names_invalid` otherwise), so `release` follows the same rule:
    a publisher's tag such as Nerd's `v3.4.0` is written `3.4.0`.
  - `<sha256>`: 64 lowercase hex digits, the SHA-256 of that part's bytes.
  - **Domain separation.** The first line is the manifest's own label,
    distinct from the release statement's `blit-release-signature 1`, and
    each verifier requires its own label first: a glyph-set signature can
    never pass as a release signature, nor a release signature as a
    glyph-set one, though one key makes both.
  - `glyphs.sig` is the signature's 64 bytes, raw: no hex, no envelope.
    The release key files and the verifier are the ones releases use; only
    the release's transport form (128 hex digits) differs.
- **Order, all on Blit's root-owned copy:**
  1. the manifest within its 512 bytes, the signature exactly 64 bytes;
  2. the signature verifies over the manifest's bytes under one of the
     release keys (`glyph_signature_invalid`; Ed25519 cannot tell a bad
     signature from another key's, so both are this);
  3. the manifest's grammar: its label first, its name and version as
     above (`glyph_manifest_invalid`);
  4. each part present (`glyph_part_missing`) and its SHA-256 over Blit's
     copy equal to the manifest's line (`glyph_hash_mismatch`);
  5. only then is anything parsed: the names file, then the font
     (section 6).
- **Signing.** `tools/release/blit-sign` gains a glyph-set mode: it
  hashes the three files, writes the manifest itself and signs it, so no
  hand-written manifest is ever signed.
- **Cost, stated.** Only the holder of Blit's release key -- the owner,
  through the maintainer -- can publish a glyph set. That is the owner's
  "signed catalogue only", and it is right for what root parses: a
  third-party icon font reaches root only after Blit's maintainer chose to
  sign it.
- **Two independent checks, each by its consumer's own key.** hibr's
  OpenPGP index signature (the hibr-apt repository key) still protects
  the catalogue's install path: the files placed are the ones the
  catalogue published. Blit's signature decides what root parses. Neither
  trusts the other's.
- **Rotation** follows releases: a release adds the next key while the
  current one signs; sets are re-signed under the new key before an old
  key file is removed, which revokes what it alone signed (section 6).

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
POST /v1/fonts/glyphs/rescan            re-read the system sources
frame socket (the peer's uid and session; no API token needed):
  GLYPHS sets                     the session's view with versions and ranges, as GET /v1/fonts/sets
  GLYPHS page <set> [<after>]     a page, as GET /v1/fonts/sets/{set}
  GLYPHS lookup <set>/<name>      a record, as GET /v1/fonts/sets/{set}/{name}
  GLYPHS import <set> | remove <set>     the peer's own uid's scope, signed sets only
  GLYPHS rescan                   re-read the system sources
```

The frame-socket answers are the same JSON bodies, from the same function,
so a program in a user's session -- which has no API token -- resolves
`x-fontawesome/house` there today, and through the in-band `names`
capability once it is built; and hold's client reports its display's sets
from `GLYPHS sets` (section 5).

A set entry in `GET /v1/fonts` and `/v1/fonts/sets` gains `source`
(`shipped` or `installed`) and, for an installed set, `install`:

```json
{"id": "x-fontawesome", "owner": "x", "scope": "terminal-local",
 "version": "<sha-256 of its allocation listing>", "count": 2052,
 "faces": ["x-fontawesome"], "source": "installed",
 "install": {"scope": "user", "origin": "catalogue", "via": "frame",
             "verified": true, "state": "loaded",
             "release": "6.7.2", "licence": "OFL-1.1 AND CC-BY-4.0",
             "glyphs": 2052, "bytes": 1184000}}
```

`via` is `http`, `frame` or `dir`; `state` is `loaded` or `refused` (with
`reason`, section 6). Profiles keep `source: shipped|local`; `registry`
faces are shipped. A lookup in an installed set answers the same record as
a shipped one:

```text
GET /v1/fonts/sets/x-fontawesome/house
  -> 200 {"set":"x-fontawesome","scope":"terminal-local","version":"<sha-256>",
          "name":"house","codepoints":["102000"]}
PUT /v1/fonts/glyphs/system/x-fontawesome      (signed)
  X-Blit-Glyph-Parts: manifest=263:<sha> signature=64:<sha> names=91022:<sha>
                      font=406412:<sha> licence=4401:<sha>
  -> 201 {"set":"x-fontawesome","scope":"system","origin":"catalogue",
          "verified":true,"allocated":2052}
PUT /v1/fonts/glyphs/system/x-myicons          (the administrator's own)
  X-Blit-Glyph-Parts: names=2210:<sha> font=31840:<sha> licence=1088:<sha>
  -> 201 {"set":"x-myicons","scope":"system","origin":"own",
          "verified":false,"allocated":40}
```

- **Errors**, each with nothing changed: 400 `malformed_query` (one fixed
  body, as today); 403 `glyph_scope` (a frame-socket caller naming
  another scope, a `system` import or an `own` set over the frame
  socket); 409 `glyph_retired_name` (an update bringing back a name the
  set retired); 413 `glyph_too_large`; 422 `glyph_signature_invalid`,
  `glyph_manifest_invalid`, `glyph_part_missing`, `glyph_hash_mismatch`,
  `glyph_font_rejected` (with its reason from a fixed list),
  `glyph_names_invalid`; 507 `glyph_ledger_full`,
  `glyph_quota_exhausted`, `glyph_store_full`.
- **The Glyph Protocol.** `q ; cp=` for a ledger codepoint answers
  `status=system` in a session whose view holds a set drawing it, and
  `status=` empty otherwise (another user's, uninstalled, refused or never
  allocated). The in-band `names` capability, when built (ADR 0243 step
  3), answers from the session's view, so a program in a user's session
  finds that user's sets.
- `docs/api.md` ("Glyph sets", `/v1/fonts`), glyph sets §3.1 (the `x` row)
  and §9 (the ledger's block), `docs/terminal.md` (`q` per view) and the
  settings (`glyph_system_dirs`, `glyph_user_quota`) change in the release
  that builds each part, not before.

### 9. Lifetime: uninstall, updates, retired names

- **Uninstall** removes the scope's copy (the store's directory for that
  set and scope) and nothing else: a user's removal touches only theirs.
  Who removes: for a user set imported over the frame socket, hold's
  client or the console when the user no longer has it (`GLYPHS remove`);
  for one the administrator imported, the administrator over HTTP; for
  a `dir` set, the rescan that finds its directory gone; for an `http`
  set, `DELETE`. The ledger keeps every allocation. Afterwards, in that
  view, the set answers 410 `uninstalled` and its names 410
  `uninstalled`; programs print their fallbacks. Cells already showing its
  codepoints draw the replacement glyph in that view (a view is about what
  a session may draw, and the set is gone from it); another scope that
  still has the same glyph keeps drawing it at its own codepoint.
  Reinstalling restores every name the ledger has for that scope to its
  codepoint.
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
  The font is stored and loaded unmodified (its cmap read, never
  rewritten) and is never served.
- **Limits** (stated as constants, reported by `GET /v1/fonts/glyphs`):
  64 installed sets per scope, 1,024 in all; 8,192 glyphs and 16,384
  names a set; font 16 MiB, one face of at most 65,535 glyphs; names
  4 MiB; licence 256 KiB; manifest 512 bytes; signature 64 bytes; a
  store of 512 MiB for `system` and 64 MiB per uid (which also bounds the
  fonts held in memory); 8,192 lifetime allocations per uid; the ledger's
  57,342. A set over a limit is refused whole.

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
  player's own art, pulled in by its `# needs:`, section 5; the app prints
  its fallback when the set is absent).
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

- **The icon** is valid as written: the form is `@<blit id>:<fallback>`,
  and Blit's catalogue has a glyph with id `blit` (U+101000, Blit's logo;
  `assets/fonts/icons/catalogue.json`), with `▦` printed where Blit is
  absent. The group `hardware` is hibr's to confirm against its merged
  tree.
- **Read-only (decided, 2026-11-22).** `blit_glyphs_rows` is built from
  `cp_choice`, `cp_toggle` and `cp_absent` rows and changes nothing:
  installing and removing are the catalogue's, importing is hold's
  clients', the console's and Blit's own (section 5).
- **Phase 1, works today:** one row per text font (id, title, `source`),
  per face (`registry`: id, role, file) and per set (`id`, `owner`,
  `scope`, `version`, `count`), from `GET /v1/fonts` and
  `GET /v1/fonts/sets`; a set browsed by name, a page at a time
  (`GET /v1/fonts/sets/{set}?after=`), each name drawn by printing its
  codepoint; `cp_absent` when Blit does not answer.
- **Phase 2:** `source` and `install` (scope, origin, verified, state,
  release, licence) and the ledger's use, from the pane's own display's
  Blit, once Blit's phase 2 ships.
- **Phase 3:** the same per display: which sets are installed (in the
  catalogue) and imported (in each display's Blit, from the desktop's
  per-display report).

## Consequences

- Any icon font can be installed without colliding with Nerd, Blit or
  another install, and named by `x-<set>/<name>` with the same lookup,
  listing, aliases, deprecations and retirements as shipped sets.
- Root parses an installed font with FreeType, as it parses a shipped
  face, but only one Blit's release key signed, or the administrator's
  own over a token that is root anyway. Nothing new to build for it: no
  worker, system user, seccomp filter, outline format, `gpgv` or
  catalogue key; one key set and one verifier serve releases and glyph
  sets, kept apart by their labels.
- Only the holder of Blit's release key can publish a glyph set; every
  third-party set waits on the maintainer's signature. Removing a key
  revokes its sets at the next start.
- Installed glyphs are drawn by the same fitted path as Nerd's, and get
  whatever it draws for shipped faces.
- Codepoints of installed sets are this machine's and this view's; they do
  not survive a recording replayed elsewhere (glyph sets §5, as for
  `blit`). Portable sets remain the choice for output shown elsewhere.
- Each display's Blit holds its own copy, imported by that display's hold
  client; a remote display's codepoints differ, so the desktop maps glyph
  ids per display.
- The ledger is finite and never reused: 57,342 glyphs over the machine's
  life, spent per scope. A few icon sets installed for the system use a
  few percent; each user installing their own copy spends again, which is
  the price of no leak between users; abuse is bounded per uid.
- Blit stays independent of Hibr: the catalogue, hold's clients and the
  pane are clients; Blit fetches nothing, and the one Hibr path it reads
  is a setting's default, a root-owned source it copies from.

## Release plan

| Phase | Where | Releases |
| --- | --- | --- |
| 1. Read-only pane | hibr-apps only | none of Blit's |
| 2a. Store and ledger; the glyph-set manifest, its check in `src/blit_release_signature.h` and `blit-sign`'s glyph-set mode; root loading a verified set and the remap table; `PUT`/`DELETE` over HTTP for `system` (signed, and `own`); `source`/`install` in `/v1/fonts`; the `x` owner | Blit | 1 |
| 2b. Views: user scope, per-view lookups and `q`, shadowing; `users/{uid}/{set}` over HTTP | Blit | 1 |
| 2c. The frame socket's `GLYPHS` (`sets` with versions and ranges, `page`, `lookup`, `import` of sealed memfds, `remove`, `rescan`); `glyph_system_dirs`, the rescan and `POST /v1/fonts/glyphs/rescan` | Blit | 1 |
| 3. The `glyphs` kind and `# needs:` in `mkindex` and `catalogue.hibr` (hibr's); hold's clients' and the console's import at attach and on `rescan`, and the per-display report (hibr's); the pane's per-display rows (hibr-apps) | hibr, hibr-apps | none of Blit's |

Probably three Blit releases (2a, 2b, 2c; the first draft had four, one of
them a worker), each with its gates passing on the release set before the
next starts.

## Decided (2026-11-22)

hibr's proposal, approved by the owner:

1. **Signature:** Blit's own release key signs each glyph set over a
   labelled manifest (section 7); no catalogue key, no `gpgv`. A signed
   set is trusted as shipped faces are and loaded with FreeType by root
   (section 6); the worker and the outline format are dropped (option 4).
   This revision's choice within it: the administrator's unsigned `own`
   set is accepted as their act, `verified: false` (section 6).
2. **Auto:** `# needs: glyphs/<name>` in the app or lang; the catalogue
   installs the set, marks it `auto`, `purge` removes it (hibr's;
   section 5).
3. **Import:** through each display's connection, by hold's clients (or
   the unheld console); system sets from `glyph_system_dirs` by Blit
   itself; each Blit reports per display what it holds; the pane stays
   read-only (sections 5, 12).

## Open questions

None. The last one was decided (the owner, through the hibr session,
2026-11-22): **show what's missing.** A remote display's Blit holds its
own machine's system sets, not the desktop host's; it reports the host's
system sets missing and programs draw their fallbacks there. Hold's client
imports nothing under the user's scope for them and spends no quota.

## Evidence

Nothing is built; this record is `proposed`. The gates each phase must
add, and pass, before the record is accepted:

- `glyph_ledger_gate`: allocation in name order; a name after an update
  (redrawn or not) and after uninstall and reinstall keeps its codepoint;
  two scopes never share one; a redraw's new pixels drawn without a
  restart (CPU and GLES); no codepoint ever reassigned over thousands of
  random imports, updates and removals; a retired name refused on return;
  the per-uid quota and a full ledger answer 507 with nothing changed; the
  ledger survives a restart and a crash between append and rename.
- `glyph_signature_gate` (was `catalogue_verify_gate`), each refused with
  nothing changed and nothing parsed (FreeType and the names reader never
  called): a bad signature; a valid signature by a key not in the release
  keys; the release statement and its genuine release signature offered
  as a glyph manifest; a manifest naming another set or another version;
  the manifest's grammar (a CR, a byte after the last LF, upper-case hex,
  a missing or reordered line, over 512 bytes); a part whose hash
  differs; a part swapped between hash and read (a hook replaces the
  staging file after it is hashed, and the hashed bytes are what
  FreeType opens, as `update_pair_gate`'s `swapped`); a missing part; a
  stored set whose key file is removed, not loaded after a restart and
  reported `refused`. The signed set installs. `release_signature_gate`
  gains the reverse: a glyph manifest's genuine signature offered to the
  update helper as a release signature is refused.
- `glyph_install_gate` (HTTP and the frame socket, scratch store): import
  and remove in both scopes; a frame-socket caller cannot act for another
  uid, import into `system` or import an `own` set; an unsealed memfd is
  refused; the system sources (new, changed, unchanged and skipped, gone
  and uninstalled, an `http` set untouched, a directory failing the
  ownership check skipped); `own` imported and reported unverified; the
  font checks (a collection, not scalable, too many glyphs, a `code` the
  cmap lacks, over size) refused before any glyph loads; each name drawn
  at its ledger codepoint matches FreeType drawing the font at its own
  `code` through the same fitted path; views (a user's set drawn in their
  session and not another's, in a CPU and a GLES frame, the replacement
  glyph there, `q` empty there); shadowing; the view checked before the
  cache.
- `glyph_sets_gate` extended: shipped and installed sets in one listing,
  one table, one grammar; `x-` ids; per-view answers; 410 `uninstalled`
  and `retired`. `glyph_protocol_gate` extended for `q` per view.
  `font_registry_gate` prints the same line as before.
- `package_artifact_gate`: the store created `root:root 0700`; no new
  binary, system user, key file or `Depends`.
- `documentation_gate` for this record and every document each phase
  changes.
