#!/usr/bin/env python3
"""Build the double-width PSF2 that Blit uses for wide cells.

Blit's primary font is 8x16 and a wide character occupies two cells, so its
glyph needs a 16x16 bitmap. PSF2 carries the cell size in its header, so the
wide font is a second artifact rather than a change to the format.

Input is GNU Unifont's BDF, which is the only freely licensed bitmap font with
complete coverage of the wide BMP blocks. Unifont is dual licensed SIL OFL 1.1
and GPLv2+ with the GNU Font Embedding Exception; the OFL is what this relies
on. See assets/fonts/README.md.

Only glyphs the BDF marks DWIDTH 16 are taken. A narrow glyph belongs in the
primary font, and taking one here would put an 8px bitmap in a 16px cell.

    tools/fonts/make-wide-psf.py unifont.bdf assets/fonts/<name>.psf
"""
import struct
import sys

PSF2_MAGIC = b"\x72\xb5\x4a\x86"
PSF2_HAS_UNICODE = 1
WIDTH = 16
HEIGHT = 16
ROW_BYTES = (WIDTH + 7) // 8
CHARSIZE = ROW_BYTES * HEIGHT


def read_wide_glyphs(path):
    """Yield (codepoint, bitmap bytes) for every DWIDTH 16 glyph, by codepoint."""
    glyphs = {}
    codepoint = None
    dwidth = None
    rows = None
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("ENCODING"):
            codepoint = int(line.split()[1])
        elif line.startswith("DWIDTH"):
            dwidth = int(line.split()[1])
        elif line == "BITMAP" or line == "BITMAP ":
            rows = []
        elif line.startswith("ENDCHAR"):
            if (codepoint is not None and codepoint >= 0 and dwidth == WIDTH
                    and rows is not None and len(rows) == HEIGHT):
                glyphs[codepoint] = b"".join(rows)
            codepoint = dwidth = rows = None
        elif rows is not None and line:
            # Each row is ROW_BYTES*2 hex digits. A BDF pads rows to a byte
            # boundary, which for 16px is exactly two bytes.
            value = bytes.fromhex(line.strip())
            if len(value) != ROW_BYTES:
                rows = None
                continue
            rows.append(value)
    return dict(sorted(glyphs.items()))


def write_psf2(path, glyphs):
    bitmaps = bytearray()
    table = bytearray()
    for codepoint, bitmap in glyphs.items():
        assert len(bitmap) == CHARSIZE, (codepoint, len(bitmap))
        bitmaps += bitmap
        table += chr(codepoint).encode("utf-8")
        table += b"\xff"
    header = PSF2_MAGIC + struct.pack(
        "<7I", 0, 32, PSF2_HAS_UNICODE, len(glyphs), CHARSIZE, HEIGHT, WIDTH)
    with open(path, "wb") as out:
        out.write(header)
        out.write(bitmaps)
        out.write(table)
    return len(header) + len(bitmaps) + len(table)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, destination = sys.argv[1], sys.argv[2]
    glyphs = read_wide_glyphs(source)
    if not glyphs:
        sys.exit("no DWIDTH %d glyphs in %s" % (WIDTH, source))
    size = write_psf2(destination, glyphs)
    print("%s: %d glyphs %dx%d, %.2f MiB" %
          (destination, len(glyphs), WIDTH, HEIGHT, size / (1 << 20)))
    for name, (lo, hi) in (("CJK Unified", (0x4E00, 0x9FFF)),
                           ("Hiragana", (0x3040, 0x309F)),
                           ("Katakana", (0x30A0, 0x30FF)),
                           ("Hangul Syllables", (0xAC00, 0xD7A3)),
                           ("Fullwidth forms", (0xFF01, 0xFF60))):
        have = sum(1 for c in range(lo, hi + 1) if c in glyphs)
        print("  %-18s %5d / %5d" % (name, have, hi - lo + 1))


if __name__ == "__main__":
    main()
