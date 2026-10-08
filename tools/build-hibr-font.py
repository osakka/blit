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
    "·░▒▓⋅"
    "—•…←↑→↪↻⌂⏎"
    "─━│┄┆┌┐└┘├┤═║╔╗╚╝╭╮╯╰"
    "▀▁▂▃▄▅▆▇█░▒■□▤▥▯▲▶▸▼▽▾◂◆◉●◢★☐☑♥⚑⚠✓✕✗✱❝"
    # Additional codepoints emitted by Hibr's executable desktop scripts:
    # controls/icons, geometry symbols, and the Matrix saver alphabet.  U+200B
    # is deliberately not included: it is a zero-width control, not a glyph.
    "±¶⇅∿≡⊞⌘⌨⎘ⓘ╱▞▣▦▭◈◌◎◐◧◫◷☺☀☾♪♫⚙⚿✉✎"
    # Common combining marks are retained as drawable overlays by the
    # terminal cell model; they must not fall back to a visible question mark.
    "́̃̊"
    "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ🔔"
    # Hibr's optional Arabic catalogue is shaped by its uni module before
    # console output. Keep both the source letters/marks and the exact
    # presentation forms emitted by mods/uni/tab.c in the runtime font.
    "«»×،؛؟ءآأؤإئابةتثجحخدذرزسشصضطظعغـفقكلمنهوىيًٍَُِّ—…"
    "ﭐﭑﭒﭓﭔﭕﭖﭗﭘﭙﭚﭛﭜﭝﭞﭟﭠﭡﭢﭣﭤﭥﭦﭧﭨﭩﭪﭫﭬﭭﭮﭯﭰﭱﭲﭳﭴﭵﭶﭷﭸﭹﭺﭻﭼﭽﭾﭿﮀﮁﮂﮃﮄﮅﮆﮇﮈﮉﮊﮋﮌﮍﮎﮏﮐﮑﮒﮓﮔﮕﮖﮗﮘﮙﮚﮛﮜﮝﮞﮟﮠﮡﮢﮣﮤﮥﮦﮧﮨﮩﮪﮫﮬﮭﮮﮯﮰﮱﯓﯔﯕﯖﯗﯘﯙﯚﯛﯜﯝﯞﯟﯠﯡﯢﯣﯤﯥﯦﯧﯨﯩﯼﯽﯾﯿﺀﺁﺂﺃﺄﺅﺆﺇﺈﺉﺊﺋﺌﺍﺎﺏﺐﺑﺒﺓﺔﺕﺖﺗﺘﺙﺚﺛﺜﺝﺞﺟﺠﺡﺢﺣﺤﺥﺦﺧﺨﺩﺪﺫﺬﺭﺮﺯﺰﺱﺲﺳﺴﺵﺶﺷﺸﺹﺺﺻﺼﺽﺾﺿﻀﻁﻂﻃﻄﻅﻆﻇﻈﻉﻊﻋﻌﻍﻎﻏﻐﻑﻒﻓﻔﻕﻖﻗﻘﻙﻚﻛﻜﻝﻞﻟﻠﻡﻢﻣﻤﻥﻦﻧﻨﻩﻪﻫﻬﻭﻮﻯﻰﻱﻲﻳﻴ"
))

BOX_CODEPOINTS = (
    0x2500, 0x2501, 0x2502, 0x2504, 0x2506,
    0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524,
    0x2550, 0x2551, 0x2554, 0x2557, 0x255A, 0x255D, 0x2560, 0x2563,
    0x256D, 0x256E, 0x256F, 0x2570,
)


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
    # Box drawing characters are terminal geometry, not ordinary text.  The
    # fallback font aliases several of these characters at 8x16, which makes
    # light, heavy, double, and dashed borders indistinguishable.  Keep their
    # topology deterministic so Hibr panels and menus cannot change shape with
    # the build host's font rasterizer.
    box = {
        0x2500: ("light", "horizontal"),  # ─
        0x2501: ("heavy", "horizontal"),  # ━
        0x2502: ("light", "vertical"),    # │
        0x2504: ("dashed", "horizontal"),  # ┄
        0x2506: ("dashed", "vertical"),    # ┆
        0x250C: ("light", "right_down"),  # ┌
        0x2510: ("light", "left_down"),   # ┐
        0x2514: ("light", "right_up"),    # └
        0x2518: ("light", "left_up"),     # ┘
        0x251C: ("light", "up_down_right"),  # ├
        0x2524: ("light", "up_down_left"),   # ┤
        0x2550: ("double", "horizontal"),  # ═
        0x2551: ("double", "vertical"),    # ║
        0x2554: ("double", "right_down"),  # ╔
        0x2557: ("double", "left_down"),   # ╗
        0x255A: ("double", "right_up"),    # ╚
        0x255D: ("double", "left_up"),     # ╝
        0x2560: ("double", "up_down_right"),  # ╠
        0x2563: ("double", "up_down_left"),   # ╣
        0x256D: ("rounded", "right_down"),  # ╭
        0x256E: ("rounded", "left_down"),   # ╮
        0x256F: ("rounded", "left_up"),     # ╯
        0x2570: ("rounded", "right_up"),    # ╰
    }
    if codepoint in box:
        style, topology = box[codepoint]
        horizontal_rows = {
            "light": (7,), "heavy": (7, 8), "double": (5, 10),
            "dashed": (7,), "rounded": (7,),
        }[style]
        vertical_columns = {
            "light": (3,), "heavy": (2, 3), "double": (2, 5),
            "dashed": (3,), "rounded": (3,),
        }[style]
        bitmap = [[0 for _ in range(8)] for _ in range(16)]

        def horizontal(rows, start, end):
            for row in rows:
                for x in range(start, end + 1):
                    bitmap[row][x] = 1

        def vertical(columns, start, end):
            for column in columns:
                for y in range(start, end + 1):
                    bitmap[y][column] = 1

        if topology == "horizontal":
            if style == "dashed":
                for row in horizontal_rows:
                    for x in (0, 1, 4, 5):
                        bitmap[row][x] = 1
            else:
                horizontal(horizontal_rows, 0, 7)
        elif topology == "vertical":
            if style == "dashed":
                for y in range(0, 16, 2):
                    for column in vertical_columns:
                        bitmap[y][column] = 1
            else:
                vertical(vertical_columns, 0, 15)
        else:
            right = topology.endswith("right") or topology == "right_down" or topology == "right_up"
            left = topology.endswith("left") or topology == "left_down" or topology == "left_up"
            down = topology.endswith("down") or topology == "up_down_right" or topology == "up_down_left"
            up = topology.endswith("up") or topology == "up_down_right" or topology == "up_down_left"
            if right:
                horizontal(horizontal_rows, 3, 7)
            if left:
                horizontal(horizontal_rows, 0, 3)
            if down:
                vertical(vertical_columns, 7, 15)
            if up:
                vertical(vertical_columns, 0, 7)
        return bytes(sum(1 << (7 - x) for x in range(8) if bitmap[y][x])
                     for y in range(16))

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

    # Terminus already maps part of this set, but its compact aliases do not
    # preserve line weight/topology. Give every box character its own mapping
    # entry so the geometric contract applies regardless of base-font aliases.
    for row in mappings:
        row[:] = [codepoint for codepoint in row
                  if codepoint not in BOX_CODEPOINTS]
    for codepoint in BOX_CODEPOINTS:
        glyphs.append(render(codepoint, fallback))
        mappings.append([codepoint])

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
    print(f"wrote {args.output}: {len(glyphs)} glyphs, added {len(added)}, normalized box glyphs={len(BOX_CODEPOINTS)}")


if __name__ == "__main__":
    main()
