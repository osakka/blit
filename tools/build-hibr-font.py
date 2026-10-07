#!/usr/bin/env python3
"""Build Blit's bundled 8x16 Hibr-capable PSF2 font.

The Terminus PSF remains the primary glyph source.  Only codepoints that Hibr's
unicode desktop uses and that Terminus does not provide are rasterized from
DejaVu Sans Mono.  The resulting PSF is self-contained at runtime; Pillow and
the source TTF are build-time inputs only.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


# Hibr's unicode desktop glyphs plus its wallpaper palette.  Nerd-font PUA
# glyphs are deliberately excluded: they are a separate Hibr glyph set.
REQUIRED = tuple(ord(c) for c in (
    "·░▒⋅"
    "—•…←↑→↪↻⌂⏎"
    "─━│┄┆┌┐└┘├┤═║╔╗╚╝╭╮╯╰"
    "▀▁▂▃▄▅▆▇█░▒■□▤▥▯▲▶▸▼▽▾◂◆◉●◢★☐☑♥⚑⚠✓✕✗✱❝"
))


def psf1(path: Path):
    data = path.read_bytes()
    if len(data) < 4 or data[:2] != b"\x36\x04":
        raise SystemExit(f"not a PSF1 font: {path}")
    glyphs = 512 if data[2] & 1 else 256
    height = data[3]
    end = 4 + glyphs * height
    if end > len(data):
        raise SystemExit(f"truncated PSF1 font: {path}")
    mappings = [[] for _ in range(glyphs)]
    if data[2] & 2:
        cursor = end
        glyph = 0
        while cursor + 1 < len(data) and glyph < glyphs:
            value = data[cursor] | (data[cursor + 1] << 8)
            cursor += 2
            if value == 0xFFFF:
                glyph += 1
            elif value != 0xFFFE:
                mappings[glyph].append(value)
    return [data[4 + i * height:4 + (i + 1) * height]
            for i in range(glyphs)], mappings, height


def render(codepoint: int, font: ImageFont.FreeTypeFont) -> bytes:
    # DejaVu's block-element glyphs are wider/taller than the 8x16 terminal
    # cell, so Pillow clips the lower one-eighth blocks out of the bitmap.
    # These characters are geometric terminal primitives; encode their exact
    # cell coverage instead of accepting a font-dependent crop.
    block_rows = {
        0x2580: range(0, 8),
        0x2581: range(14, 16),
        0x2582: range(12, 16),
        0x2583: range(10, 16),
        0x2584: range(8, 16),
        0x2585: range(6, 16),
        0x2586: range(4, 16),
        0x2587: range(2, 16),
        0x2588: range(0, 16),
    }
    if codepoint in block_rows:
        filled = set(block_rows[codepoint])
        return bytes(0xFF if row in filled else 0 for row in range(16))
    image = Image.new("1", (8, 16), 0)
    draw = ImageDraw.Draw(image)
    character = chr(codepoint)
    left, top, right, bottom = draw.textbbox((0, 0), character, font=font)
    width = right - left
    x = max(0, (8 - width) // 2 - left)
    # DejaVu's 16px face has a 15px em; align its baseline into the cell.
    y = max(0, 13 - bottom)
    draw.text((x, y), character, fill=1, font=font)
    pixels = image.load()
    return bytes(sum((1 << (7 - x)) for x in range(8) if pixels[x, y])
                 for y in range(16))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("fallback", type=Path)
    args = parser.parse_args()

    glyphs, mappings, height = psf1(args.base)
    if height != 16:
        raise SystemExit("base font must be 16 pixels high")
    present = {codepoint for row in mappings for codepoint in row}
    fallback = ImageFont.truetype(str(args.fallback), 16)
    added = []
    for codepoint in REQUIRED:
        if codepoint not in present:
            glyphs.append(render(codepoint, fallback))
            mappings.append([codepoint])
            present.add(codepoint)
            added.append(codepoint)

    # PSF2: magic, version, header size, flags(unicode), glyph count,
    # bytes/glyph, height, width.  Unicode tables use UTF-8 and 0xff terminates
    # each glyph's list; 0xfe is the PSF sequence separator.
    header_size = 32
    glyph_bytes = 16
    header = struct.pack("<8I", 0x864AB572, 0, header_size, 1,
                         len(glyphs), glyph_bytes, 16, 8)
    table = bytearray()
    for row in mappings:
        for codepoint in row:
            table.extend(chr(codepoint).encode("utf-8"))
        table.append(0xFF)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + b"".join(glyphs) + table)
    print(f"wrote {args.output}: {len(glyphs)} glyphs, added {len(added)}")


if __name__ == "__main__":
    main()
