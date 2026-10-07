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
  /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
```

The generated PSF is checked in so runtime installations have no font-tool or
font-package dependency.
