# ADR 0196: Battery and volume are one levelled icon per state

- Status: accepted; in use since 0.2.38
- Date: 2026-10-27
- Scope: `assets/fonts/icons` (catalogue and artwork), `BlitIcons-Regular.ttf`,
  the icon gates

## Context

On Blit, Hibr's menu bar showed the battery as two glyphs side by side: a
Nerd bolt or plug, then a Nerd battery level. The owner asked for one icon
that says the whole state, levelled like Blit's Wi-Fi icon, and the same for
volume. On other terminals Hibr keeps its own bars.

## Options considered

1. **Keep a state mark beside a level glyph.** This is what was there, and
   it is what the owner called coupling.
2. **One colour icon per state, in Blit's icon font.** Levels come from one
   drawing each, as Wi-Fi does. Hibr picks one codepoint per state.

## Decision

Option 2, with codepoints agreed with Hibr:

| IDs | Codepoints | Drawing |
|---|---|---|
| `battery-0` … `battery-4` | U+100009–U+10000D | `battery.svg`: upright battery, 0–4 teal segments |
| `battery-0-charging` … `battery-4-charging` | U+10000E–U+100012 | `battery-charging.svg`: green segments, a yellow bolt inside |
| `battery-ac` | U+100013 | `battery-ac.svg`: full, with a plug inside: on mains, not charging |
| `volume-0` | U+100014 | `volume-muted.svg`: speaker with a red cross |
| `volume-1` … `volume-3` | U+100015–U+100017 | `volume.svg`: speaker, 1–3 active waves |

- **Levels** use the Wi-Fi mechanism: a path is active when its
  `data-level` is at or below the icon's `signal_level`, and otherwise takes
  its `data-inactive-fill`.
- **Muted** is its own drawing, because a level can only add parts, not
  replace them.
- **U+100007 and U+100008** stay the bar corners (cell geometry), so the
  new range starts at U+100009.

## Consequences

- Hibr maps its battery and volume glyphs to these in its Blit set and
  draws one glyph per state. Other glyph sets are unchanged.
- The icon font grows from 9 to 24 colour glyphs. It is still built
  reproducibly from `catalogue.json`, and fitted by the stack like every
  other icon.

- **Power (0.2.41):** `power` at U+10001A, `power.svg`, is the standard
  power symbol with a teal ring and a light bar. It is for Hibr's Power
  item on machines with no battery.
- **Telephone (0.2.44),** for longwire's menu bar item and Control
  Strip module, asked for by the owner:
  - `phone-off`, U+10001B: outline;
  - `phone-wait`, U+10001C: dashed outline, connecting;
  - `phone-on`, U+10001D: filled;
  - `phone-ring`, U+10001E: filled, with ring waves.
  The handset is one variable-width stroke along an arc. The outline and
  the dashes are the band between its edge and an inset of it, so all
  four share one shape.
- **Classic telephone (0.2.45).** The owner wanted the choice of a retro
  phone too. A rotary desk phone comes in the same four states:
  `phone-classic-off`, `-wait`, `-on` and `-ring`, at U+10001F–100022. Its
  parts are the base (with the dial cut into it when filled), the dial and
  the handset (one arched outline with its two cups), each insettable the
  same way. longwire's Phone Style setting picks Modern or Classic.
- **The classic set redrawn (0.2.47).** As first shipped, its ringing icon's
  waves reached past the view box. That made its ink wider than one icon,
  so the renderer took it for a two-column icon and drew half of a badge.
  The other three were too fine to read at the menu bar's 10 px. They are
  redrawn with thicker lines, a bigger dial, ring marks beside the base,
  and dashes spaced evenly along the outline. Both sets now come from
  `tools/fonts/draw-phone-icons.py`, which refuses an icon whose ink is
  wider than its view box. It reproduces the modern set byte for byte.
- **Large phones (0.3.0).** The owner keeps one cell as the default and
  wanted the option of two. Every phone icon, both styles and all four
  states, also comes as a two-column badge (ADR 0197): a left/right pair
  from the same drawing, at U+100023–100032. longwire's Phone Icon Size
  setting picks Small or Large for the menu bar, and the Control Strip
  keeps Small.
- **Large phones retired (0.3.5).** The owner decided regular is all
  longwire needs. longwire 0.5.22 and Hibr 0.99.378 stopped using them
  first, then Blit removed them. U+100023–100032 is listed under `retired`
  in the catalogue and is never reused.
- **Hibr no longer reads `BLIT_VERSION` (its 0.99.360).** Choosing the
  blit glyph set is the one switch for every Blit icon. So a Blit release
  must reach the owner's machines before the Hibr release that uses its
  codepoints. `BLIT_VERSION` stays for any child that wants it.
- **A child can tell which Blit it runs under (0.2.39).** Hibr's desktop
  keeps no Blit URL or token, so it can't ask `GET /v1/fonts/icons`
  cheaply. Every session child now gets `BLIT_VERSION`, the server's
  version, and Hibr uses the new icons from 0.2.38 on, with its bars below
  that. `TERM_PROGRAM` was not used, because Hibr names a display after it.

## Evidence (0.2.38)

- `custom_icon_gate`: all 22 colour originals in every text font at all 57
  sizes, premultiplied with partial-alpha edges. Every level is a distinct
  raster: Wi-Fi 4, battery 11 (including charging and mains), volume 4.
- `custom_icon_api_gate`: the 22 originals through a real PTY, one column
  each, with identical pixels by PTY and API.
- `font_kms_raster_gate`: the new range on the KMS path at every size.
- `build-blit-icons.py` builds the same font twice
  (sha256 `80c0feec…2c15`).
- `child_environment_gate` (0.2.39, new): a session child's `BLIT_VERSION`
  equals `server_version`, beside the binding coordinates.
