# Glyph sets: looking a glyph up by set and name

> **Status: DRAFT; the set level is built, the in-band capability is
> not.** From 0.3.134 Blit's registry has the `sets` level of section 3.2
> and serves its sets, names, aliases, deprecations and retirements over
> HTTP (`GET /v1/fonts/sets`, [API guide](../api.md#glyph-sets);
> ADR 0243, "Step 3's set level as built"), by section 4's grammar and
> section 6's lifetime rules. Nothing in band is answered: Blit does not
> list `names`, and `q ; name=` and `n` are not implemented. Step 2 of
> ADR 0243 is built (0.3.133): Blit answers the Glyph Protocol's own `s`
> (`fmt=` empty, no `cap`) and `q ; cp=`, following section 9 below and
> section 8's no-echo and fixed-alphabet rules
> ([terminal reference](../terminal.md#glyph-protocol)).
> This document is the proposal behind step 3 of
> [ADR 0243](../adr/0243-glyph-multiplexing.md). It is written as an
> optional capability, `names`, of the Glyph Protocol, to be offered to
> that protocol's maintainers before anything is built; it has not been
> offered yet. If they decline, Appendix A gives the same verbs under a
> Blit-numbered OSC. The wire format may change until one of the two is
> agreed and a conformance probe exists.

This document is meant to be enough on its own: a terminal emulator can
implement the capability from it, and a program can use it from it. The
key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in
RFC 2119.

## 1. What it is for

Terminals draw icon sets at private-use codepoints, and programs have to
know those codepoints at build time. A program that wants Blit's
`bluetooth` icon has to know it is U+101006, and that it was U+100006
until 0.3.130. A program that wants Font Awesome's `bluetooth` from Nerd
Fonts has to know it is U+F293, and that Material Design's `bluetooth`
is a different glyph at U+F00AF. Names alone are ambiguous: 11 of Blit's
icon ids are also Nerd Fonts names in other sets. Two are spelled the
same (`bluetooth`, in Font Awesome and in Material Design; `power`, in
IEC and in Material Design); nine differ only by Nerd's `_` for Blit's
`-` (`battery-0` to `battery-4`, `phone-off`, `phone-ring`,
`phone-classic-off`, `wifi-off`).

This capability lets a program ask the terminal, once per session:

- which glyph sets it draws;
- the codepoint(s) of a glyph, by **set and name**, with the number of
  cells it takes;
- the names in a set, a page at a time, for pickers.

The program then prints ordinary codepoints. Nothing about how text is
stored, copied or recorded changes. Section 5 says what that means across
multiplexers, recordings and terminal updates.

## 2. Terms

- **Glyph:** one drawn picture with a meaning, such as Font Awesome's
  Bluetooth rune. A glyph is printed as one codepoint, or as a sequence
  (an emoji ZWJ sequence).
- **Set:** a published table of names for glyphs, with one owner and a
  version. A set is **not a face** (a font file): Nerd Fonts' symbols
  face holds 17 sets, and one set can be drawn from several faces. It is
  also not the `set` field of Blit's icon catalogue, which names a window
  style (`chalk`) and is unrelated.
- **Name:** a glyph's name within one set.
- **Address:** `set/name`, the only way a glyph is named in this protocol.
- **Owner token:** the first part of a set id, naming who publishes the
  set's names (`nerd`, `emoji`, `blit`). Owner tokens are registered in
  this document's table (section 3.1).
- **Portable set:** a set whose codepoints are fixed by its owner's
  published table, so every terminal serving the same set version prints
  the same codepoints (Nerd Fonts, Unicode emoji).
- **Terminal-local set:** a set whose codepoints are chosen by one
  terminal (Blit's own icons). Another terminal may draw something else,
  or nothing, at those codepoints.
- **Set version:** a token that changes whenever the set's table changes.

## 3. Sets

### 3.1 Registered owners and sets

A set id starts with its owner token. A terminal MUST NOT serve a set
under an owner token that is not in this table, and MUST serve a
registered set exactly as its names source defines it at the version it
reports. Registering a new owner token is a change to this table.

| Owner | Set ids | Names source | Name rule | Scope | Version |
| --- | --- | --- | --- | --- | --- |
| `nerd` | `nerd-cod`, `nerd-custom`, `nerd-dev`, `nerd-extra`, `nerd-fa`, `nerd-fae`, `nerd-iec`, `nerd-indent`, `nerd-indentation`, `nerd-linux`, `nerd-md`, `nerd-oct`, `nerd-pl`, `nerd-ple`, `nerd-pom`, `nerd-seti`, `nerd-weather` | Nerd Fonts' `glyphnames.json` for the release | the Nerd name `<prefix>-<rest>` is the address `nerd-<prefix>/<rest>`, unchanged otherwise | portable | the Nerd Fonts release, `3.4.0` |
| `emoji` | `emoji` | Unicode's `emoji-test.txt`, entries marked `fully-qualified` or `component` | section 3.3 | portable | the Emoji version, `18.0` |
| `blit` | `blit` | Blit's icon catalogue, its `icons`, `text_glyphs` and `cell_geometry` entries, and the retired names Blit's registry carries | the entry's `id`, unchanged | terminal-local | the catalogue's revision (SHA-256 of the packaged file) |

Blit's catalogue has 830 ids: 554 icons, 38 text glyphs and 238 cell
geometry pieces (window chrome). They share one namespace (every id is
unique across the three lists), Hibr already names all of them by id,
and all of them are drawn, so all 830 are in the one set `blit`. They
have 830 distinct codepoints, so the set has no aliases. One icon,
`hibr-pen`, also has a monochrome text form at U+270E (its `text_alias`);
a lookup answers its own codepoint, U+101001, and this version of the
protocol does not report the text form.

The catalogue's `retired` list keeps codepoints, not names (43 ranges,
345 codepoints). The ids that held them -- the 16 large phone icons, the
288 colourways of non-folder file kinds and the 35 Slate dark glyphs --
are in the catalogue's history, one at each retired codepoint, and are the
set's retired names (section 6): Blit's registry carries them, since the
catalogue's shape is frozen.

Nerd Fonts' sets are taken as the file spells them, including its two
indent prefixes (`indent` and `indentation`): a terminal implementing
the `nerd-*` sets needs only the published file and this rule, with no
table of its own. It need not ship the Nerd face: the set's names and
codepoints are the published table's, and `status` (section 7.2) says
whether anything in that terminal draws the codepoint, the user's own
font included. The version `v` versions the names table, not a font.

The 17 Nerd sets with Blit's pinned file hold 10,764 names for 10,386
codepoints. Twelve codepoints have names in two Nerd sets (for example
`nerd-custom/bazel` and `nerd-seti/bazel`, both U+E63A); those are two
addresses for one codepoint, not aliases (section 4.3).

Sets Blit draws but does not name -- the text fonts, Noto Sans Symbols,
Symbols 2 and Math, Unifont, Arabic -- draw standard Unicode, whose
characters already have names in the Unicode Character Database. A
`unicode` set may be registered later; it is not part of this version.

### 3.2 One source of truth in Blit

Blit's font registry, [fonts.source.json](../../assets/fonts/fonts.source.json),
gains one level, `sets`, so that the face, the names and the set id are
declared in one place. The catalogue
([icons/catalogue.json](../../assets/fonts/icons/catalogue.json)) is not
changed in any way: Hibr reads it by id, and its keys, ids and
`codepoint` field stay as they are.

As built in 0.3.134 (shortened):

```json
"sets": {
  "adr": "ADR 0243 step 3, the set level ...",
  "owners": ["blit", "emoji", "nerd"],
  "scopes": {"portable": "...", "terminal-local": "..."},
  "blit": {
    "owner": "blit",
    "scope": "terminal-local",
    "faces": ["blit-icons"],
    "names": {"file": "icons/catalogue.json", "format": "blit-catalogue",
              "lists": ["icons", "text_glyphs", "cell_geometry"]},
    "version": "sha256",
    "retired_names": {
      "file-asm-amber-left": {"codepoint": "1011B9", "replaced_by": ["file-asm-classic-left"]},
      "phone-off-large-left": {"codepoint": "101023"},
      "slate-dark-close": {"codepoint": "101280", "replaced_by": ["slate-light-close"]},
      ...
    }
  },
  "nerd": {
    "owner": "nerd",
    "scope": "portable",
    "faces": ["symbols-nerd-font-mono"],
    "names": {"file": "nerd-glyphnames-v3.4.0.json", "format": "nerd-glyphnames",
              "split": "prefix"},
    "version": "3.4.0"
  }
}
```

The `emoji` entry is to be added once its names file is in the package
(`assets/unicode` is not packaged today):

```json
"emoji": {
  "owner": "emoji",
  "scope": "portable",
  "faces": ["noto-color-emoji", "noto-emoji"],
  "names": {"file": "../unicode/emoji-18.0/emoji-test.txt", "format": "unicode-emoji-test",
            "statuses": ["fully-qualified", "component"]},
  "version": "18.0"
}
```

- **One entry can declare several sets** when its names source splits
  mechanically (`"split": "prefix"` makes the 17 `nerd-*` sets from one
  file). The set ids are derived, never listed by hand, so they cannot
  disagree with the file.
- **Codepoint ranges are derived, not declared.** A Nerd set is not one
  range (`oct` runs from U+2665 to U+F533; `iec` is outside the Private
  Use Areas altogether), so ranges are computed from the names source
  and never treated as authoritative: Blit computes them from its table
  at start and reports them with a set's listing (descriptive only).
- **Retired names** live here, not in the catalogue (whose shape is
  frozen): an id removed from the catalogue is moved to `retired_names`
  with the codepoint it had (which the catalogue's `retired` list keeps)
  and what replaced it, and is answered as retired forever (section 6).
- **Names are available at run time.** The catalogue and the Nerd names
  file are already in the package (`/usr/lib/blit/fonts`); the server
  reads the names sources the registry names once, at start, into a
  sorted table, and answers from it. The generated header
  (`src/blit_font_registry.h`) gains only the sources (key, owner,
  scope, names file, format, version, faces), the derived set list, and
  the retired names (which are in no names file), so no file is named
  twice and no name a file holds is copied. `emoji-test.txt` is not
  packaged today; serving `emoji` needs it, or a names file generated
  from it, in the package.
- **One answer everywhere.** `GET /v1/fonts` reports the sets (id,
  owner, scope, version, count, faces), `GET /v1/fonts/sets/{id}` serves
  a set's names and `GET /v1/fonts/sets/{id}/{name}` a name's record
  (built in 0.3.134), and the in-band replies below are to be built from
  the same table. A gate holds them equal (section 10.3; for HTTP,
  `glyph_sets_gate`).

### 3.3 The emoji name rule

An `emoji-test.txt` line's name (the text after the emoji and its
version, such as `keycap: #` or `Côte d’Ivoire`) becomes a name by these
steps, in order:

1. `#` becomes ` number sign ` and `*` becomes ` asterisk `.
2. Unicode compatibility decomposition (NFKD), then every character
   outside ASCII is dropped.
3. Letters are lowercased.
4. Every run of characters that are not `a`-`z` or `0`-`9` becomes one
   `-`, and a `-` at either end is removed.

So `keycap: #` is `emoji/keycap-number-sign`, `piñata` is
`emoji/pinata`, and `flag: Côte d’Ivoire` is `emoji/flag-cote-divoire`.
With Emoji 18.0 this gives 3,972 distinct names, no two alike, the
longest 76 characters, the longest sequence 10 codepoints. A generator
MUST fail if a future version makes two names alike, rather than
choose one.

## 4. The address

### 4.1 Grammar

In ABNF (RFC 5234):

```
address   = set-id "/" name
set-id    = owner *( "-" id-part )        ; 1 to 32 octets in all
owner     = lower *( lower / digit )
id-part   = 1*( lower / digit )
name      = name-start *95( name-char )   ; 1 to 96 octets
name-start = lower / digit
name-char = lower / digit / "-" / "_"
lower     = %x61-7A                       ; a-z
digit     = %x30-39                       ; 0-9
```

- Only ASCII. ECMA-48 allows only 02/00 to 07/14 (and a few format
  effectors) in a control string, so a name could not carry anything
  else anyway.
- The grammar excludes `;`, `,` and `=`, which frame Glyph Protocol
  messages, and `/`, `:`, `.` and `@`, which this capability uses as
  separators. A name never needs escaping.
- **Case:** names are lowercase. A terminal compares octets exactly and
  MUST NOT fold case: a request with an uppercase letter is malformed
  (section 4.5), so `BlueTooth` never silently means `bluetooth`.

### 4.2 Names within a set

- A name is unique within its set: it names one glyph.
- A set MAY give one glyph several names (Nerd Fonts' `dev-aws` and
  `dev-amazonwebservices` are both U+E7AD). Exactly one is canonical.
  The canonical name is the one the names source marks as canonical; a
  source that marks none (Nerd Fonts' file, today) has the bytewise
  smallest of the glyph's names as canonical. Every name is a full
  address and resolves; a lookup of an alias says which name is
  canonical (`alias_of`).
- A glyph drawn across several cells (Blit's 4x2 discs, its two-cell
  logos) has one name per cell, as the catalogue already has
  (`hui-left`, `hui-right`). A lookup says which tile it is (`tile`), and
  the listing finds the rest.

### 4.3 The same codepoint in two sets

Two addresses in different sets that give the same codepoint are not
aliases of each other: each set is its own table, with its own owner
and lifetime. A program MUST NOT infer anything from a shared codepoint.

## 5. Codepoints are local to a terminal: the contract

A name resolves to a codepoint **in this terminal, for this set
version**. The protocol does not make a codepoint mean the same thing
everywhere, and it cannot. What holds:

- **Resolve per session.** A program looks names up when it starts (or
  when it is attached to a new terminal) and prints the codepoints it
  got. It MAY keep the answers across runs only keyed by the set id and
  set version, and, for a terminal-local set, by the terminal's identity
  (XTVERSION); it MUST look them up again when any of these differ.
- **Portable sets are safe to print anywhere** that has the set: a
  `nerd-fa` codepoint is Font Awesome's in every terminal with Nerd
  Fonts 3.4.0. A program that may be shown on terminals it cannot ask
  should prefer portable sets.
- **Multiplexers.** tmux and screen do not answer `s`, so a program
  inside them sees no support unless it uses the multiplexer's
  passthrough (the Glyph Protocol's §3.4). With passthrough and two
  clients attached on two different terminals, if the multiplexer
  forwards the request to both (tmux(1) does not say), both may reply;
  the program SHOULD take the first complete answer and ignore a
  second. The pane's text is one byte stream: a
  terminal-local codepoint resolved against one terminal can draw as
  something else, or nothing, on the other. That is inherent, and it is
  why portable sets exist.
- **Recordings** (asciinema, `script`, `tee`) hold the program's output,
  which carries codepoints, not names; the lookup replies are input and
  are not in the recording. A recording replays faithfully only on a
  terminal that serves the same sets at the same versions. This is the
  same limit for which the Glyph Protocol withdrew `cp=auto` in its
  version 1.6; here the answers are fixed by the published table, not
  by session state, so the same terminal and version always answer the
  same.
- **Copy and paste** carry the codepoint, never the name. Pasted into
  another terminal, it draws whatever that terminal draws there.
- **Scrollback across a terminal update.** Cells keep the codepoint the
  program printed. A terminal that moves a terminal-local set to new
  codepoints MUST report a new set version, SHOULD keep drawing the old
  codepoints as the glyphs they moved to for a transition, and MUST NOT
  later give an old codepoint to a different glyph of its own. Blit did
  exactly this in 0.3.131: its icons moved from U+100000-U+100495 to
  U+101000-U+101495, and the old range still draws the same glyphs until
  a later release named in its notes (section 9).

## 6. Name lifetime

Who keeps these promises is the set's owner. A terminal serves a
portable set exactly as its owner publishes it at the version it
reports: a name that release does not have answers `unknown_name`, and
a new release is a new version, which tells programs to look up again.
Whether an owner drops or reuses names between its releases is the
owner's decision, not the terminal's. For a terminal-local set the terminal is the
owner, and the rules below bind it. An owner that publishes
deprecations or retirements (Blit's catalogue does) has them answered
as below whatever its scope.

- **A name is never reused** by the owner of a terminal-local set. Once
  it has published a name, at any version, that name never names a
  different glyph in that set. A glyph may be redrawn; its meaning does
  not change.
- **Deprecated** names still resolve, with `deprecated=1` and, when the
  set says so, `by=` (the names that replace it). Blit's catalogue
  already marks these (`deprecated: true`, `replaced_by`): `blit/wifi-1`
  resolves, deprecated, `by=cellular-1`.
- **Retired** names no longer resolve: the reply is `reason=retired`,
  with `by=` when they were replaced. A terminal keeps every retired
  name of every terminal-local set it owns, for good, so such a name is
  never answered as unknown and never taken by something new.
- **Codepoints of a portable set** are its owner's; a new owner release
  that moves them is a new set version. **Codepoints of a
  terminal-local set** may move between versions (section 5), never
  into the registration block (section 9).
- Deprecated and retired names are not listed (section 7.4); they exist
  for programs that already use them.

## 7. Wire protocol: the `names` capability

Framing, keys and value forms are the Glyph Protocol's (§3.2): every
message is `ESC _ 25a1 ; <verb> [ ; key=value ]* ESC \`, keys are
lowercase ASCII, codepoints are lowercase hex, lists are comma-separated.
This capability adds the key `name` to the `q` verb and one verb, `n`.
The pinned version of the Glyph Protocol this extends is in Appendix C.

### 7.1 Detection

A program sends the Glyph Protocol's support check followed by DA1, in
one write:

```
ESC _ 25a1 ; s ESC \  CSI c
```

A terminal that implements this capability lists `names` in the `cap`
key of its `s` reply:

```
ESC _ 25a1 ; s ; fmt= ; cap=names ESC \
```

then answers DA1. A DA1 reply with no `s` reply before it means the
Glyph Protocol is not supported; an `s` reply without `names` in `cap`
means this capability is not. In either case the program sends no
request of this document. (An empty `fmt=` is what a terminal that
accepts no glyph registrations sends, which is Blit's case.)

### 7.2 Lookup: `q` with `name`

Request, one address per message:

```
ESC _ 25a1 ; q ; name=<address> ESC \
```

`q` takes exactly one of `cp`, `grapheme` and `name`; anything else is
malformed (section 7.5).

Reply:

```
ESC _ 25a1 ; q ; name=<address> ; cp=<hex>[,<hex>]* ; status=<list> ; w=<cells> ; v=<version> [ ; alias_of=<name> ] [ ; deprecated=1 [ ; by=<name>[,<name>]* ] ] [ ; tile=<col>,<row>,<cols>,<rows> ] ESC \
```

| Key | Value |
| --- | --- |
| `name` | the address, as the terminal parsed it (only when it parsed; section 8) |
| `cp` | the codepoint, or the sequence of up to 32 codepoints in order (an emoji sequence) |
| `status` | the Glyph Protocol's coverage list: §5.2's meaning for one codepoint, §5.3's for a sequence. Empty means the terminal knows the name but cannot draw it now (a face is missing) |
| `w` | the cells the terminal advances when it prints `cp` as plain text in the current mode: `1` or `2` |
| `v` | the set version, 1 to 64 characters of `a`-`z`, `0`-`9`, `.` and `-` |
| `alias_of` | the canonical name, when the name asked for is an alias |
| `deprecated`, `by` | section 6 |
| `tile` | for one cell of a multi-cell picture: its column and row (from 0), and the picture's size in cells |

`w` is reported although the Glyph Protocol's grapheme query does not
report widths (§5.3), because a name's codepoint is usually private
use, which Unicode gives an ambiguous East Asian width
(`EastAsianWidth.txt`: `E000..F8FF`, `F0000..FFFFD`, `100000..10FFFD`
are `A`). A program that learned the codepoint at run time has no width
table that knows it; `w` is the width it must count. It is the
terminal's ordinary width for that text, not an override.

Examples:

```
ESC _ 25a1 ; q ; name=blit/bluetooth ESC \
ESC _ 25a1 ; q ; name=blit/bluetooth ; cp=101006 ; status=system ; w=1 ; v=<catalogue revision> ESC \

ESC _ 25a1 ; q ; name=nerd-fa/bluetooth ESC \
ESC _ 25a1 ; q ; name=nerd-fa/bluetooth ; cp=f293 ; status=system ; w=1 ; v=3.4.0 ESC \

ESC _ 25a1 ; q ; name=nerd-md/bluetooth ESC \
ESC _ 25a1 ; q ; name=nerd-md/bluetooth ; cp=f00af ; status=system ; w=1 ; v=3.4.0 ESC \

ESC _ 25a1 ; q ; name=nerd-dev/aws ESC \
ESC _ 25a1 ; q ; name=nerd-dev/aws ; cp=e7ad ; status=system ; w=1 ; v=3.4.0 ; alias_of=amazonwebservices ESC \

ESC _ 25a1 ; q ; name=emoji/keycap-number-sign ESC \
ESC _ 25a1 ; q ; name=emoji/keycap-number-sign ; cp=23,fe0f,20e3 ; status=system ; w=2 ; v=18.0 ESC \

ESC _ 25a1 ; q ; name=blit/wifi-1 ESC \
ESC _ 25a1 ; q ; name=blit/wifi-1 ; cp=101002 ; status=system ; w=1 ; v=<catalogue revision> ; deprecated=1 ; by=cellular-1 ESC \

ESC _ 25a1 ; q ; name=blit/hui-right ESC \
ESC _ 25a1 ; q ; name=blit/hui-right ; cp=101019 ; status=system ; w=1 ; v=<catalogue revision> ; tile=1,0,2,1 ESC \
```

`dev-amazonwebservices` and `dev-aws` share U+E7AD. `amazonwebservices`
sorts first, so it is canonical: its own reply has no `alias_of`, and
`aws` answers `alias_of=amazonwebservices`. `alias_of` appears only
when the name asked for is an alias.

### 7.3 Batching

A terminal answers requests in the order it receives them (the Glyph
Protocol's §5.5). A program writes all its lookups and then DA1, in one
write, and reads until DA1's reply:

```
ESC _ 25a1 ; q ; name=blit/bluetooth ESC \
ESC _ 25a1 ; q ; name=nerd-fa/bluetooth ESC \
ESC _ 25a1 ; q ; name=emoji/pinata ESC \
CSI c
```

Every lookup that will be answered has been answered when DA1's reply
arrives. A program matches each reply to its request by `name`; a lookup
with no reply by then (a terminal under load may discard it, section 8)
is treated as unknown, and the program uses its fallback for it.

### 7.4 Listing: the `n` verb

`n` lists, a page at a time. It exists for pickers and tools; a program
that knows its names needs only `q`.

**Sets:**

```
ESC _ 25a1 ; n ; list=sets [ ; after=<set-id> ] ESC \
ESC _ 25a1 ; n ; list=sets ; sets=<entry>[,<entry>]* ; more=<0|1> ESC \
```

`entry = set-id ":" scope ":" count ":" version`, where scope is `p`
(portable) or `t` (terminal-local) and count is the number of names
listed for the set (deprecated names are not counted: Blit's 830 ids
list as 808). Sets are in bytewise order of id; `after` starts
the page after that id.

**Names in a set:**

```
ESC _ 25a1 ; n ; list=names ; set=<set-id> [ ; after=<name> ] ESC \
ESC _ 25a1 ; n ; list=names ; set=<set-id> ; v=<version> ; names=<entry>[,<entry>]* ; more=<0|1> ESC \
```

`entry = name ":" hex *( "." hex )` for a canonical name, or
`name ":@" canonical-name` for an alias. Names are in bytewise order;
`after` starts the page after that name, whether or not it is still in
the set. Deprecated and retired names are not listed.

**Pages.** A reply holds as many whole entries as fit in 4096 bytes,
framing included, and at most 128; at least one when any remain.
`more=1` means another page follows `after=<the last name listed>`. A
program that sees `v` change between pages starts again.

Example, the first page of the sets on Blit:

```
ESC _ 25a1 ; n ; list=sets ESC \
ESC _ 25a1 ; n ; list=sets ; sets=blit:t:808:<catalogue revision>,emoji:p:3972:18.0,nerd-cod:p:438:3.4.0,... ; more=0 ESC \
```

### 7.5 Errors

A request that cannot be answered gets a reply with `reason=` and no
`cp`. The terminal echoes the request's address or set id only when it
parsed against the grammar of section 4.1.

| `reason=` | Meaning | Echoed |
| --- | --- | --- |
| `malformed_query` | not exactly one of `cp`, `grapheme`, `name`; an address or set id outside the grammar; an unknown `list` value; a request over 256 bytes | nothing |
| `unknown_set` | no such set in this terminal | `name` or `set` |
| `unknown_name` | the set has no such name, and never had | `name` |
| `retired` | the name was retired (section 6); `by=` when replaced | `name` |
| `unsupported` | the terminal does not list `names` in `cap` | nothing |
| `unknown` | the terminal is limiting how many requests it answers; ask again later | `name` or `set` |

```
ESC _ 25a1 ; q ; name=blit/no-such-icon ESC \
ESC _ 25a1 ; q ; name=blit/no-such-icon ; reason=unknown_name ESC \

ESC _ 25a1 ; q ; name=Blit/Bluetooth ESC \
ESC _ 25a1 ; q ; reason=malformed_query ESC \
```

Programs MUST ignore `reason` values and keys they do not know, as the
Glyph Protocol requires.

## 8. Reply safety

Replies are written into the program's input. Blit's security review
([SR-4](../security-review-2026-11.md)) found an XTGETTCAP reply that
copied request bytes back verbatim, so `cat` of a hostile file typed a
command into the shell. This capability is built so that cannot happen:

- **No echo of anything unparsed.** A reply contains only values the
  terminal parsed against section 4.1's grammar, written out again from
  the parsed value, or values from its own tables. A malformed request
  is answered with `reason=malformed_query` and nothing else.
- **A fixed alphabet.** A reply contains only the framing (`ESC _` and
  `ESC \`) and the octets `a`-`z`, `0`-`9`, `;`, `=`, `,`, `.`, `:`,
  `/`, `@`, `-` and `_`. No CR, LF or other control character, so a
  reply that arrives after the program has exited can at worst appear at
  a shell prompt; it can never end a line (the Glyph Protocol's
  Appendix C describes stray replies).
- **Bounded size.** A request over 256 bytes is malformed; a reply is
  at most 4096 bytes; a lookup carries at most 32 codepoints.
- **Bounded work.** A lookup is a search in a table built at start; it
  never opens a font or a file. Coverage (`status`) comes from the
  faces' character maps loaded at start. Nothing loops over a count the
  request supplies ([SR-16](../security-review-2026-11.md) was such a
  loop).
- **Rate.** A terminal MAY answer `reason=unknown` past a budget of its
  own. Blit's proposed budget: 2,048 lookups and 64 pages a second per
  session, the rest answered `unknown`.
- **Never block.** A program that sends requests and never reads leaves
  replies in the PTY. The terminal MUST NOT stall its input handling, or
  other sessions, waiting to write them. It keeps a bounded reply queue
  per session; when a reply does not fit, it discards that request
  unanswered and writes nothing for it (never part of a reply). Replies
  stay in request order. A program therefore matches replies by their
  `name` or `set`, not by position, and treats a request with no reply
  by the time DA1's reply arrives as unknown (section 7.3).
- **What a reply discloses:** which sets and versions the terminal
  serves. That says which terminal and release it is, as XTVERSION
  already does.
- **Nothing changes how text looks.** A name only reports a codepoint
  the terminal already draws. Registration (the Glyph Protocol's `r`)
  is unrelated, and its rules are unchanged.

## 9. The registration block, and Blit's move

The Glyph Protocol reserves U+100000-U+1003FF for glyphs programs
register (its §4), and says nothing else puts glyphs there.

- A lookup MUST NOT answer a codepoint in the registration block. No
  set may place a glyph there.
- **Blit's transition.** Until 0.3.130 Blit's icons were at
  U+100000-U+100495, overlapping the block. From 0.3.131 they are at
  U+101000-U+101495 (every glyph at old + 0x1000), and the old
  codepoints still draw the same glyphs, for text printed by old
  programs. During that transition:
  - lookups answer only the new codepoints, never the old ones;
  - `q ; cp=` for a codepoint in the block answers as if nothing
    covered it (`status=` empty, or `status=glossary` once a program has
    registered it), because the old drawing is a courtesy to old output,
    not coverage a program may rely on;
  - Blit lists no payload format in `fmt` (accepts no registrations)
    until a release has dropped the old range, so a registered glyph and
    an old icon never compete for a codepoint. If Blit ever accepts
    registrations first, a registration MUST win over the old drawing.
- U+100400-U+100495 (the old range above the block) is retired with the
  rest of the old range: never answered by a lookup, never reused, and
  `q ; cp=` for it answers `status=` empty during the transition too,
  for the same reason.

## 10. Conformance

### 10.1 A terminal conforms if it

1. Lists `names` in `cap` only when it implements all of this document.
2. Answers `q ; name=` and `n` in the order received, never stalling
   input handling (sections 7.3 and 8).
3. Serves only registered owners' sets, each exactly as its names
   source defines it at the reported version (section 3.1).
4. Parses addresses by the grammar of section 4.1 with no case folding,
   and echoes only what parsed (section 8).
5. For a set it owns, keeps every name for good: never reuses one,
   answers retired names as retired; for any set, answers deprecations
   and retirements its owner publishes (section 6).
6. Never answers a codepoint in the registration block (section 9).
7. Reports `w` equal to the cells it advances when it prints `cp`, and
   `status` with the Glyph Protocol's meaning.
8. Pages listings within the bounds of section 7.4, and lists every
   listed name exactly once across the pages of one version.
9. Writes replies from the fixed alphabet, within the size bounds
   (section 8).

### 10.2 A program conforms if it

1. Sends no request of this document until an `s` reply lists `names`.
2. Ends a batch with DA1, and treats an unanswered lookup as unknown.
3. Prints its own fallback for an unknown, retired or unanswered name,
   and when the capability is missing.
4. Counts `w` cells for each glyph it prints.
5. Looks names up again when a set version or the terminal changes
   (section 5), and prefers portable sets for output other terminals
   may show.
6. Ignores reasons and keys it does not know.

### 10.3 What a conformance probe checks

A probe is a program run inside the terminal under test, with no access
to the terminal but its PTY. It checks, and prints one line per check:

- detection: `s` then DA1; `names` in `cap`; DA1 answered after `s`;
- ordering: a batch of lookups, including errors, answered in order and
  all before DA1;
- the registered sets: for every set the terminal lists, page through
  it; the count matches the listing; for a registered portable set, the
  names and codepoints equal the owner's published file at the reported
  version (the probe carries the Nerd 3.4.0 and Emoji 18.0 tables);
- every listed name looked up answers the listed codepoint; `w` equals
  the cursor's movement when the codepoint is printed (a cursor position
  report before and after); a codepoint in the registration block never
  appears;
- the grammar: an uppercase address, an address holding a space or a
  non-ASCII byte, an empty name, a 97-octet name, a 300-byte request --
  each answered `reason=malformed_query` with nothing echoed, and the
  PTY receiving exactly that reply;
- the hostile echo: a request carrying CR, LF and a shell command
  inside the address; the reply holds none of those bytes (the SR-4
  case);
- aliases, deprecated and retired names, where the terminal's sets have
  them;
- rate: a burst past the budget is answered `unknown` or not at all, in
  order, and a following DA1 is answered within 50 ms.

A probe cannot see other sessions, so "a session that never reads its
replies does not stall the others" is a terminal's own test; for Blit,
a gate writes a burst into one session without reading and requires a
DA1 in a second session to be answered within 50 ms.

For Blit, a gate also holds that `fonts.source.json`'s `sets`,
`GET /v1/fonts` with `GET /v1/fonts/sets/{id}`, and the in-band answers
are the same table: every name, codepoint, alias, deprecation and
version, in all three.

## Appendix A. If the Glyph Protocol declines: a standalone OSC

If the Glyph Protocol's maintainers decline the `names` capability,
Blit serves the same verbs under its own OSC number, 7701, the number
of its pointer-lock mode. No body allocates OSC numbers; no list consulted
for this draft uses 7701, but without a registry that cannot be proved. Every rule in
sections 3 to 10 applies unchanged; only the framing differs:

```
OSC 7701 ; s ST                               -> OSC 7701 ; s ; cap=names ST
OSC 7701 ; q ; name=<address> ST              -> OSC 7701 ; q ; name=... ; cp=... ST
OSC 7701 ; n ; list=sets [ ; after=<id> ] ST  -> OSC 7701 ; n ; list=sets ; ... ST
```

Detection is `OSC 7701 ; s ST` followed by DA1; a terminal that does
not answer before DA1 does not support it. `status` keeps the Glyph
Protocol's meaning when the terminal also implements that protocol, and
is otherwise `system` or empty only. The reply-safety rules of section
8 apply to OSC replies exactly as to APC ones.

## Appendix B. A program's start-up, end to end

1. Write `ESC _ 25a1 ; s ESC \` and `CSI c`.
2. Read until DA1's reply. No `s` reply, or no `names` in its `cap`:
   use plain-text fallbacks (or, with `s` but no `names`, ask `q ; cp=`
   for the Nerd codepoints the program knows), and stop here.
3. Write one `q ; name=` per glyph, then `CSI c`. Read until DA1's
   reply.
4. For each glyph, keep `cp`, `w` and `v`; use the fallback where the
   reply was a `reason`, or missing.
5. Print the codepoints, counting `w` cells for each.

## Appendix C. Sources

- The Glyph Protocol, version 1.18 (change log entry 2026-11-19),
  `specs/glyph-protocol.md` in the Rio terminal's repository,
  https://github.com/raphamorim/rio/blob/main/specs/glyph-protocol.md,
  fetched 2026-11-21, SHA-256
  `68cc90ed0597dbf154b0e8dbd59f52711db9b6ffdf4d829dfafbfc9fb48cda37`.
  Sections cited: §3.2 framing, §3.3 `s` and `cap`, §3.4 detection,
  §4 the registration block, §5 query, §9 security, Appendix C, and the
  change log (v1.6 withdrew `cp=auto`; v1.13 reserved the block; v1.14
  added `cap` with `grapheme`).
- ECMA-48, 5th edition (June 1991): §5.4 (private parameter strings and
  final bytes), §8.3.2 APC and §8.3.89 OSC (the characters a control
  string may hold), §8.3.53 FNT, §8.3.117 SGR 10-19,
  https://ecma-international.org/publications-and-standards/standards/ecma-48/.
- Unicode `EastAsianWidth.txt` 18.0.0,
  https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt.
- Nerd Fonts' names file, as pinned in Blit:
  [nerd-glyphnames-v3.4.0.json](../../assets/fonts/nerd-glyphnames-v3.4.0.json),
  upstream https://raw.githubusercontent.com/ryanoasis/nerd-fonts/v3.4.0/glyphnames.json.
- Unicode emoji test data, as pinned in Blit:
  [emoji-test.txt](../../assets/unicode/emoji-18.0/emoji-test.txt).
- Blit: [ADR 0243](../adr/0243-glyph-multiplexing.md), the
  [icon catalogue](../../assets/fonts/icons/catalogue.json) and
  `GET /v1/fonts/icons` in the [API guide](../api.md), the
  [terminal reference](../terminal.md) (DA1, XTGETTCAP), the
  [security review](../security-review-2026-11.md).
