#!/usr/bin/env python3
"""The window-chrome glyph sets, Chalk and Slate (ADR 0210): the design.

Blit draws these 105 codepoints itself, as cell geometry, in
src/blit_window_glyphs.h. Every length comes from the geometry table in
that header, which this script reads, so there is one table. The rules
that lay the table out are written here once more, so a gate can compare
every glyph Blit rasters with the one drawn here, pixel for pixel, at the
owner's 11x22 cell and at other sizes (window_glyph_gate).

Three sets of 35, in one order: Chalk (one colour: the cell's foreground
over its background, so a theme recolours it) at U+100231, Slate light at
U+100254 and Slate dark at U+100277: the same geometry in Slate's own
colours. A pixel no rule sets is transparent in both: the cell's
background shows, which for a frame cell on a held desktop is the
wallpaper.

Pixel-designed for 11x22. At other sizes lines, stripes, the shadow and
the rings keep their widths in pixels; the box (the cell's width), the
number of stripes (six at 22 px, in proportion) and a button end's radius
scale with the cell.

    draw-window-glyphs.py --catalogue    rewrite the catalogue's entries
    draw-window-glyphs.py --reference W H   print every glyph at W x H
    draw-window-glyphs.py --sheet PNG [--scale N]   an enlarged preview
"""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOGUE = ROOT / 'assets/fonts/icons/catalogue.json'

# One set's 35 glyphs, in codepoint order, with what each one is.
NAMES = [
    ('stripe', 'title bar: frame line, six stripes, rule'),
    ('stripe-first', 'title bar next to the left frame: stripes start 2 px in'),
    ('stripe-last', 'title bar next to the right frame: stripes stop 2 px short'),
    ('stripe-before-title', 'title bar before the title: stripes stop 5 px short'),
    ('stripe-after-title', 'title bar after the title: stripes start 5 px in'),
    ('title', 'title bar without stripes: under the title, and the inactive bar'),
    ('stripe-to-box', 'title bar before a box: stripes stop 2 px short'),
    ('stripe-from-box', 'title bar after a box: stripes start 2 px in'),
    ('stripe-between', 'title bar between two boxes: 2 px clear each side'),
    ('close', 'title bar with the close box: an 11x11 square, the cell\'s width'),
    ('shade', 'title bar with the window-shade box: the square with a bar across'),
    ('zoom', 'title bar with the zoom box: the square with a smaller square in its corner'),
    ('frame-left', 'window frame, left: a line against the content'),
    ('frame-right-top', 'window frame, right, beside the title bar: line, shadow from 2 px down'),
    ('frame-right', 'window frame, right: line and its 2 px shadow'),
    ('frame-bottom-left', 'window frame, bottom-left corner'),
    ('frame-bottom-first', 'window frame, first bottom cell: line, shadow from 2 px in'),
    ('frame-bottom', 'window frame, bottom: line and its 2 px shadow'),
    ('frame-bottom-right', 'window frame, bottom-right corner and shadow'),
    ('frame-top-left', 'frame without a title bar, top-left corner'),
    ('frame-top', 'frame without a title bar, top: a line against the content'),
    ('frame-top-right', 'frame without a title bar, top-right corner'),
    ('note-close', 'a notification\'s close box alone, transparent around it'),
    ('dialog-top-left', 'dialog double border, top-left corner'),
    ('dialog-top', 'dialog double border, top'),
    ('dialog-top-right', 'dialog double border, top-right corner, shadow from 2 px down'),
    ('dialog-left', 'dialog double border, left'),
    ('dialog-right', 'dialog double border, right, with its shadow'),
    ('dialog-bottom-left', 'dialog double border, bottom-left corner, shadow from 2 px in'),
    ('dialog-bottom', 'dialog double border, bottom, with its shadow'),
    ('dialog-bottom-right', 'dialog double border, bottom-right corner and shadow'),
    ('button-left', 'button, left end: rounded 1 px outline; SGR 51 draws the label\'s lines'),
    ('button-right', 'button, right end'),
    ('default-button-left', 'default button, left end: rounded 3 px ring; SGR 51:3 draws the label\'s'),
    ('default-button-right', 'default button, right end'),
]
STYLES = [('chalk', 0x100231), ('slate-light', 0x100254), ('slate-dark', 0x100277)]
NEXT_FREE = 0x10029A

HEADER = ROOT / 'src/blit_window_glyphs.h'
ROLES = 'KDLBWSGFNORH'


def table():
    """The geometry table and the palettes, read from the C header."""
    text = HEADER.read_text(encoding='utf-8')
    t = {m[0]: int(m[1]) for m in re.findall(r'#define BLIT_WINDOW_([A-Z_]+) (\d+)\b', text)}
    block = text[text.index('blit_window_palette[2][WR_ROLES][3] = {'):]
    block = block[:block.index('};')]
    triples = [tuple(int(v) for v in m) for m in re.findall(r'\{(\d+), (\d+), (\d+)\}', block)]
    count = len(ROLES) + 1
    assert len(triples) == 2 * count, 'palette table not found'
    palettes = {}
    for set_index, style in enumerate(('slate-light', 'slate-dark')):
        palettes[style] = {role: triples[set_index * count + 1 + i] for i, role in enumerate(ROLES)}
    return t, palettes


T, PALETTES = table()


def inside_round(x, y, x0, x1, y0, y1, r):
    """Is pixel (x, y)'s centre inside the rounded rectangle [x0, x1) x
    [y0, y1) with corner radius r? In half pixels, so it stays integer."""
    px, py = 2 * x + 1, 2 * y + 1
    if px < 2 * x0 or px > 2 * x1 or py < 2 * y0 or py > 2 * y1:
        return False
    dx = max(2 * (x0 + r) - px, 0, px - 2 * (x1 - r))
    dy = max(2 * (y0 + r) - py, 0, py - 2 * (y1 - r))
    return dx * dx + dy * dy <= 4 * r * r + 2 * r


def round_normal(x, y, x0, x1, y0, y1, r):
    """Which way the rounded rectangle's edge faces at (x, y): the centre's
    offset from the nearest point of its inner rectangle, in half pixels."""
    px, py = 2 * x + 1, 2 * y + 1
    vx = px - 2 * (x0 + r) if px < 2 * (x0 + r) else px - 2 * (x1 - r) if px > 2 * (x1 - r) else 0
    vy = py - 2 * (y0 + r) if py < 2 * (y0 + r) else py - 2 * (y1 - r) if py > 2 * (y1 - r) else 0
    return vx, vy


class Cell:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[None] * w for _ in range(h)]

    def fill(self, x0, x1, y0, y1, role):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.px[y][x] = role

    def rows(self):
        return [''.join(r or '.' for r in row) for row in self.px]


def draw(style, index, w, h):
    """One glyph's role map: h strings of w characters, '.' transparent."""
    name = NAMES[index][0]
    slate = style != 'chalk'
    L, inset = T['LINE'], T['STRIPE_INSET']
    sw, so = T['SHADOW'], T['SHADOW_OFFSET']
    d1 = T['DIALOG_MARGIN']
    d2 = d1 + T['DIALOG_BAND']
    d3 = d2 + T['DIALOG_GAP']
    d4 = d3 + T['DIALOG_OUTER']
    c = Cell(w, h)
    ink = 'K'
    # The title bar's stripes: L thick, L apart, six at 22 px and as many
    # in proportion at other heights, centred.
    n = (T['STRIPES'] * h + T['DESIGN_HEIGHT'] // 2) // T['DESIGN_HEIGHT']
    n = max(1, min(n, (h - 4 * L) // (2 * L)))
    band = 2 * L * n
    top = (h - band) // 2
    # A box: square, the cell's width, centred on the stripes.
    side = min(w, band - L)
    bx, by = (w - side) // 2, top + (band - L - side) // 2

    def bar(x0=0, x1=w, stripes=True):
        if slate:
            c.fill(0, w, 0, h, 'B')
        if stripes:
            for k in range(n):
                y = top + 2 * L * k
                c.fill(x0, x1, y, y + L, 'D' if slate else ink)
                if slate:
                    c.fill(x0, x1, y + L, y + 2 * L, 'L')
        # The frame line and the rule under the bar; Slate's bevel is a
        # highlight under the line and a shade over the rule.
        c.fill(0, w, 0, L, ink)
        c.fill(0, w, h - L, h, ink)
        if slate:
            c.fill(0, w, L, 2 * L, 'L')
            c.fill(0, w, h - 2 * L, h - L, 'D')

    def outline(x0, x1, y0, y1):
        c.fill(x0, x1, y0, y0 + L, ink); c.fill(x0, x1, y1 - L, y1, ink)
        c.fill(x0, x0 + L, y0, y1, ink); c.fill(x1 - L, x1, y0, y1, ink)

    def box(kind):
        if slate:
            c.fill(bx, bx + side, by, by + side, 'W')
        outline(bx, bx + side, by, by + side)
        if kind == 'zoom':
            inner = (T['ZOOM_NUM'] * side + T['ZOOM_DEN'] // 2) // T['ZOOM_DEN']
            outline(bx, bx + inner, by, by + inner)
        elif kind == 'shade':
            y = by + (side - 3 * L) // 2
            c.fill(bx, bx + side, y, y + L, ink)

    def dialog(distance, shadow):
        # The double border, outward from the content: a margin of the
        # face, the wide line (Slate's grey with a highlight down its
        # middle), a gap of the face, the outer line; then the shadow.
        mid = d1 + (d2 - d1) // 2
        for y in range(h):
            for x in range(w):
                d = distance(x, y)
                role = None
                if d < d1 or d2 <= d < d3:
                    role = 'G' if slate else None
                elif d < d2:
                    role = ('H' if d == mid else 'R') if slate else ink
                elif d < d4:
                    role = 'O' if slate else ink
                elif shadow(x, y):
                    role = 'S'
                if role:
                    c.px[y][x] = role

    def button(right, thick):
        # A rounded end, its straight top and bottom running to the edge the
        # label's SGR 51 lines continue; L thick, or the default's ring.
        t = T['THICK_RING'] if thick else L
        x0 = T['BUTTON_INSET']
        r = max(0, min(w - x0, h // 2, T['BUTTON_RADIUS_NUM'] * h // T['BUTTON_RADIUS_DEN']))
        # The inner edges' radii, square once a small cell's ring is as
        # thick as its end is round.
        r1 = max(0, r - t)
        r2 = max(0, r1 - L)
        x1 = w + 2 * r
        for y in range(h):
            for x in range(w):
                mx = w - 1 - x if right else x
                if not inside_round(mx, y, x0, x1, 0, h, r):
                    continue
                if not inside_round(mx, y, x0 + t, x1 - t, t, h - t, r1):
                    c.px[y][x] = 'O' if slate else ink
                elif slate:
                    # The face, and a bevel round the end only (the label's
                    # cells between the ends are flat): light where the edge
                    # faces up and left, shade where it faces down and right.
                    role = 'F'
                    if not inside_round(mx, y, x0 + t + L, x1 - t - L, t + L, h - t - L, r2):
                        vx, vy = round_normal(mx, y, x0 + t, x1 - t, t, h - t, r1)
                        if vx:
                            vx = -vx if right else vx
                            role = 'L' if vx + vy < 0 else 'N'
                    c.px[y][x] = role

    gap = T['TITLE_GAP']
    stripes = {
        'stripe': (0, w), 'stripe-first': (inset, w), 'stripe-last': (0, w - inset),
        'stripe-before-title': (0, w - gap), 'stripe-after-title': (gap, w),
        'stripe-to-box': (0, w - inset), 'stripe-from-box': (inset, w),
        'stripe-between': (inset, w - inset),
    }
    if name in stripes:
        bar(*stripes[name])
    elif name == 'title':
        bar(stripes=False)
    elif name in ('close', 'shade', 'zoom'):
        bar(stripes=False)
        box(name)
    elif name == 'note-close':
        box('close')
    elif name == 'frame-left':
        c.fill(w - L, w, 0, h, ink)
    elif name in ('frame-right-top', 'frame-right'):
        c.fill(0, L, 0, h, ink)
        c.fill(L, L + sw, so if name == 'frame-right-top' else 0, h, 'S')
    elif name == 'frame-bottom-left':
        c.fill(w - L, w, 0, L, ink)
    elif name in ('frame-bottom-first', 'frame-bottom'):
        c.fill(0, w, 0, L, ink)
        # The left frame's outer edge is L into the previous cell.
        c.fill(so - L if name == 'frame-bottom-first' else 0, w, L, L + sw, 'S')
    elif name == 'frame-bottom-right':
        c.fill(0, L + sw, 0, L + sw, 'S')
        c.fill(0, L, 0, L, ink)
    elif name == 'frame-top-left':
        c.fill(w - L, w, h - L, h, ink)
    elif name == 'frame-top':
        c.fill(0, w, h - L, h, ink)
    elif name == 'frame-top-right':
        c.fill(0, L, h - L, h, ink)
    elif name.startswith('dialog-'):
        parts = name[len('dialog-'):].split('-')
        left, right = parts[-1] == 'left', parts[-1] == 'right'
        upper, lower = parts[0] == 'top', parts[0] == 'bottom'

        def distance(x, y):
            dx = w - 1 - x if left else x if right else 0
            dy = h - 1 - y if upper else y if lower else 0
            return max(dx, dy)

        # The shadow: beside the right and under the bottom, so from the
        # outer line's far corners.
        def shadow(x, y):
            if right and x < d4 + sw:
                return y >= h - d4 + so if upper else (not lower or y < d4 + sw)
            if lower and y < d4 + sw:
                return x >= w - d4 + so if left else (not right or x < d4 + sw)
            return False
        dialog(distance, shadow)
    elif name.startswith('button-') or name.startswith('default-button-'):
        button(name.endswith('-right'), name.startswith('default-'))
    return c.rows()


def glyphs():
    """Every (codepoint, style, index) in codepoint order."""
    for style, first in STYLES:
        for index in range(len(NAMES)):
            yield first + index, style, index


def colour(style, role, foreground=(255, 255, 255)):
    return foreground if style == 'chalk' else PALETTES[style][role]


def reference(w, h):
    """Every glyph as Blit's raster holds it: Chalk as coverage (one byte a
    pixel, 255 or 0), Slate as premultiplied RGBA (opaque or 0)."""
    lines = []
    for cp, style, index in glyphs():
        rows = draw(style, index, w, h)
        out = bytearray()
        for row in rows:
            for role in row:
                if style == 'chalk':
                    out.append(0 if role == '.' else 255)
                else:
                    out.extend((0, 0, 0, 0) if role == '.' else (*PALETTES[style][role], 255))
        lines.append(f'{cp:X} {"rgba" if style != "chalk" else "coverage"} {out.hex()}')
    return '\n'.join(lines) + '\n'


def catalogue_entries():
    entries = []
    for cp, style, index in glyphs():
        name, shape = NAMES[index]
        set_name = {'chalk': 'Chalk', 'slate-light': 'Slate light', 'slate-dark': 'Slate dark'}[style]
        entries.append({'id': f'{style}-{name}', 'codepoint': f'{cp:06X}',
                        'colour': style != 'chalk', 'set': style,
                        'shape': f'{set_name} set: {shape}'})
    return entries


def write_catalogue():
    text = CATALOGUE.read_text(encoding='utf-8')
    data = json.loads(text)
    keep = [e for e in data['cell_geometry'] if not any(e['id'].startswith(s + '-') for s, _ in STYLES)]
    rows = keep + catalogue_entries()
    body = ',\n'.join('    ' + json.dumps(e, ensure_ascii=False, separators=(', ', ':'))
                      for e in rows)
    new = re.sub(r'"cell_geometry": \[\n.*?\n  \]', lambda _: '"cell_geometry": [\n' + body + '\n  ]',
                 text, flags=re.S)
    json.loads(new)
    CATALOGUE.write_text(new, encoding='utf-8')


def sheet(path, scale):
    from PIL import Image, ImageDraw
    w, h = 11, 22
    gap = 4
    cols = len(NAMES)
    image = Image.new('RGB', (cols * (w * scale + gap) + gap, 3 * (h * scale + gap) + gap), (90, 110, 150))
    d = ImageDraw.Draw(image)
    for row, (style, _) in enumerate(STYLES):
        for index in range(cols):
            rows = draw(style, index, w, h)
            ox, oy = gap + index * (w * scale + gap), gap + row * (h * scale + gap)
            for y, line in enumerate(rows):
                for x, role in enumerate(line):
                    fill = (150, 170, 205) if role == '.' else colour(style, role, (0, 0, 0))
                    d.rectangle([ox + x * scale, oy + y * scale, ox + x * scale + scale - 1,
                                 oy + y * scale + scale - 1], fill=fill)
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--catalogue', action='store_true')
    parser.add_argument('--reference', nargs=2, type=int, metavar=('W', 'H'))
    parser.add_argument('--sheet')
    parser.add_argument('--scale', type=int, default=6)
    args = parser.parse_args()
    if args.catalogue:
        write_catalogue()
    if args.reference:
        print(reference(*args.reference), end='')
    if args.sheet:
        sheet(args.sheet, args.scale)


if __name__ == '__main__':
    main()
