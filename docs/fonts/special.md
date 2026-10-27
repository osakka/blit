# Blit/Hibr special additions

[Developer guide](../font-reference.md) · [Canonical runtime catalogue](../../assets/fonts/icons/catalogue.json)

Generated from the same catalogue served by `GET /v1/fonts/icons`. These original intrinsic-colour icons are one cell each in every native font, from the glyph stack. Supplementary private-use assignments are a Blit contract, not standard Unicode or Nerd Fonts assignments.

| Semantic ID | Codepoint | JSON string | Signal level |
| --- | --- | --- | --- |
| `blit` | `U+100000` | `"\udbc0\udc00"` | — |
| `hibr-pen` | `U+100001` | `"\udbc0\udc01"` | — |
| `wifi-1` | `U+100002` | `"\udbc0\udc02"` | 1 |
| `wifi-2` | `U+100003` | `"\udbc0\udc03"` | 2 |
| `wifi-3` | `U+100004` | `"\udbc0\udc04"` | 3 |
| `wifi-4` | `U+100005` | `"\udbc0\udc05"` | 4 |
| `bluetooth` | `U+100006` | `"\udbc0\udc06"` | — |
| `battery-0` | `U+100009` | `"\udbc0\udc09"` | 0 |
| `battery-1` | `U+10000A` | `"\udbc0\udc0a"` | 1 |
| `battery-2` | `U+10000B` | `"\udbc0\udc0b"` | 2 |
| `battery-3` | `U+10000C` | `"\udbc0\udc0c"` | 3 |
| `battery-4` | `U+10000D` | `"\udbc0\udc0d"` | 4 |
| `battery-0-charging` | `U+10000E` | `"\udbc0\udc0e"` | 0 |
| `battery-1-charging` | `U+10000F` | `"\udbc0\udc0f"` | 1 |
| `battery-2-charging` | `U+100010` | `"\udbc0\udc10"` | 2 |
| `battery-3-charging` | `U+100011` | `"\udbc0\udc11"` | 3 |
| `battery-4-charging` | `U+100012` | `"\udbc0\udc12"` | 4 |
| `battery-ac` | `U+100013` | `"\udbc0\udc13"` | — |
| `volume-0` | `U+100014` | `"\udbc0\udc14"` | — |
| `volume-1` | `U+100015` | `"\udbc0\udc15"` | 1 |
| `volume-2` | `U+100016` | `"\udbc0\udc16"` | 2 |
| `volume-3` | `U+100017` | `"\udbc0\udc17"` | 3 |
| `hui-left` | `U+100018` | `"\udbc0\udc18"` | — |
| `hui-right` | `U+100019` | `"\udbc0\udc19"` | — |
| `power` | `U+10001A` | `"\udbc0\udc1a"` | — |

## Themed text additions

Text aliases use the terminal foreground, not intrinsic colour.

| Semantic ID | Codepoint | JSON string |
| --- | --- | --- |
| `hibr-pen` (themed alias) | `U+270E` | `"\u270e"` |
| `berkanan` | `U+16D2` | `"\u16d2"` |
| `wired` | `U+21CC` | `"\u21cc"` |

Berkanan preserves its runic meaning; it is not the Bluetooth icon. Existing standard/Nerd assignments are not repurposed.

## Cell geometry

Drawn by the shared raster at every cell size, edge to edge, in the terminal foreground: no outline font carries them, so neighbouring cells join without a seam. One cell each, in every font.

| Semantic ID | Codepoint | JSON string | Shape |
| --- | --- | --- | --- |
| `bar-corner-left` | `U+100007` | `"\udbc0\udc07"` | cell filled except a quarter-circle cut at its top-left, radius the cell's shorter side |
| `bar-corner-right` | `U+100008` | `"\udbc0\udc08"` | cell filled except a quarter-circle cut at its top-right, radius the cell's shorter side |

[Production-raster specimen, including all seven colour originals](../images/blit-fonts.png).
