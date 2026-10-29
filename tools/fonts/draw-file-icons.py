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
    return page() + [('primary', rect(8, 27, 17, 2))] + [
        ('red', rect(5, 17, 16, 9)),
        ('outline', rect(5, 17, 16, 0.8)),
        ('white', poly((8, 24), (10.5, 19), (12.5, 22.5), (14, 20.5), (17.5, 24))),
        ('ink', rect(10, 10, 12, 1.2)),
        ('ink', rect(10, 13, 9, 1.2)),
    ]


def image():
    return page() + [
        ('outline', rect(8, 10, 17, 17)),
        ('primary', rect(9, 11, 15, 15)),
        ('sky', rect(10, 12, 13, 13)),
        ('hill', poly((10, 25), (10, 20), (14, 16), (18, 20), (20, 18), (23, 21), (23, 25))),
        ('sun', poly((18, 13), (21, 13), (21, 16), (18, 16))),
    ]


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


ICONS = {
    'folder': folder, 'document': document, 'text': text, 'pdf': pdf,
    'image': image, 'audio': audio, 'video': video, 'archive': archive,
    'code': code, 'application': application, 'disk': disk, 'trash': trash,
    'home': home, 'boxer': boxer, 'markdown': markdown, 'spreadsheet': spreadsheet,
    'diagram': diagram, 'presentation': presentation, 'font': font, 'web': web,
    'terminal': terminal, 'settings': settings, 'bundle': bundle, 'link': link,
}


def svg(layers, colours, title):
    body = '\n'.join(f'  <path fill="{colours[role]}" d="{d}"/>' for role, d in layers)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">\n'
            f'  <title>{title}</title>\n{body}\n</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for variant, primary in VARIANTS.items():
        colours = palette(primary)
        for name, draw in ICONS.items():
            (args.out / f'file-{name}-{variant}.svg').write_text(
                svg(draw(), colours, f'{name}, {variant}: a two-column file icon'))
    print(f'FILE_ICONS_DRAWN icons={len(ICONS)} variants={len(VARIANTS)}')


if __name__ == '__main__':
    main()
