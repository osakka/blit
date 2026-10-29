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


ICONS = {
    'folder': folder, 'document': document, 'text': text, 'pdf': pdf,
    'image': image, 'audio': audio, 'video': video, 'archive': archive,
    'code': code, 'application': application, 'disk': disk, 'trash': trash,
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
