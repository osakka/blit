# Blit/Hibr special additions

[Developer guide](../font-reference.md) · [Canonical runtime catalogue](../../assets/fonts/icons/catalogue.json)

Generated from the same catalogue served by `GET /v1/fonts/icons`. These original intrinsic-colour icons are one cell each in both native profiles. Supplementary private-use assignments are a Blit contract, not standard Unicode or Nerd Fonts assignments.

| Semantic ID | Codepoint | JSON string | Signal level |
| --- | --- | --- | --- |
| `blit` | `U+100000` | `"\udbc0\udc00"` | — |
| `hibr-pen` | `U+100001` | `"\udbc0\udc01"` | — |
| `wifi-1` | `U+100002` | `"\udbc0\udc02"` | 1 |
| `wifi-2` | `U+100003` | `"\udbc0\udc03"` | 2 |
| `wifi-3` | `U+100004` | `"\udbc0\udc04"` | 3 |
| `wifi-4` | `U+100005` | `"\udbc0\udc05"` | 4 |
| `bluetooth` | `U+100006` | `"\udbc0\udc06"` | — |

## Themed text additions

Text aliases use the terminal foreground, not intrinsic colour.

| Semantic ID | Codepoint | JSON string |
| --- | --- | --- |
| `hibr-pen` (themed alias) | `U+270E` | `"\u270e"` |
| `berkanan` | `U+16D2` | `"\u16d2"` |
| `wired` | `U+21CC` | `"\u21cc"` |

Berkanan preserves its runic meaning; it is not the Bluetooth icon. Existing standard/Nerd assignments are not repurposed.

[Production-raster specimen, including all seven colour originals](../images/blit-fonts.png).
