# ADR 0172: one original vector catalogue for font generation and API discovery

- Status: accepted; in use since 0.1.588
- Date: 2026-10-21
- Scope: fonts, colour raster, API discovery
- Tracking: [Blit 118](https://git.home.arpa/itdlabs/blit/issues/118),
  [Hibr 330](https://git.home.arpa/itdlabs/hibr/issues/330)

## Context

Full Nerd cmap tests did not prove live glyph coverage or bespoke artwork.
Read-only Hibr GL inspection found U+16D2 absent from both outline/PSF routes
and U+21CC absent in Retro. Existing U+270E used a Font Awesome pencil alias;
it was not the promised original design. The operator also requests four
Wi-Fi levels and Bluetooth. Do not modify Hibr or silently replace Nerd codes.

## Options and decision

Keep ordinary Nerd glyphs foreground-coloured and standard emoji under
ADR 0163. Add seven explicit supplementary private-use icons in the original
SVG catalogue `assets/fonts/icons/catalogue.json`, with one-cell advances in
both Modern and Retro. Build-time fontTools generates glyf/COLR v0/CPAL from
the same paths, pinned source hashes and licences. A themed monochrome pen
alias replaces the previous U+270E alias; missing standard text glyphs keep
their meanings. Preserve all original source Unicode/Nerd mappings otherwise.

Prefer scalable COLR v0 to a new bitmap atlas or runtime SVG renderer. The
existing FreeType dependency supports COLR v0 default blending through
`FT_LOAD_COLOR`, returning premultiplied BGRA; shared raster code converts
it to the existing premultiplied RGBA contract. Ordinary outline glyphs stay
coverage masks. This is not COLR v1 or arbitrary SVG rendering support.
See [FreeType glyph retrieval](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html)
and [layer management](https://freetype.org/freetype2/docs/reference/ft2-layer_management.html).

Authenticated `GET /v1/fonts/icons`, specified first in OpenAPI, serves the
packaged canonical catalogue (bounded 8KiB), resolved from the same font root.
Missing/unreadable/oversized assets return 503 with
`font_icon_catalogue_unavailable`; the reader does not mutate configuration.
Package asset gates verify hash identity. Do not maintain another mapping
table in the server, Control Panel or renderer. Native-font source selection
continues to follow ADR 0171; CJK and bitmap-only profiles remain intact.

The initial 588 package was rejected before deployment by an older heuristic
that treated any `hibr-*` basename as a launcher, including `hibr-pen.svg`.
Build and artifact gates now share one runtime-entrypoint path policy: reject
shell files and retired launchers in runtime directories, not SVG/font assets.
The boundary fixture tests six rejected and five legitimate paths. Listing
checks consume the full producer output, avoiding quiet-grep SIGPIPE noise.

## Evidence and limits

Custom raster gate covers 7×57×2=798 originals, including distinct Wi-Fi
levels at 8px, partial alpha, premultiplication, foreground independence,
unchanged advances/cache budget and themed U+270E. The hash-pinned Hibr GL
snapshot covers 101 scalars×57 sizes×2 spans×2 profiles=23,028 non-replacement
cases; legitimate PSF fallbacks remain explicitly allowed. It is not every
application, every Unicode scalar or universal shaping. Nerd and emoji corpus
gates remain independent. Native/SDL adapter gates include originals; the
local real-PTY/API fixture verifies split UTF-8 and presentation parity.

Hibr must explicitly select the colour mappings; 330 tracks that integration.
No font/backend/layout change or Hibr edit is implied by generating assets.
Live installation/operator aesthetic acceptance must be recorded separately.

## Developer reference publication (Blit 121)

Publish docs/font-reference.md through the public github.com/osakka/blit
repository. Gitea remains the internal engineering origin and issue backlog;
it is not the public documentation address. Prefer repository-relative file
links and GitHub blob/main URLs for externally shared documents.
Generate docs/fonts tables from the existing hash-pinned Nerd catalogue,
original icon catalogue, emoji corpus and exact font cmaps using the existing
fontTools 4.60.1 build dependency. Do not introduce another editable glyph
registry, runtime route, hosted service or client-side numbering scheme.
Generated JSON coverage is an asset inventory, not renderer capability or
shaping acceptance. Browser glyphs are explicitly not production previews;
the existing shared-raster specimen supplies that evidence. The reference
generator's --check mode and font_reference_gate guard drift, encoding,
category completeness and relative links. A future site consumes the same
canonical files rather than forking the reference.
