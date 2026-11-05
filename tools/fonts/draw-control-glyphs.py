#!/usr/bin/env python3
"""The controls glyph set (ADR 0212): the design.

Blit draws these 20 codepoints itself, as cell geometry, in
src/blit_control_glyphs.h. Every length comes from the geometry table in
that header and in src/blit_window_glyphs.h, which this script reads, so
there is one table. The rules that lay it out are written here once more,
so a gate can compare every glyph Blit rasters with the one drawn here,
pixel for pixel, at the owner's 11x22 cell and at other sizes
(control_glyph_gate).

From U+10029A, in this order:
    checkbox, Chalk then Slate: off, on, mixed (one cell each);
    compact switch, Chalk then Slate: off-left, off-right, on-left,
      on-right (two cells wide);
    capsule: off-left, off-mid, off-right, on-left, on-mid, on-right
      (three cells wide).

Roles are the window glyphs' (ADR 0211): K the foreground, L the underline
colour (SGR 58), M half the foreground and half the background, B the
background (drawn as nothing). Chalk inks every role in the foreground.

    draw-control-glyphs.py --catalogue    rewrite the catalogue's entries
    draw-control-glyphs.py --check        fail if the catalogue's entries are not what the tables give
    draw-control-glyphs.py --reference W H   print every glyph at W x H
    draw-control-glyphs.py --sheet PNG [--scale N]   an enlarged preview
"""
import argparse
import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOGUE = ROOT / 'assets/fonts/icons/catalogue.json'
HEADER = ROOT / 'src/blit_control_glyphs.h'

_spec = importlib.util.spec_from_file_location('draw_window_glyphs', ROOT / 'tools/fonts/draw-window-glyphs.py')
wd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wd)

FIRST = 0x10029A
NEXT_FREE = 0x1002AE

# id, set, what it is, how many cells it spans and which one this is.
# Every entry's pens are in part(): part, background, foreground and line.
CONTROLS = [
    ('chalk-check-off', 'chalk', 'checkbox, off: an 11x11 square on the text row', 1, 0),
    ('chalk-check-on', 'chalk', 'checkbox, checked: the square with an X', 1, 0),
    ('chalk-check-mixed', 'chalk', 'checkbox, mixed: the square with a dash', 1, 0),
    ('slate-check-off', 'slate', 'checkbox, off: the square, a face and a shade down and right', 1, 0),
    ('slate-check-on', 'slate', 'checkbox, checked: with a 2 px check mark', 1, 0),
    ('slate-check-mixed', 'slate', 'checkbox, mixed: with a heavier dash', 1, 0),
    ('chalk-switch-off-left', 'chalk', 'compact switch, off, left cell: a rounded track, the knob on the left', 2, 0),
    ('chalk-switch-off-right', 'chalk', 'compact switch, off, right cell', 2, 1),
    ('chalk-switch-on-left', 'chalk', 'compact switch, on, left cell: a filled track, the knob on the right', 2, 0),
    ('chalk-switch-on-right', 'chalk', 'compact switch, on, right cell', 2, 1),
    ('slate-switch-off-left', 'slate', 'compact switch, off, left cell: a bevelled track, the knob on the left', 2, 0),
    ('slate-switch-off-right', 'slate', 'compact switch, off, right cell', 2, 1),
    ('slate-switch-on-left', 'slate', 'compact switch, on, left cell: a mixed-grey track, the knob on the right', 2, 0),
    ('slate-switch-on-right', 'slate', 'compact switch, on, right cell', 2, 1),
    ('capsule-off-left', 'capsule', 'capsule switch, off, left cell: a pill track, the knob on the left', 3, 0),
    ('capsule-off-mid', 'capsule', 'capsule switch, off, middle cell', 3, 1),
    ('capsule-off-right', 'capsule', 'capsule switch, off, right cell', 3, 2),
    ('capsule-on-left', 'capsule', 'capsule switch, on, left cell: the knob on the right', 3, 0),
    ('capsule-on-mid', 'capsule', 'capsule switch, on, middle cell', 3, 1),
    ('capsule-on-right', 'capsule', 'capsule switch, on, right cell', 3, 2),
]
SET_NAMES = {'chalk': 'Chalk', 'slate': 'Slate', 'capsule': 'Capsule'}
# Chalk, Slate and the capsule: where they pass their pens.
PENS = {'chalk': 'foreground', 'slate': 'cell', 'capsule': 'cell'}


def ctable():
    """The controls' geometry table, read from the C header."""
    text = HEADER.read_text(encoding='utf-8')
    return {f'{m[0]}_{m[1]}': int(m[2]) for m in
            re.findall(r'#define BLIT_(CONTROL|CAPSULE)_([A-Z_]+) (\d+)\b', text)}


C = ctable()
L = wd.T['LINE']


def part(index):
    """What a glyph is and the pens an application sets for it (the
    catalogue's structured fields)."""
    name, kind, _, span, position = CONTROLS[index]
    st = kind
    if name.startswith(('chalk-check', 'slate-check')):
        what = 'checkbox'
    elif '-switch-' in name:
        what = 'switch'
    else:
        what = 'capsule'
    return {
        'part': what,
        'cells': {'span': span, 'position': position},
        # What the cell's background is: the panel the control sits on.
        'background': 'panel',
        # What the cell's foreground is: the ink, or the capsule's track.
        'foreground': 'track' if what == 'capsule' else 'ink',
        # What SGR 58 is: nothing for Chalk, else a checkbox's face, a
        # switch's knob (and a track's highlight) or the capsule's knob.
        'line': 'none' if st == 'chalk' else 'box-face' if what == 'checkbox' else 'knob',
    }


def in_stadium(px, py, width, s, shrink):
    """Is the pixel whose centre is (px, py), in half pixels, inside a
    stadium `width` px wide and s px high, its ends round, drawn in from its
    edge by `shrink` px? The ends' centres do not move as it shrinks."""
    r = s - 2 * shrink
    if r <= 0:
        return False
    dx = max(s - px, 0, px - (2 * width - s))
    dy = py - s
    return dx * dx + dy * dy <= r * r


def in_disc(px, py, cx, cy, r):
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def knob_size(s):
    """A switch's knob: the track's height less a ring and a gap each side,
    or less the ring alone where that would leave under the minimum."""
    kd = s - 2 * (L + C['CONTROL_KNOB_GAP'])
    if kd < C['CONTROL_KNOB_MIN']:
        kd = s - 2 * L
    return max(kd, 1)


def switch_role(slate, on, xs, y, w, s, by):
    """One pixel (xs along the two-cell track) of a compact switch."""
    kd = knob_size(s)
    gi = (s - kd) // 2
    ty = y - by
    kx = 2 * w - gi - kd if on else gi
    px, py, cxh = 2 * xs + 1, 2 * ty + 1, 2 * kx + kd
    if ty < 0 or ty >= s:
        return '.'
    if not in_stadium(px, py, 2 * w, s, 0):
        return '.'
    inner = in_stadium(px, py, 2 * w, s, L)
    knob = in_disc(px, py, cxh, s, kd)
    if not slate:
        # Chalk: off, a ring and a solid knob; on, a solid track with the
        # knob left out of it.
        if on:
            return '.' if knob else 'K'
        return 'K' if (not inner or knob) else '.'
    knob_inner = in_disc(px, py, cxh, s, kd - 2) if kd >= C['CONTROL_KNOB_MIN'] else knob
    if knob:
        if not knob_inner:
            return 'K'
        # The knob's face is the line pen, shaded on its last inside row.
        return 'M' if kd >= C['CONTROL_KNOB_MIN'] and ty - gi == kd - 2 else 'L'
    if not inner:
        return 'K'
    if on:
        return 'M'
    # Off: the cell's background for a face, with a highlight along the first
    # row inside the ring, stopping a pixel short of the curve.
    if ty == L and in_stadium(px - 2, py, 2 * w, s, L) and in_stadium(px + 2, py, 2 * w, s, L):
        return 'L'
    return 'B'


def checkbox(slate, state, w, h):
    """A checkbox's rows: state 0 off, 1 on, 2 mixed."""
    _, _, side, bx, by = wd.band_box(w, h)
    c = wd.Cell(w, h)
    m = C['CONTROL_MARK_INSET'] if side >= C['CONTROL_MARK_FROM'] else 1
    t = C['CONTROL_CHECK_THICK'] if side >= C['CONTROL_CHECK_FROM'] else 1
    span = side - 2 * m

    def put(x, y):
        c.fill(bx + x, bx + x + 1, by + y, by + y + 1, 'K')
    if slate:
        c.fill(bx, bx + side, by, by + side, 'L')
    c.fill(bx, bx + side, by, by + L, 'K'); c.fill(bx, bx + side, by + side - L, by + side, 'K')
    c.fill(bx, bx + L, by, by + side, 'K'); c.fill(bx + side - L, bx + side, by, by + side, 'K')
    if slate:
        # The shade: the bottom and the right inside edge.
        c.fill(bx + L, bx + side - L, by + side - 2 * L, by + side - L, 'M')
        c.fill(bx + side - 2 * L, bx + side - L, by + L, by + side - L, 'M')
    if state == 1 and not slate:
        # An X, one pixel wide, corner to corner of the marks' square.
        for i in range(span):
            put(m + i, m + i)
            put(m + i, m + span - 1 - i)
    elif state == 1:
        # A check mark, t rows thick: down to its vertex a third of the way
        # along, then up to the right; its lowest row is just above the
        # shade, and no row is above the first inside the box.
        la, yv = (span - 1) // 3, side - 2 - t
        for i in range(span):
            cy = yv - la + i if i <= la else yv - (i - la)
            for k in range(t):
                put(m + i, max(cy + k, 1))
    elif state == 2 and not slate:
        for i in range(span):
            put(m + i, side // 2)
    elif state == 2:
        # A heavier dash, centred on the face.
        x1 = max(side - 2 - m, m)
        y0 = max((side - 1 - t) // 2, 1)
        for i in range(m, x1 + 1):
            for k in range(t):
                put(i, y0 + k)
    return c.rows()


def capsule_role(on, xs, y, w, h):
    """One pixel of the capsule (xs along its three cells): the track and the
    knob counted in 8x8 sub-samples, in integers -- positions in 1/22 of
    1/16 of a pixel, so the design's lengths, in 22nds of the height,
    multiply in exactly. The knob decides first (48 samples or more of 64:
    the line pen, 16 or more: the mix, whatever lies under it); else the
    track (48 or more: the foreground, 16 or more: the mix)."""
    S, U = C['CAPSULE_SAMPLES'], C['CAPSULE_UNITS']
    u = h * 2 * S
    total = 3 * w * U * 2 * S
    cy = U * u // 2
    left = (C['CAPSULE_MARGIN'] + C['CAPSULE_RADIUS']) * u
    right = total - left
    rt = C['CAPSULE_RADIUS'] * u
    rk = C['CAPSULE_KNOB'] * u // 2
    inset = (C['CAPSULE_MARGIN'] + C['CAPSULE_KNOB_INSET']) * u
    kx = total - inset - rk if on else inset + rk
    tc = kc = 0
    for iy in range(S):
        Y = U * (2 * S * y + 2 * iy + 1)
        dy = Y - cy
        for ix in range(S):
            X = U * (2 * S * xs + 2 * ix + 1)
            dx = left - X if X < left else X - right if X > right else 0
            tc += dx * dx + dy * dy <= rt * rt
            kc += (X - kx) ** 2 + dy * dy <= rk * rk
    if kc >= C['CAPSULE_FULL']:
        return 'L'
    if kc >= C['CAPSULE_EDGE']:
        return 'M'
    if tc >= C['CAPSULE_FULL']:
        return 'K'
    if tc >= C['CAPSULE_EDGE']:
        return 'M'
    return '.'


def draw(index, w, h):
    """One glyph's role map: h strings of w characters, '.' transparent."""
    if index <= 5:
        return checkbox(index >= 3, index % 3, w, h)
    if index <= 13:
        k = index - 6
        slate, on, piece = k >= 4, (k % 4) >= 2, k & 1
        _, _, side, _, by = wd.band_box(w, h)
        return [''.join(switch_role(slate, on, piece * w + x, y, w, side, by) for x in range(w))
                for y in range(h)]
    k = index - 14
    on, piece = k >= 3, k % 3
    return [''.join(capsule_role(on, piece * w + x, y, w, h) for x in range(w)) for y in range(h)]


def glyphs():
    """Every (codepoint, index) in codepoint order."""
    for index in range(len(CONTROLS)):
        yield FIRST + index, index


def reference(w, h):
    """Every glyph as Blit's raster holds it: Chalk as coverage (one byte a
    pixel), the rest as a role map -- K, L or M, '.' where nothing is drawn
    (the background role included)."""
    lines = []
    for cp, index in glyphs():
        rows = draw(index, w, h)
        if CONTROLS[index][1] == 'chalk':
            out = bytes(0 if role == '.' else 255 for row in rows for role in row)
            lines.append(f'{cp:X} coverage {out.hex()}')
        else:
            roles = ''.join('.' if role in '.B' else role for row in rows for role in row)
            lines.append(f'{cp:X} roles {roles}')
    return '\n'.join(lines) + '\n'


def catalogue_entries():
    entries = []
    for cp, index in glyphs():
        name, kind, shape, _, _ = CONTROLS[index]
        entries.append({'id': name, 'codepoint': f'{cp:06X}', 'colour': False, 'pens': PENS[kind],
                        'set': kind, 'shape': f'{SET_NAMES[kind]} set: {shape}', **part(index)})
    return entries


def write_catalogue():
    text = CATALOGUE.read_text(encoding='utf-8')
    data = json.loads(text)
    keep = [e for e in data['cell_geometry'] if not FIRST <= int(e['codepoint'], 16) < NEXT_FREE]
    rows = sorted(keep + catalogue_entries(), key=lambda e: int(e['codepoint'], 16))
    body = ',\n'.join('    ' + json.dumps(e, ensure_ascii=False, separators=(', ', ':')) for e in rows)
    new = re.sub(r'"cell_geometry": \[\n.*?\n  \]', lambda _: '"cell_geometry": [\n' + body + '\n  ]',
                 text, flags=re.S)
    json.loads(new)
    CATALOGUE.write_text(new, encoding='utf-8')


def check_catalogue():
    """Is every one of this script's entries in the catalogue exactly what
    the tables give, structured fields included?"""
    data = json.loads(CATALOGUE.read_text(encoding='utf-8'))
    have = [e for e in data['cell_geometry'] if FIRST <= int(e['codepoint'], 16) < NEXT_FREE]
    ok = have == catalogue_entries()
    print('catalogue matches the control tables' if ok else 'the catalogue differs from the control tables: run --catalogue')
    return 0 if ok else 1


def sheet(path, scale):
    from PIL import Image, ImageDraw
    w, h, gap = 11, 22, 4
    shown = [('chalk', (0, 0, 0), (255, 255, 255), None),
             ('slate', (0, 0, 0), (221, 221, 221), (255, 255, 255)),
             ('slate', (225, 225, 225), (52, 52, 58), (112, 112, 112)),
             ('capsule', (52, 199, 89), (255, 255, 255), (255, 255, 255)),
             ('capsule', (206, 206, 212), (255, 255, 255), (255, 255, 255)),
             ('capsule', (48, 209, 88), (38, 38, 42), (255, 255, 255))]
    cols = len(CONTROLS)
    image = Image.new('RGB', (cols * (w * scale + gap) + gap, len(shown) * (h * scale + gap) + gap), (90, 110, 150))
    d = ImageDraw.Draw(image)
    for row, (kind, fg, bg, line) in enumerate(shown):
        for index, (_, k, _, _, _) in enumerate(CONTROLS):
            if k != kind:
                continue
            rows = draw(index, w, h)
            ox, oy = gap + index * (w * scale + gap), gap + row * (h * scale + gap)
            d.rectangle([ox, oy, ox + w * scale - 1, oy + h * scale - 1], fill=bg)
            for y, line_roles in enumerate(rows):
                for x, role in enumerate(line_roles):
                    if role in '.B':
                        continue
                    colour = fg if k == 'chalk' else wd.pens(fg, bg, line)[role]
                    d.rectangle([ox + x * scale, oy + y * scale, ox + x * scale + scale - 1,
                                 oy + y * scale + scale - 1], fill=colour)
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--catalogue', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--reference', nargs=2, type=int, metavar=('W', 'H'))
    parser.add_argument('--sheet')
    parser.add_argument('--scale', type=int, default=6)
    args = parser.parse_args()
    if args.catalogue:
        write_catalogue()
    if args.check:
        raise SystemExit(check_catalogue())
    if args.reference:
        print(reference(*args.reference), end='')
    if args.sheet:
        sheet(args.sheet, args.scale)


if __name__ == '__main__':
    main()
