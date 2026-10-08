# Blit terminal font

`Blit-Hibr-Terminus16.psf` is the runtime font. It preserves the bundled
Terminus 8×16 glyphs and adds the missing Hibr Unicode desktop glyphs at the
same cell size. The added glyphs are rasterized from DejaVu Sans Mono, which is
licensed under the permissive DejaVu font license; see the system font package
for the complete license text.

Regenerate it with:

```sh
tools/build-hibr-font.py assets/fonts/Uni2-Terminus16.psf \
  assets/fonts/Blit-Hibr-Terminus16.psf \
  /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
  /usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf \
  /usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf
```

The generated PSF is checked in so runtime installations have no font-tool or
font-package dependency. The manifest includes Hibr glyphs plus common
terminal shading symbols and combining marks; it does not claim universal
Unicode or CJK shaping in an 8×16 cell. The build uses ordered fallback fonts
and fails if a required scalar resolves to a fallback `.notdef` bitmap. Required box-drawing characters are
generated as dedicated deterministic terminal geometry, rather than inherited
from potentially aliased fallback-font bitmaps.
