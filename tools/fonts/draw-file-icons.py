#!/usr/bin/env python3
"""Draw Blit's file-manager icons as SVG: two-column colour badges in the
spirit of System 7 (original artwork), for Hibr's Files (ADR 0201).
Every icon carries the primary colour somewhere, so each colourway of
every kind is its own icon.

Each icon is drawn once, on a 32-unit grid, from layers that name a role
rather than a colour: outline, paper, primary, light, shade, ink, and a few
fixed accents. A variant is one primary colour; its light and shade are
derived from it, so every colourway comes from the same drawing.

    draw-file-icons.py OUT_DIR [--preview]

writes OUT_DIR/file-<icon>-<variant>.svg for every icon and variant.
"""
import argparse
import colorsys
from pathlib import Path

# The variants: one primary colour each. The first is the default.
VARIANTS = {
    'classic': '#8e99f3',   # periwinkle, a System 7 folder
    'blue': '#4c9be8',
    'green': '#57b86a',
    'amber': '#e8a93a',
}
# Every kind but a folder has one look, in its own natural colour, as in
# System 7: a house has a red roof, a key is gold, a disk is grey. Only
# folders come in the colourways above, so Files can colour them.
NATURAL = {
    'document': '#4c7fd9', 'text': '#4c7fd9', 'pdf': '#d8443c', 'image': '#b07a3c',
    'audio': '#a05cd6', 'video': '#55596a', 'archive': '#d34c4c', 'code': '#3cae5c',
    'application': '#8e6cf0', 'disk': '#5a8fd8', 'trash': '#8a8f9a', 'home': '#d0453c',
    'boxer': '#7a2430', 'markdown': '#3a3a44', 'spreadsheet': '#3fa45a', 'diagram': '#4c9be8',
    'presentation': '#e8873a', 'font': '#2f2f38', 'web': '#3f8fe0', 'terminal': '#3fbf6a',
    'settings': '#8a909c', 'bundle': '#41b8a8', 'link': '#3f8fe0', 'rc': '#3fbf6a',
    'history': '#4c7fd9', 'service': '#e8873a', 'key': '#e0b030', 'network': '#3f8fe0',
    'cron': '#d8443c', 'readme': '#3f8fe0', 'license': '#e0b030', 'log': '#7c818c',
    'kernel': '#3a9a5a', 'boot': '#3a9a5a', 'c': '#3f6fd0', 'asm': '#e8873a',
    'makefile': '#7c818c', 'patch': '#8e6cf0', 'json': '#e0b030', 'vector': '#d04c9c',
    'image-jpg': '#d8693c', 'image-png': '#3f8fe0', 'image-gif': '#8e6cf0',
    'rom': '#8a8f9a', 'ebook': '#9c5a2c', 'exe': '#3f6fd0', 'disc': '#c070e0',
    'package': '#d34c4c', 'library': '#3f8fe0', 'lock': '#e0b030', 'database': '#3f8fe0',
    'usb': '#4a5060',
}


def is_folder(name):
    return name == 'folder' or name == 'share' or name.startswith('folder-')


FIXED = {
    'outline': '#141414',
    'paper': '#f7f7f2',
    'fold': '#d9d9d2',
    'ink': '#4a4a52',
    'red': '#d8443c',
    'sky': '#8fd0f2',
    'hill': '#5aa85a',
    'sun': '#f4c542',
    'metal': '#b8bcc4',
    'metal-dark': '#7c818c',
    'cardboard': '#c99a5b',
    'led': '#4fe07a',
    'white': '#ffffff',
}


def shade_of(hex_colour, delta):
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255 for i in (1, 3, 5))
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    l = max(0.0, min(1.0, l + delta))
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return '#%02x%02x%02x' % (round(r * 255), round(g * 255), round(b * 255))


def palette(primary):
    colours = dict(FIXED)
    colours.update(primary=primary, light=shade_of(primary, 0.17), shade=shade_of(primary, -0.2))
    return colours


def rect(x, y, w, h):
    return f'M{x} {y}H{x + w}V{y + h}H{x}Z'


def poly(*points):
    return 'M' + ' L'.join(f'{x} {y}' for x, y in points) + 'Z'


def circle(cx, cy, r, steps=24, reverse=False):
    import math
    pts = [(round(cx + r * math.cos(2 * math.pi * k / steps), 2),
            round(cy + r * math.sin(2 * math.pi * k / steps), 2)) for k in range(steps)]
    return poly(*(pts[::-1] if reverse else pts))


# A 3x5 pixel font for labels on icons (PDF, JPG...): one unit per pixel.
GLYPHS = {
    'A': ['010', '101', '111', '101', '101'], 'B': ['110', '101', '110', '101', '110'],
    'C': ['011', '100', '100', '100', '011'], 'D': ['110', '101', '101', '101', '110'],
    'E': ['111', '100', '110', '100', '111'], 'F': ['111', '100', '110', '100', '100'],
    'G': ['011', '100', '101', '101', '011'], 'I': ['111', '010', '010', '010', '111'],
    'J': ['001', '001', '001', '101', '010'], 'M': ['101', '111', '101', '101', '101'],
    'N': ['110', '101', '101', '101', '101'], 'P': ['110', '101', '110', '100', '100'],
    'W': ['101', '101', '111', '111', '101'],
}


def label(text, x, y, role='white', unit=1.0):
    """Text in the label font: each letter 3 units wide, 1 apart."""
    layers = []
    for i, ch in enumerate(text):
        for r, row in enumerate(GLYPHS[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    layers.append((role, rect(x + (i * 4 + c) * unit, y + r * unit, unit, unit)))
    return layers


def tag(text):
    """A coloured band across a page's foot, with the format's name."""
    width = (len(text) * 4 - 1) * 1.2
    x = 6 + (15.5 - width) / 2
    return [('outline', rect(5, 19, 17.5, 9)), ('primary', rect(6, 20, 15.5, 7))] + \
        label(text, x, 21.5, 'white', 1.2)


def page(x0=7, x1=26, top=2, bottom=30, ear=6):
    """A dog-eared page: its outline, its paper, and its folded corner."""
    outer = poly((x0, top), (x1 - ear, top), (x1, top + ear), (x1, bottom), (x0, bottom))
    inner = poly((x0 + 1, top + 1), (x1 - ear - 0.4, top + 1), (x1 - 1, top + ear + 0.4),
                 (x1 - 1, bottom - 1), (x0 + 1, bottom - 1))
    fold = poly((x1 - ear, top + 1), (x1 - ear, top + ear), (x1 - 1, top + ear))
    crease = poly((x1 - ear - 1, top + 1), (x1 - ear, top + 1), (x1 - ear, top + ear + 1),
                  (x1 - 1, top + ear + 1), (x1 - 1, top + ear), (x1 - ear - 1, top + ear))
    return [('outline', outer), ('paper', inner), ('fold', fold), ('outline', crease)]


def folder():
    return [
        ('outline', poly((2, 6), (12, 6), (14, 9), (30, 9), (30, 28), (2, 28))),
        ('shade', poly((3, 7), (11.5, 7), (13.5, 10), (29, 10), (29, 27), (3, 27))),
        ('primary', poly((3, 12), (29, 12), (29, 26), (3, 26))),
        ('outline', rect(3, 11, 26, 1)),
        ('light', rect(3, 12, 26, 1.2)),
        ('light', rect(3, 12, 1.2, 14)),
        ('shade', rect(3, 25, 26, 1)),
    ]


def document():
    return page() + [('primary', rect(8, 26, 17, 3))]


def text():
    lines = [('ink', rect(10, y, 13 if y % 2 else 10, 1.2)) for y in (14, 17, 20, 23)]
    return page() + [('primary', rect(10, 10, 9, 2))] + lines


def pdf():
    return page() + [('ink', rect(10, 10, 12, 1.2)), ('ink', rect(10, 13, 9, 1.2)),
                     ('ink', rect(10, 16, 11, 1.2))] + tag('PDF')


def image():
    return page() + [
        ('outline', rect(8, 10, 17, 17)),
        ('primary', rect(9, 11, 15, 15)),
        ('sky', rect(10, 12, 13, 13)),
        ('hill', poly((10, 25), (10, 20), (14, 16), (18, 20), (20, 18), (23, 21), (23, 25))),
        ('sun', poly((18, 13), (21, 13), (21, 16), (18, 16))),
    ]


def picture_tagged(fmt):
    def draw():
        return page() + [
            ('outline', rect(9, 7, 14, 11)), ('sky', rect(10, 8, 12, 9)),
            ('hill', poly((10, 17), (10, 13.4), (13.4, 10.6), (16.4, 13.6), (18.6, 12), (22, 14.4), (22, 17))),
            ('sun', rect(18, 8.6, 2.4, 2.4)),
        ] + tag(fmt)
    return draw


def audio():
    return page() + [
        ('primary', rect(17, 10, 2, 11)),
        ('primary', poly((17, 10), (23, 8), (23, 11), (19, 12.5))),
        ('primary', poly((12, 20), (17, 19), (19, 21), (17, 24), (12, 24.5), (10.5, 22))),
        ('outline', rect(19, 10, 0.8, 11)),
    ]


def video():
    holes = [('paper', rect(x, 12, 1.6, 1.6)) for x in (10, 13.4, 16.8, 20.2)] + \
            [('paper', rect(x, 23, 1.6, 1.6)) for x in (10, 13.4, 16.8, 20.2)]
    return page() + [('outline', rect(9, 11, 15, 15))] + holes + [
        ('primary', rect(9, 14.5, 15, 7.5)),
        ('white', poly((14.5, 15.8), (19.5, 18.2), (14.5, 20.6))),
    ]


def archive():
    return [
        ('outline', poly((3, 10), (16, 4), (29, 10), (29, 26), (16, 30), (3, 26))),
        ('cardboard', poly((4, 10.6), (16, 5.1), (28, 10.6), (28, 25.4), (16, 29), (4, 25.4))),
        ('outline', poly((4, 10.6), (16, 16), (28, 10.6), (28, 11.6), (16, 17), (4, 11.6))),
        ('outline', rect(15.5, 16.5, 1, 12.5)),
        ('primary', poly((9, 8), (11, 7), (23, 12.6), (23, 26.3), (21, 27.2), (21, 13.6))),
        ('light', poly((9, 8), (10, 7.5), (22, 13.1), (21, 13.6))),
    ]


def code():
    return page() + [
        ('primary', poly((14, 12), (15.5, 13.4), (11.8, 17), (15.5, 20.6), (14, 22), (9, 17))),
        ('primary', poly((18, 12), (23, 17), (18, 22), (16.5, 20.6), (20.2, 17), (16.5, 13.4))),
        ('ink', rect(10, 25, 12, 1.2)),
    ]


def application():
    return [
        ('outline', poly((16, 2), (30, 16), (16, 30), (2, 16))),
        ('primary', poly((16, 3.4), (28.6, 16), (16, 28.6), (3.4, 16))),
        ('light', poly((16, 3.4), (28.6, 16), (27.2, 16), (16, 4.8), (4.8, 16), (3.4, 16))),
        ('shade', poly((3.4, 16), (16, 28.6), (28.6, 16), (27.2, 16), (16, 27.2), (4.8, 16))),
        ('white', poly((16, 9), (17.6, 14.4), (23, 16), (17.6, 17.6), (16, 23), (14.4, 17.6),
                       (9, 16), (14.4, 14.4))),
    ]


def disk():
    return [
        ('outline', rect(3, 9, 26, 15)),
        ('metal', rect(4, 10, 24, 13)),
        ('light', rect(4, 10, 24, 1.4)),
        ('metal-dark', rect(4, 19, 24, 4)),
        ('outline', rect(4, 18.5, 24, 0.8)),
        ('led', rect(23, 20.4, 3, 1.4)),
        ('ink', rect(6, 20.6, 10, 1)),
        ('primary', rect(6, 13, 8, 3)),
    ]


def trash():
    ribs = [('metal-dark', rect(x, 12, 1.2, 14)) for x in (11, 15.4, 19.8)]
    return [
        ('outline', poly((7, 9), (25, 9), (23.5, 29), (8.5, 29))),
        ('metal', poly((8, 10), (24, 10), (22.6, 28), (9.4, 28))),
        ('light', poly((8, 10), (24, 10), (23.9, 11.3), (8.1, 11.3))),
    ] + ribs + [
        ('outline', rect(5, 6, 22, 3.5)),
        ('primary', rect(6, 7, 20, 1.6)),
        ('outline', rect(13, 3.5, 6, 3)),
        ('metal', rect(14, 4.5, 4, 1.6)),
    ]


def home():
    return [
        # chimney behind the roof
        ('outline', rect(21, 5, 5, 8)),
        ('metal-dark', rect(22, 6, 3, 7)),
        # walls
        ('outline', rect(6, 15, 20, 14)),
        ('paper', rect(7, 16, 18, 12)),
        ('shade', rect(7, 26.8, 18, 1.2)),
        # a solid roof over them
        ('outline', poly((16, 3), (30.5, 16.5), (1.5, 16.5))),
        ('primary', poly((16, 4.7), (28.2, 15.5), (3.8, 15.5))),
        ('light', poly((16, 4.7), (17, 5.6), (5.1, 15.5), (3.8, 15.5))),
        ('shade', poly((3.8, 15.5), (28.2, 15.5), (27.1, 14.5), (4.9, 14.5))),
        # door and window
        ('outline', rect(9, 19, 6, 10)),
        ('primary', rect(10, 20, 4, 8)),
        ('sun', rect(12.6, 23.6, 0.9, 0.9)),
        ('outline', rect(18, 18.5, 6, 6)),
        ('sky', rect(19, 19.5, 4, 4)),
        ('outline', rect(20.6, 19.5, 0.8, 4)),
        ('outline', rect(19, 21.1, 4, 0.8)),
    ]


def boxer():
    """A Boxer game without its cover art: two boxing gloves, facing."""
    def glove(cx, side):
        # side is +1 for a glove whose thumb is on its right, -1 on its left
        fist, thumb = (cx, 11.5), (cx + side * 5.2, 15.5)
        return [
            ('outline', circle(*fist, 7.4)), ('outline', circle(*thumb, 3.6)),
            ('outline', rect(cx - 5, 17, 10, 9)),
            ('primary', circle(*fist, 6.3)), ('primary', circle(*thumb, 2.5)),
            ('light', circle(cx - side * 2.4, 8.6, 1.8)),
            ('shade', poly((cx + side * 2.6, 13.6), (cx + side * 3.4, 12.6), (cx + side * 4.4, 14.4),
                           (cx + side * 3.4, 15.6))),
            ('paper', rect(cx - 4, 18, 8, 7)),
            ('ink', rect(cx - 4, 20, 8, 0.8)), ('ink', rect(cx - 4, 22.6, 8, 0.8)),
            ('primary', rect(cx - 4, 18, 8, 1.2)),
        ]
    return glove(9, 1) + glove(23, -1) + [('outline', rect(4, 27, 24, 1.4))]


def markdown():
    return page() + [
        ('outline', rect(9, 12, 15, 10)),
        ('primary', rect(10, 13, 13, 8)),
        ('white', poly((11, 19.5), (11, 14.5), (12.6, 14.5), (14, 16.4), (15.4, 14.5), (17, 14.5),
                       (17, 19.5), (15.6, 19.5), (15.6, 17), (14, 19), (12.4, 17), (12.4, 19.5))),
        ('white', poly((19, 14.5), (20.4, 14.5), (20.4, 17), (21.6, 17), (19.7, 19.6), (17.8, 17), (19, 17))),
        ('ink', rect(10, 25, 12, 1.2)),
    ]


def spreadsheet():
    cells = [('ink', rect(9, y, 15, 0.7)) for y in (14, 17.5, 21, 24.5)] + \
            [('ink', rect(x, 10, 0.7, 17)) for x in (13.5, 18.5)]
    return page() + [('outline', rect(9, 10, 15, 17)), ('paper', rect(9.7, 10.7, 13.6, 15.6)),
                     ('primary', rect(9.7, 10.7, 13.6, 3.3))] + cells + [('light', rect(14.2, 18.2, 4.3, 2.8))]


def diagram():
    def node(x, y, w, h):
        return [('outline', rect(x, y, w, h)), ('primary', rect(x + 1, y + 1, w - 2, h - 2)),
                ('light', rect(x + 1, y + 1, w - 2, 1))]
    return [
        ('ink', rect(15.4, 10, 1.2, 6)), ('ink', rect(8, 15.4, 16, 1.2)),
        ('ink', rect(8, 15.4, 1.2, 6)), ('ink', rect(22.8, 15.4, 1.2, 6)),
    ] + node(10, 3, 12, 8) + node(2, 20, 12, 9) + node(18, 20, 12, 9)


def presentation():
    return [
        ('outline', rect(2, 4, 28, 19)),
        ('paper', rect(3, 5, 26, 17)),
        ('primary', rect(3, 5, 26, 3)),
        ('shade', rect(7, 15, 4, 5)), ('primary', rect(13, 12, 4, 8)), ('light', rect(19, 10, 4, 10)),
        ('ink', rect(6, 20, 20, 0.8)),
        ('outline', rect(15.2, 23, 1.6, 4)),
        ('outline', poly((10, 30), (15.2, 26), (16.8, 26), (22, 30), (20.4, 30), (16, 27.2), (11.6, 30))),
    ]


def font():
    return page() + [
        ('primary', poly((9, 26), (13.6, 11), (16.4, 11), (21, 26), (18.2, 26), (17.2, 22.4), (12.8, 22.4),
                         (11.8, 26))),
        ('paper', poly((13.6, 19.8), (15, 14.8), (16.4, 19.8))),
        ('ink', rect(20.5, 22, 3.5, 1)),
    ]


def web():
    return [
        ('outline', circle(16, 16, 13.5)),
        ('primary', circle(16, 16, 12.3)),
        ('light', poly((16, 3.7), (19, 4.4), (12, 6), (8, 9.2), (7.2, 8.4), (11, 5.2))),
        ('outline', rect(3.5, 15.4, 25, 1.2)),
        ('outline', rect(15.4, 3.5, 1.2, 25)),
        ('outline', poly((16, 3.7), (21, 9), (22.4, 16), (21, 23), (16, 28.3), (19.4, 22.6), (20.6, 16),
                         (19.4, 9.4))),
        ('outline', poly((16, 3.7), (11, 9), (9.6, 16), (11, 23), (16, 28.3), (12.6, 22.6), (11.4, 16),
                         (12.6, 9.4))),
        ('outline', rect(6, 9.6, 20, 1)), ('outline', rect(6, 21.4, 20, 1)),
    ]


def terminal():
    return [
        ('outline', rect(2, 5, 28, 23)),
        ('primary', rect(3, 6, 26, 4)),
        ('light', rect(3, 6, 26, 1)),
        ('ink', rect(3, 10, 26, 17)),
        ('white', poly((7, 14), (12, 17.5), (7, 21), (7, 19.4), (9.8, 17.5), (7, 15.6))),
        ('white', rect(13, 20, 6, 1.4)),
        ('paper', rect(25, 7, 2, 2)),
    ]


def settings():
    import math
    teeth = []
    for k in range(8):
        a = math.pi * 2 * k / 8
        for da, r in ((-0.22, 9.4), (-0.16, 13), (0.16, 13), (0.22, 9.4)):
            teeth.append((round(16 + r * math.cos(a + da), 2), round(16 + r * math.sin(a + da), 2)))
    outer = []
    for k in range(8):
        a = math.pi * 2 * k / 8
        outer += [(round(16 + r * math.cos(a + da), 2), round(16 + r * math.sin(a + da), 2))
                  for da, r in ((-0.22, 10.4), (-0.17, 14.2), (0.17, 14.2), (0.22, 10.4))]
    return [
        ('outline', poly(*outer)),
        ('primary', poly(*teeth)),
        ('light', circle(16, 16, 8.4)),
        ('primary', circle(16, 16, 7.4)),
        ('outline', circle(16, 16, 4)),
        ('paper', circle(16, 16, 2.8)),
    ]


def bundle():
    return [
        ('outline', poly((16, 3), (29, 9.5), (29, 23), (16, 29.5), (3, 23), (3, 9.5))),
        ('light', poly((16, 4.4), (27.4, 10), (16, 15.6), (4.6, 10))),
        ('primary', poly((4.2, 11.2), (15.4, 16.8), (15.4, 28), (4.2, 22.4))),
        ('shade', poly((16.6, 16.8), (27.8, 11.2), (27.8, 22.4), (16.6, 28))),
        ('outline', poly((15.4, 16.6), (16.6, 16.6), (16.6, 29), (15.4, 29))),
        ('paper', poly((9.5, 7.2), (21.5, 13.2), (21.5, 16.4), (19.7, 17.3), (19.7, 14.1), (7.7, 8.1))),
    ]


def link():
    return page() + [
        ('ink', rect(10, 10, 13, 1.2)), ('ink', rect(10, 13, 9, 1.2)),
        ('outline', rect(5, 17, 13, 12)),
        ('primary', rect(6, 18, 11, 10)),
        ('white', poly((8.5, 26), (8.5, 23), (10, 21.5), (12.6, 21.5), (12.6, 20), (15.4, 22.6), (12.6, 25.2),
                       (12.6, 23.7), (10.6, 23.7), (10, 24.3), (10, 26))),
    ]


# ---- emblems: a small badge drawn over the folder's lower right ----------
def badge(*glyph):
    return [('outline', rect(17, 15, 13, 13)), ('paper', rect(18, 16, 11, 11))] + list(glyph)


def gear_shape(cx, cy, r_out, r_in, teeth=8, role='primary'):
    import math
    pts = []
    for k in range(teeth):
        a = 2 * math.pi * k / teeth
        for da, r in ((-0.24, r_in), (-0.16, r_out), (0.16, r_out), (0.24, r_in)):
            pts.append((round(cx + r * math.cos(a + da), 2), round(cy + r * math.sin(a + da), 2)))
    return [(role, poly(*pts)), ('paper', circle(cx, cy, r_in * 0.45))]


def em_branch():
    return badge(('primary', circle(21, 19, 1.6)), ('primary', circle(21, 25, 1.6)),
                 ('primary', circle(26, 21, 1.6)), ('primary', rect(20.4, 19, 1.2, 6)),
                 ('primary', poly((21.6, 23), (25.4, 21.8), (25.8, 22.8), (21.6, 24.2))))


def em_gear():
    return badge(*gear_shape(23.5, 21.5, 4.6, 3.2))


def em_house():
    return badge(('primary', poly((23.5, 17.5), (28, 21.5), (19, 21.5))), ('primary', rect(20, 21.5, 7, 4.5)),
                 ('paper', rect(22.6, 23, 1.8, 3)))


def em_shield():
    return badge(('primary', poly((19.5, 17.6), (27.5, 17.6), (27.5, 21.5), (23.5, 26.4), (19.5, 21.5))),
                 ('light', poly((19.5, 17.6), (23.5, 17.6), (23.5, 26.4), (19.5, 21.5))))


def em_monitor():
    return badge(('outline', rect(19, 17.5, 9, 6.5)), ('primary', rect(19.8, 18.3, 7.4, 4.9)),
                 ('outline', rect(22.8, 24, 1.4, 1.4)), ('outline', rect(21, 25.2, 5, 0.9)))


def em_download():
    return badge(('primary', rect(22.6, 17.4, 1.8, 5)), ('primary', poly((19.8, 21.8), (27.2, 21.8), (23.5, 25.4))),
                 ('outline', rect(19.5, 25.6, 8, 0.9)))


def em_page():
    return badge(('outline', rect(20.5, 17.4, 6, 8.2)), ('paper', rect(21.2, 18.1, 4.6, 6.8)),
                 ('primary', rect(21.8, 19, 3.4, 0.9)), ('ink', rect(21.8, 21, 3.4, 0.7)), ('ink', rect(21.8, 22.8, 2.6, 0.7)))


def em_note():
    return badge(('primary', rect(24, 17.4, 1.3, 6.6)), ('primary', poly((24, 17.4), (27.6, 16.8), (27.6, 18.6), (25.3, 19.2))),
                 ('primary', circle(22.6, 24.3, 1.9)))


def em_frame():
    return badge(('outline', rect(19, 17.8, 9, 7.6)), ('sky', rect(19.8, 18.6, 7.4, 6)),
                 ('hill', poly((19.8, 24.6), (19.8, 22.4), (22, 20.6), (24.4, 22.8), (27.2, 21.4), (27.2, 24.6))),
                 ('primary', rect(24.8, 19.2, 1.6, 1.6)))


def em_film():
    return badge(('outline', rect(19, 17.6, 9, 8)), ('primary', rect(19, 19.6, 9, 4)),
                 ('white', poly((22.4, 20.2), (25.2, 21.6), (22.4, 23))))


def em_diamond():
    return badge(('outline', poly((23.5, 16.8), (28.2, 21.5), (23.5, 26.2), (18.8, 21.5))),
                 ('primary', poly((23.5, 18), (27, 21.5), (23.5, 25), (20, 21.5))))


def em_glove():
    return badge(('outline', circle(22.8, 20.4, 3.6)), ('primary', circle(22.8, 20.4, 2.8)),
                 ('outline', circle(25.6, 22.2, 1.9)), ('primary', circle(25.6, 22.2, 1.2)),
                 ('outline', rect(20.4, 23.2, 5, 3)), ('paper', rect(21, 23.6, 3.8, 2)))


def special(emblem):
    return lambda: folder() + emblem()


# ---- more files --------------------------------------------------------------
def big_letter(*layers):
    return page() + list(layers)


def c_source():
    return page() + [('primary', poly((21, 13), (19, 11.2), (14.2, 11.2), (10.5, 14.4), (10.5, 20.6), (14.2, 23.8),
                                      (19, 23.8), (21, 22), (19.5, 20.4), (18, 21.6), (15, 21.6), (13, 19.6),
                                      (13, 15.4), (15, 13.4), (18, 13.4), (19.5, 14.6))),
                     ('ink', rect(10, 26, 12, 1.2))]


def asm():
    rows = [('ink', rect(10, y, 4, 1.2)) for y in (11, 14, 17, 20, 23)] + \
           [('primary', rect(15.5, y, 7 if y % 2 else 5, 1.2)) for y in (11, 14, 17, 20, 23)]
    return page() + rows


def makefile():
    return page() + [('primary', poly((11, 24), (17.4, 17.6), (16.6, 15.2), (18.6, 13), (21.6, 12.6), (19.6, 14.6),
                                      (20.2, 16.6), (22.2, 17.2), (24.2, 15.2), (23.8, 18.2), (21.6, 20.2),
                                      (19.2, 19.4), (12.8, 25.8))),
                     ('ink', rect(10, 10, 6, 1.2))]


def readme():
    return page() + [('primary', circle(16.5, 18, 6)), ('white', rect(15.7, 17, 1.6, 5)),
                     ('white', rect(15.7, 14, 1.6, 1.6))]


def license_doc():
    return page() + [('ink', rect(10, 10, 13, 1.2)), ('ink', rect(10, 13, 10, 1.2)),
                     ('primary', poly((13, 22), (13, 29), (15, 27.4), (17, 29), (17, 22))),
                     ('primary', circle(15, 20, 4.2)), ('light', circle(15, 20, 2.6)), ('primary', circle(15, 20, 1.6))]


def log():
    rows = []
    for y in (10, 13.5, 17, 20.5, 24):
        rows += [('primary', rect(9.5, y, 3.5, 1.4)), ('ink', rect(14, y, 9 if y != 17 else 6, 1.4))]
    return page() + rows


def history():
    return page() + [('outline', circle(16.5, 18, 7)), ('paper', circle(16.5, 18, 6)),
                     ('primary', rect(15.9, 13.5, 1.2, 5)), ('primary', poly((16.5, 17.4), (20, 19.6), (19.4, 20.6), (16, 18.6))),
                     ('primary', poly((9, 12), (12, 10.6), (11.4, 13.6)))]


def rc():
    return page() + [('outline', rect(9, 10, 15, 11)), ('ink', rect(10, 11, 13, 9)),
                     ('white', poly((11.6, 13), (14.6, 15.5), (11.6, 18), (11.6, 16.7), (13.2, 15.5), (11.6, 14.3))),
                     ('white', rect(15.4, 17, 4, 1.2))] + gear_shape(19.5, 24, 3.6, 2.5)


def service():
    return gear_shape(16, 16, 13.5, 9.6, role='primary') + [
        ('outline', circle(16, 16, 6.2)), ('light', circle(16, 16, 5.2)),
        ('primary', poly((14.2, 12.8), (19.6, 16), (14.2, 19.2)))]


def key():
    return [('outline', circle(10, 16, 7.5)), ('primary', circle(10, 16, 6.3)), ('paper', circle(8.6, 16, 2.2)),
            ('outline', rect(15, 14.2, 15, 3.6)), ('primary', rect(15, 15.2, 14, 1.6)),
            ('outline', rect(22, 17.4, 2.4, 4)), ('outline', rect(26, 17.4, 2.4, 5.4)),
            ('primary', rect(22.6, 17, 1.2, 3.6)), ('primary', rect(26.6, 17, 1.2, 5))]


def network():
    return [('outline', rect(12, 3, 8, 7)), ('primary', rect(13, 4, 6, 5)),
            ('outline', rect(3, 21, 8, 7)), ('primary', rect(4, 22, 6, 5)),
            ('outline', rect(21, 21, 8, 7)), ('primary', rect(22, 22, 6, 5)),
            ('ink', rect(15.4, 10, 1.2, 6)), ('ink', rect(6.4, 15.4, 19.2, 1.2)),
            ('ink', rect(6.4, 15.4, 1.2, 5.6)), ('ink', rect(24.4, 15.4, 1.2, 5.6))]


def cron():
    return [('outline', rect(4, 6, 24, 22)), ('paper', rect(5, 7, 22, 20)), ('primary', rect(5, 7, 22, 5)),
            ('outline', rect(9, 4, 2, 5)), ('outline', rect(21, 4, 2, 5))] + \
           [('ink', rect(x, y, 3, 2.4)) for y in (14, 18, 22) for x in (7, 12, 17, 22)] + [('primary', rect(17, 18, 3, 2.4))]


def kernel():
    pins = [('outline', rect(x, 3, 1.6, 4)) for x in (10, 14, 18, 22)] + \
           [('outline', rect(x, 25, 1.6, 4)) for x in (10, 14, 18, 22)] + \
           [('outline', rect(3, y, 4, 1.6)) for y in (10, 14, 18, 22)] + \
           [('outline', rect(25, y, 4, 1.6)) for y in (10, 14, 18, 22)]
    return pins + [('outline', rect(7, 7, 18, 18)), ('primary', rect(8, 8, 16, 16)), ('light', rect(8, 8, 16, 1.4)),
                   ('shade', rect(11, 11, 10, 10)), ('primary', rect(12.4, 12.4, 7.2, 7.2))]


def boot():
    return kernel()[:-2] + [('paper', circle(16, 16, 5.6)), ('primary', circle(16, 16.6, 4.4)), ('paper', circle(16, 16.6, 3)),
                            ('paper', rect(14.8, 10, 2.4, 6)), ('primary', rect(15.4, 10.6, 1.2, 5.6))]


def patch():
    return page() + [('hill', rect(10, 11, 4, 1.4)), ('hill', rect(11.3, 9.7, 1.4, 4)), ('ink', rect(15, 11, 8, 1.2)),
                     ('red', rect(10, 16.5, 4, 1.4)), ('ink', rect(15, 16.5, 6, 1.2)),
                     ('primary', rect(10, 22, 13, 1.4)), ('primary', rect(10, 25, 9, 1.4))]


def json_doc():
    return page() + [('primary', poly((13, 11), (11, 11), (10, 12), (10, 16), (9, 17.5), (10, 19), (10, 23), (11, 24),
                                      (13, 24), (13, 22.6), (11.8, 22.6), (11.6, 22.4), (11.6, 18.8), (10.8, 17.5),
                                      (11.6, 16.2), (11.6, 12.6), (11.8, 12.4), (13, 12.4))),
                     ('primary', poly((19, 11), (21, 11), (22, 12), (22, 16), (23, 17.5), (22, 19), (22, 23), (21, 24),
                                      (19, 24), (19, 22.6), (20.2, 22.6), (20.4, 22.4), (20.4, 18.8), (21.2, 17.5),
                                      (20.4, 16.2), (20.4, 12.6), (20.2, 12.4), (19, 12.4))),
                     ('ink', circle(16, 17.5, 1.1, 10))]


def vector():
    return page() + [('ink', rect(9.4, 21.4, 3, 3)), ('ink', rect(20.6, 12.4, 3, 3)),
                     ('primary', poly((11, 22), (13.6, 15.6), (19, 13.4), (21.4, 14.4), (17.4, 16.2), (14.4, 21.4))),
                     ('outline', poly((12.6, 23.4), (20, 26), (22.4, 21), (21.2, 20.4), (19.6, 24.4), (13, 22.2)))]


def rom():
    return [('outline', poly((5, 3), (27, 3), (27, 27), (24, 29), (8, 29), (5, 27))),
            ('primary', poly((6, 4), (26, 4), (26, 26.6), (23.6, 28), (8.4, 28), (6, 26.6))),
            ('light', rect(6, 4, 20, 1.4)),
            ('outline', rect(9, 7, 14, 11)), ('paper', rect(10, 8, 12, 9)),
            ('sky', rect(10, 8, 12, 5)), ('hill', poly((10, 17), (10, 13), (13, 11), (16, 13.4), (19, 11.4), (22, 13), (22, 17))),
            ('shade', rect(8, 21, 16, 5))] + [('metal', rect(x, 21.6, 1.4, 3.8)) for x in (9.4, 12.2, 15, 17.8, 20.6)]


def ebook():
    return [('outline', poly((2, 7), (15, 5), (16, 6), (17, 5), (30, 7), (30, 27), (17, 25), (16, 26), (15, 25), (2, 27))),
            ('paper', poly((3, 8), (15, 6.2), (15, 24), (3, 25.8))),
            ('paper', poly((17, 6.2), (29, 8), (29, 25.8), (17, 24))),
            ('primary', rect(15, 6, 2, 19))] + \
           [('ink', poly((5, 11 + d), (13, 9.8 + d), (13, 10.8 + d), (5, 12 + d))) for d in (0, 3, 6, 9)] + \
           [('ink', poly((19, 9.8 + d), (27, 11 + d), (27, 12 + d), (19, 10.8 + d))) for d in (0, 3, 6, 9)]


def exe():
    return [('outline', rect(3, 5, 26, 22)), ('primary', rect(4, 6, 24, 4)), ('light', rect(4, 6, 24, 1)),
            ('paper', rect(4, 10, 24, 16)),
            ('white', rect(24.4, 7, 2.2, 2)), ('white', rect(21.4, 7, 2.2, 2)),
            ('outline', rect(7, 13, 8, 8)), ('red', rect(7.6, 13.6, 3.2, 3.2)), ('hill', rect(11.2, 13.6, 3.2, 3.2)),
            ('sky', rect(7.6, 17.2, 3.2, 3.2)), ('sun', rect(11.2, 17.2, 3.2, 3.2)),
            ('ink', rect(17, 14, 8, 1.2)), ('ink', rect(17, 17, 6, 1.2)), ('ink', rect(17, 20, 7, 1.2))]


def disc():
    return [('outline', circle(16, 16, 14, 32)), ('metal', circle(16, 16, 13, 32)),
            ('light', poly((16, 3), (24, 5.6), (17, 14), (16, 9))),
            ('primary', poly((16, 29), (8, 26.4), (15, 18), (16, 23))),
            ('outline', circle(16, 16, 4.4)), ('paper', circle(16, 16, 3.2)), ('outline', circle(16, 16, 1.6))]


def package():
    return archive() + [('paper', poly((7, 15), (13, 17.8), (13, 23), (7, 20.2))),
                        ('primary', poly((8, 17), (12, 18.9), (12, 19.9), (8, 18))),
                        ('ink', poly((8, 19.4), (12, 21.3), (12, 22), (8, 20.1)))]


def library():
    plates = []
    for i, y in enumerate((20, 13, 6)):
        plates += [('outline', poly((3, y + 3), (16, y - 2.5), (29, y + 3), (16, y + 8.5))),
                   (('primary', 'light', 'shade')[i % 3], poly((4.6, y + 3), (16, y - 1.4), (27.4, y + 3), (16, y + 7.4)))]
    return plates


def lock():
    return page() + [('outline', rect(10, 17, 13, 11)), ('primary', rect(11, 18, 11, 9)),
                     ('outline', poly((12, 17), (12, 13.5), (14, 11), (19, 11), (21, 13.5), (21, 17),
                                      (19.4, 17), (19.4, 14), (18.4, 12.6), (14.6, 12.6), (13.6, 14), (13.6, 17))),
                     ('paper', circle(16.5, 21.5, 1.4)), ('paper', rect(16, 21.5, 1, 3.4))]


def database():
    import math
    def ell(cy, rx=11, ry=3.6, steps=24, rev=False):
        pts = [(round(16 + rx * math.cos(2 * math.pi * k / steps), 2), round(cy + ry * math.sin(2 * math.pi * k / steps), 2))
               for k in range(steps)]
        return poly(*(pts[::-1] if rev else pts))
    return [('outline', rect(4, 7, 24, 18)), ('outline', ell(25, 12, 4.6)), ('outline', ell(7, 12, 4.6)),
            ('primary', rect(5, 7, 22, 18)), ('primary', ell(25, 11, 3.6)),
            ('shade', rect(5, 14.4, 22, 1)), ('shade', rect(5, 20, 22, 1)),
            ('light', ell(7, 11, 3.6))]


def usb():
    return [('outline', rect(10, 2, 12, 9)), ('metal', rect(11, 3, 10, 8)),
            ('outline', rect(13, 5, 2, 2)), ('outline', rect(17, 5, 2, 2)),
            ('outline', rect(8, 10, 16, 20)), ('primary', rect(9, 11, 14, 18)), ('light', rect(9, 11, 1.4, 18)),
            ('white', rect(15.4, 15, 1.2, 9)), ('white', poly((14, 15.6), (16, 13), (18, 15.6))),
            ('white', rect(12.4, 19, 1.2, 3)), ('white', rect(18.4, 18, 1.2, 3)),
            ('white', rect(12.4, 21.4, 4, 1.2)), ('white', rect(16, 20.4, 3.6, 1.2))]


def share():
    return folder() + [('outline', rect(15.4, 24, 1.2, 4)), ('outline', rect(6, 28, 20, 1.6)),
                       ('primary', circle(16, 28.8, 2.2))]


MORE = {
    'folder-git': special(em_branch), 'folder-config': special(em_gear), 'folder-local': special(em_house),
    'folder-system': special(em_shield), 'folder-desktop': special(em_monitor),
    'folder-downloads': special(em_download), 'folder-documents': special(em_page),
    'folder-music': special(em_note), 'folder-pictures': special(em_frame), 'folder-videos': special(em_film),
    'folder-apps': special(em_diamond), 'folder-games': special(em_glove),
    'rc': rc, 'history': history, 'service': service, 'key': key, 'network': network, 'cron': cron,
    'readme': readme, 'license': license_doc, 'log': log, 'kernel': kernel, 'boot': boot,
    'c': c_source, 'asm': asm, 'makefile': makefile, 'patch': patch, 'json': json_doc, 'vector': vector,
    'rom': rom, 'ebook': ebook, 'exe': exe, 'disc': disc, 'package': package, 'library': library,
    'lock': lock, 'database': database, 'usb': usb, 'share': share,
}

ICONS = {
    'folder': folder, 'document': document, 'text': text, 'pdf': pdf,
    'image': image, 'audio': audio, 'video': video, 'archive': archive,
    'code': code, 'application': application, 'disk': disk, 'trash': trash,
    'home': home, 'image-jpg': picture_tagged('JPG'), 'image-png': picture_tagged('PNG'),
    'image-gif': picture_tagged('GIF'), 'boxer': boxer, 'markdown': markdown, 'spreadsheet': spreadsheet,
    'diagram': diagram, 'presentation': presentation, 'font': font, 'web': web,
    'terminal': terminal, 'settings': settings, 'bundle': bundle, 'link': link,
}
ICONS.update(MORE)


def svg(layers, colours, title):
    body = '\n'.join(f'  <path fill="{colours[role]}" d="{d}"/>' for role, d in layers)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">\n'
            f'  <title>{title}</title>\n{body}\n</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    folders = [name for name in ICONS if is_folder(name)]
    others = [name for name in ICONS if not is_folder(name)]
    missing = [name for name in others if name not in NATURAL]
    if missing:
        raise SystemExit(f'no natural colour for {missing}')
    for variant, primary in VARIANTS.items():
        colours = palette(primary)
        for name in folders:
            (args.out / f'file-{name}-{variant}.svg').write_text(
                svg(ICONS[name](), colours, f'{name}, {variant}: a two-column folder icon'))
    for name in others:
        (args.out / f'file-{name}.svg').write_text(
            svg(ICONS[name](), palette(NATURAL[name]), f'{name}: a two-column file icon'))
    print(f'FILE_ICONS_DRAWN folders={len(folders)}x{len(VARIANTS)} others={len(others)}')


if __name__ == '__main__':
    main()
