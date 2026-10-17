#!/usr/bin/env python3
"""A terminal symbol must be rasterized whole, never cropped to its cell."""
import importlib.util
from pathlib import Path
from PIL import ImageFont, Image

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('font_builder', root / 'tools/build-hibr-font.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 16)
# The old builder's 10px diamond loses the right tip in its 8px canvas.
bitmap = builder.render(ord('◆'), font)
rows = [v for v in bitmap if v]
assert rows, 'diamond is blank'
assert rows == rows[::-1], f'diamond vertically truncated/asymmetric: {rows}'
assert all(f'{v:08b}'.strip('0') == f'{v:08b}'.strip('0')[::-1] for v in rows), \
    f'diamond horizontally truncated/asymmetric: {rows}'
assert all(f'{v | (v >> 1):08b}' == f'{v | (v >> 1):08b}'[::-1] for v in rows), \
    'bold diamond must remain symmetric inside its cell'
symbols = '◷▦⌨◎◐▤▣⇅◉◌⚙▭'
for ch in symbols:
    # Independent full-glyph masks: the largest size fitting the cell must
    # survive intact, not merely have some pixels in a cropped bitmap.
    for size in range(16, 0, -1):
        face = font.font_variant(size=size)
        mask = face.getmask(ch, mode='1')
        image = Image.frombytes('L', mask.size, bytes(mask))
        ink = image.crop(image.getbbox()).convert('1')
        if ink.width <= 8 and ink.height <= 16:
            break
    expected = Image.new('1', (8, 16))
    expected.paste(ink, ((8 - ink.width) // 2, max(0, 13 - ink.height)))
    got = builder.render(ord(ch), font)
    want = bytes(sum(128 >> x for x in range(8) if expected.getpixel((x,y))) for y in range(16))
    assert got == want, f'U+{ord(ch):04X} was cropped or not fitted from the complete mask'
print(f'FONT_FIT_PASS diamond=complete+symmetric+bold fitted_symbols={len(symbols)}')
