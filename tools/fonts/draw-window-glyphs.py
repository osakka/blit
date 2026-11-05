#!/usr/bin/env python3
"""The window-chrome glyph sets, Chalk and Slate (ADR 0210): the design.

Blit draws these 70 codepoints itself, as cell geometry, in
src/blit_window_glyphs.h. Every length comes from the geometry table in
that header, which this script reads, so there is one table. The rules
that lay the table out are written here once more, so a gate can compare
every glyph Blit rasters with the one drawn here, pixel for pixel, at the
owner's 11x22 cell and at other sizes (window_glyph_gate).

Two sets of 35, in one order: Chalk (one colour: the cell's foreground
over its background, so a theme recolours it) at U+100231 and Slate at
U+100254: the same geometry in the cell's own pens (ADR 0211). Slate dark,
once at U+100277-100299, is retired. A pixel no rule sets is transparent in
both: the cell's background shows, which for a frame cell on a held desktop
is the wallpaper. The controls that follow (U+10029A-1002AD) are
draw-control-glyphs.py's.

The outer-edge frame (ADR 0214), U+1002AE-1002B6, is here too: ONE frame
set (the line on the outermost edge of each frame cell, nothing else
drawn), and the title bar's four end cells (and, from 0.3.45, the four plain ones of the inactive bar), Chalk's and Slate's, which
carry that outer line and run the stripes into the cell. The frame glyphs
and stripe-first/-last of the two sets above are deprecated (the catalogue
says so, and what replaces them); they draw unchanged until Hibr moves.

Pixel-designed for 11x22. At other sizes lines, stripes and the rings keep
their widths in pixels; the box (the cell's width), the number of stripes
(six at 22 px, in proportion) and a button end's radius scale with the
cell.

    draw-window-glyphs.py --catalogue    rewrite the catalogue's entries
    draw-window-glyphs.py --check        fail if the catalogue's entries are not what the tables give
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
    ('frame-right-top', 'window frame, right, beside the title bar: a line against the content (as frame-right)'),
    ('frame-right', 'window frame, right: a line against the content'),
    ('frame-bottom-left', 'window frame, bottom-left corner'),
    ('frame-bottom-first', 'window frame, first bottom cell: a line against the content (as frame-bottom)'),
    ('frame-bottom', 'window frame, bottom: a line against the content'),
    ('frame-bottom-right', 'window frame, bottom-right corner'),
    ('frame-top-left', 'frame without a title bar, top-left corner'),
    ('frame-top', 'frame without a title bar, top: a line against the content'),
    ('frame-top-right', 'frame without a title bar, top-right corner'),
    ('note-close', 'a notification\'s close box alone, transparent around it'),
    ('dialog-top-left', 'dialog double border, top-left corner'),
    ('dialog-top', 'dialog double border, top'),
    ('dialog-top-right', 'dialog double border, top-right corner'),
    ('dialog-left', 'dialog double border, left'),
    ('dialog-right', 'dialog double border, right'),
    ('dialog-bottom-left', 'dialog double border, bottom-left corner'),
    ('dialog-bottom', 'dialog double border, bottom'),
    ('dialog-bottom-right', 'dialog double border, bottom-right corner'),
    ('button-left', 'button, left end: rounded 1 px outline; SGR 51 draws the label\'s lines'),
    ('button-right', 'button, right end'),
    ('default-button-left', 'default button, left end: rounded 3 px ring; SGR 51:3 draws the label\'s'),
    ('default-button-right', 'default button, right end'),
]
STYLES = [('chalk', 0x100231), ('slate-light', 0x100254)]
# Slate dark took U+100277-100299 until ADR 0211; retired, never reused.
RETIRED = (0x100277, 0x100299)

# ADR 0214: the outer-edge frame, thirteen glyphs in this order. The first five
# are the one frame set (one colour, coverage in the foreground); the title
# ends are the bar's end cells in each title-bar style.
FRAME_FIRST = 0x1002AE
FRAMES = [
    ('frame-left', 'frame', 'window frame, left: the line on the cell\'s left edge, nothing else'),
    ('frame-right', 'frame', 'window frame, right: the line on the cell\'s right edge'),
    ('frame-bottom-left', 'frame', 'window frame, bottom-left corner: the left and bottom edges'),
    ('frame-bottom', 'frame', 'window frame, bottom: the line on the cell\'s bottom edge'),
    ('frame-bottom-right', 'frame', 'window frame, bottom-right corner: the right and bottom edges'),
    ('chalk-title-end-left', 'chalk', 'title bar, left end cell: the outer line, the bar\'s top line and rule, stripes from 2 px in'),
    ('chalk-title-end-right', 'chalk', 'title bar, right end cell: the outer line, the bar\'s top line and rule, stripes to 2 px short'),
    ('slate-light-title-end-left', 'slate-light', 'title bar, left end cell: the outer line, the bar\'s bevel, stripes from 2 px in'),
    ('slate-light-title-end-right', 'slate-light', 'title bar, right end cell: the outer line, the bar\'s bevel, stripes to 2 px short'),
    # 0.3.45: the inactive bar's end cells, the same without stripes.
    ('chalk-title-end-plain-left', 'chalk', 'inactive title bar, left end cell: the outer line, the bar\'s top line and rule, no stripes'),
    ('chalk-title-end-plain-right', 'chalk', 'inactive title bar, right end cell: the outer line, the bar\'s top line and rule, no stripes'),
    ('slate-light-title-end-plain-left', 'slate-light', 'inactive title bar, left end cell: the outer line, the bar\'s bevel, no stripes'),
    ('slate-light-title-end-plain-right', 'slate-light', 'inactive title bar, right end cell: the outer line, the bar\'s bevel, no stripes'),
]
# The glyphs that ADR 0214 deprecates: each old frame piece and the stripe
# cells beside the frame, and what an application draws instead (the title
# row's cells are the title ends).
DEPRECATED = {}
for _style in ('chalk', 'slate-light'):
    DEPRECATED.update({
        f'{_style}-frame-left': ['frame-left', f'{_style}-title-end-left'],
        f'{_style}-frame-right-top': [f'{_style}-title-end-right'],
        f'{_style}-frame-right': ['frame-right'],
        f'{_style}-frame-bottom-left': ['frame-bottom-left'],
        f'{_style}-frame-bottom-first': ['frame-bottom'],
        f'{_style}-frame-bottom': ['frame-bottom'],
        f'{_style}-frame-bottom-right': ['frame-bottom-right'],
        f'{_style}-stripe-first': [f'{_style}-title-end-left'],
        f'{_style}-stripe-last': [f'{_style}-title-end-right'],
    })

HEADER = ROOT / 'src/blit_window_glyphs.h'
# Slate's roles (ADR 0211), resolved in the cell it is drawn in: K the
# foreground, L the underline colour (white if unset; SGR 58 is also a
# dialog's and a button's face), M half the foreground and half the
# background, B the background (drawn as nothing; only the bar has it).
# Chalk inks every role in the foreground.
ROLES = 'KLMB'


def table():
    """The geometry table, read from the C header."""
    text = HEADER.read_text(encoding='utf-8')
    return {m[0]: int(m[1]) for m in re.findall(r'#define BLIT_WINDOW_([A-Z_]+) (\d+)\b', text)}


def pens(fg, bg, line=None):
    """A Slate cell's pens: fg, line (white if unset) and the mix, each
    channel (fg + bg) // 2, as blit_window_pens."""
    return {'K': fg, 'L': line if line is not None else (255, 255, 255),
            'M': tuple((a + b) // 2 for a, b in zip(fg, bg)), 'B': bg}


T = table()


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


def band_box(w, h):
    """The title bar's stripes and the box among them, as blit_window_band:
    n stripes (L thick and L apart, six at 22 px and as many in proportion
    at other heights, at least one), centred in a band of 2n lines; and the
    box, square, the cell's width or the band less a line, centred on the
    stripes. The checkbox and switch (ADR 0212) stand on it too."""
    L = T['LINE']
    n = (T['STRIPES'] * h + T['DESIGN_HEIGHT'] // 2) // T['DESIGN_HEIGHT']
    n = max(1, min(n, (h - 4 * L) // (2 * L)))
    band = 2 * L * n
    top = (h - band) // 2
    side = min(w, band - L)
    return n, top, side, (w - side) // 2, top + (band - L - side) // 2


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
    d1 = T['DIALOG_MARGIN']
    d2 = d1 + T['DIALOG_BAND']
    d3 = d2 + T['DIALOG_GAP']
    d4 = d3 + T['DIALOG_OUTER']
    c = Cell(w, h)
    ink = 'K'
    # The title bar's stripes and the box among them.
    n, top, side, bx, by = band_box(w, h)

    def bar(x0=0, x1=w, stripes=True):
        if slate:
            c.fill(0, w, 0, h, 'B')
        if stripes:
            for k in range(n):
                y = top + 2 * L * k
                c.fill(x0, x1, y, y + L, 'M' if slate else ink)
                if slate:
                    c.fill(x0, x1, y + L, y + 2 * L, 'L')
        # The frame line and the rule under the bar; Slate's bevel is a
        # highlight under the line and a shade over the rule.
        c.fill(0, w, 0, L, ink)
        c.fill(0, w, h - L, h, ink)
        if slate:
            c.fill(0, w, L, 2 * L, 'L')
            c.fill(0, w, h - 2 * L, h - L, 'M')

    def outline(x0, x1, y0, y1):
        c.fill(x0, x1, y0, y0 + L, ink); c.fill(x0, x1, y1 - L, y1, ink)
        c.fill(x0, x0 + L, y0, y1, ink); c.fill(x1 - L, x1, y0, y1, ink)

    def box(kind):
        if slate:
            c.fill(bx, bx + side, by, by + side, 'L')
        outline(bx, bx + side, by, by + side)
        if kind == 'zoom':
            inner = (T['ZOOM_NUM'] * side + T['ZOOM_DEN'] // 2) // T['ZOOM_DEN']
            outline(bx, bx + inner, by, by + inner)
        elif kind == 'shade':
            y = by + (side - 3 * L) // 2
            c.fill(bx, bx + side, y, y + L, ink)

    def dialog(distance):
        # The double border, outward from the content: a margin of the
        # face (Slate: the line pen, SGR 58 the dialog's face), the wide
        # line, a gap of the face, the outer line; nothing beyond it.
        for y in range(h):
            for x in range(w):
                d = distance(x, y)
                role = None
                if d < d1 or d2 <= d < d3:
                    role = 'L' if slate else None
                elif d < d4:
                    role = ink
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
                    c.px[y][x] = ink
                elif slate:
                    # The face (the line pen, SGR 58 the button's face), and
                    # a shade round the end only where its edge faces down
                    # or right; the label's cells between the ends are flat.
                    role = 'L'
                    if not inside_round(mx, y, x0 + t + L, x1 - t - L, t + L, h - t - L, r2):
                        vx, vy = round_normal(mx, y, x0 + t, x1 - t, t, h - t, r1)
                        if vx:
                            vx = -vx if right else vx
                            if vx + vy >= 0:
                                role = 'M'
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
        # No shadow: that is Hibr's Shadow setting (ADR 0210, revised).
        c.fill(0, L, 0, h, ink)
    elif name == 'frame-bottom-left':
        c.fill(w - L, w, 0, L, ink)
    elif name in ('frame-bottom-first', 'frame-bottom'):
        c.fill(0, w, 0, L, ink)
    elif name == 'frame-bottom-right':
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
        dialog(distance)
    elif name.startswith('button-') or name.startswith('default-button-'):
        button(name.endswith('-right'), name.startswith('default-'))
    return c.rows()


def draw_frame(index, w, h):
    """One outer-edge frame glyph's role map (ADR 0214), index 0-12 of FRAMES."""
    name = FRAMES[index][0]
    L, inset = T['LINE'], T['STRIPE_INSET']
    c = Cell(w, h)
    if name in ('frame-left', 'frame-bottom-left'):
        c.fill(0, L, 0, h, 'K')
    if name in ('frame-right', 'frame-bottom-right'):
        c.fill(w - L, w, 0, h, 'K')
    if name.startswith('frame-bottom'):
        c.fill(0, w, h - L, h, 'K')
    if '-title-end-' in name:
        # The bar row: its own line and rule (Slate: bevel and bar), the
        # stripes running into the cell and stopping INSET px from its outer
        # edge, and the outer line over everything.
        slate = name.startswith('slate')
        right = name.endswith('-right')
        n, top, side, bx, by = band_box(w, h)
        x0, x1 = (0, w - inset) if right else (inset, w)
        if slate:
            c.fill(0, w, 0, h, 'B')
        for k in range(0 if '-plain-' in name else n):
            y = top + 2 * L * k
            c.fill(x0, x1, y, y + L, 'M' if slate else 'K')
            if slate:
                c.fill(x0, x1, y + L, y + 2 * L, 'L')
        c.fill(0, w, 0, L, 'K')
        c.fill(0, w, h - L, h, 'K')
        if slate:
            c.fill(0, w, L, 2 * L, 'L')
            c.fill(0, w, h - 2 * L, h - L, 'M')
        if right:
            c.fill(w - L, w, 0, h, 'K')
        else:
            c.fill(0, L, 0, h, 'K')
    return c.rows()


def glyphs():
    """Every (codepoint, style, index) in codepoint order. Window glyphs
    have the two sets' styles and index 0-34; the outer-edge frame glyphs
    (ADR 0214) the style 'frame' or their title-bar style, and 100 + n."""
    for style, first in STYLES:
        for index in range(len(NAMES)):
            yield first + index, style, index
    for index, (_, style, _) in enumerate(FRAMES):
        yield FRAME_FIRST + index, style, 100 + index


def rows_of(style, index, w, h):
    return draw_frame(index - 100, w, h) if index >= 100 else draw(style, index, w, h)


def colour(style, role, fg=(0, 0, 0), bg=(214, 214, 214), line=None):
    """A role's colour in a cell with these colours (Chalk: the foreground)."""
    return fg if style == 'chalk' else pens(fg, bg, line)[role]


def reference(w, h):
    """Every glyph as Blit's raster holds it. Chalk: coverage, one byte a
    pixel, 255 or 0. Slate: its pen planes as one role map, a character a
    pixel -- K, L or M, or '.' where nothing is drawn (the background
    role included, which shows the cell's background)."""
    lines = []
    for cp, style, index in glyphs():
        rows = rows_of(style, index, w, h)
        if style in ('chalk', 'frame'):
            out = bytes(0 if role == '.' else 255 for row in rows for role in row)
            lines.append(f'{cp:X} coverage {out.hex()}')
        else:
            roles = ''.join('.' if role in '.B' else role for row in rows for role in row)
            lines.append(f'{cp:X} roles {roles}')
    return '\n'.join(lines) + '\n'


def structure(style, name):
    """What a glyph is and the pens an application sets for it, as the
    catalogue's structured fields (vocabulary in assets/fonts/icons/README.md):
    its part, the cells it spans, what the cell's background, foreground and
    SGR 58 are."""
    slate = style != 'chalk'
    if name.startswith('stripe') or name in ('title', 'close', 'shade', 'zoom'):
        part, background, line = 'title-bar', 'bar', 'highlight' if slate else 'none'
    elif name == 'note-close':
        part, background, line = 'note-close', 'panel', 'box-face' if slate else 'none'
    elif name.startswith('frame-'):
        part, background, line = 'frame', 'wallpaper', 'none'
    elif name.startswith('dialog-'):
        # Slate paints the dialog's face itself, in the line pen, so its cells
        # keep the wallpaper; Chalk, one colour, needs the face behind it.
        part, background, line = 'dialog-border', 'wallpaper' if slate else 'dialog-face', \
            'dialog-face' if slate else 'none'
    else:
        part, background, line = 'button-cap', 'dialog-face' if slate else 'button-face', \
            'button-face' if slate else 'none'
    return {'part': part, 'cells': {'span': 1, 'position': 0}, 'background': background,
            'foreground': 'ink', 'line': line}


# The bar corners (U+100007, U+100008) are cell geometry too (blit#140); the
# raster draws them, and their entries are kept as they were, with these.
CORNER = {'part': 'bar-corner', 'cells': {'span': 1, 'position': 0}, 'background': 'wallpaper',
          'foreground': 'bar', 'line': 'none'}


def frame_entries():
    """The outer-edge frame's entries (ADR 0214). The frame cells are the
    one frame set, in the foreground alone, on the window's face (a panel);
    the title ends are the bar's, in Chalk's or Slate's pens."""
    entries = []
    for index, (name, style, shape) in enumerate(FRAMES):
        cp = FRAME_FIRST + index
        if style == 'frame':
            entries.append({'id': name, 'codepoint': f'{cp:06X}', 'colour': False, 'pens': 'foreground',
                            'set': 'frame', 'shape': f'Frame set: {shape}', 'part': 'frame',
                            'cells': {'span': 1, 'position': 0}, 'background': 'panel',
                            'foreground': 'ink', 'line': 'none'})
        else:
            set_name = {'chalk': 'Chalk', 'slate-light': 'Slate'}[style]
            entries.append({'id': name, 'codepoint': f'{cp:06X}', 'colour': False,
                            'pens': 'foreground' if style == 'chalk' else 'cell',
                            'set': style, 'shape': f'{set_name} set: {shape}',
                            **structure(style, 'title')})
    return entries


def owned(entry):
    """Is this catalogue entry one of this script's: a window glyph
    (U+100231-100276) or an outer-edge frame glyph (U+1002AE-1002B6)?"""
    cp = int(entry['codepoint'], 16)
    return STYLES[0][1] <= cp < RETIRED[0] or FRAME_FIRST <= cp < FRAME_FIRST + len(FRAMES)


def catalogue_entries():
    entries = []
    for cp, style, index in glyphs():
        if index >= 100:
            continue
        name, shape = NAMES[index]
        set_name = {'chalk': 'Chalk', 'slate-light': 'Slate'}[style]
        # Neither set carries its own colours since ADR 0211: Chalk draws in
        # the foreground, Slate in the cell's pens.
        entry = {'id': f'{style}-{name}', 'codepoint': f'{cp:06X}',
                 'colour': False, 'pens': 'foreground' if style == 'chalk' else 'cell',
                 'set': style, 'shape': f'{set_name} set: {shape}', **structure(style, name)}
        if entry['id'] in DEPRECATED:
            entry['deprecated'] = True
            entry['replaced_by'] = DEPRECATED[entry['id']]
        entries.append(entry)
    return entries + frame_entries()


def write_catalogue():
    text = CATALOGUE.read_text(encoding='utf-8')
    data = json.loads(text)
    # The entries of the other families (the corners, the controls) are kept;
    # the corners get their structured fields. Everything is in codepoint order.
    keep = [e for e in data['cell_geometry'] if not owned(e)]
    keep = [{**e, **CORNER} if e['id'].startswith('bar-corner-') else e for e in keep]
    rows = sorted(keep + catalogue_entries(), key=lambda e: int(e['codepoint'], 16))
    # Slate dark's codepoints, retired by ADR 0211 and never reused.
    retired = {'from': f'{RETIRED[0]:06X}', 'to': f'{RETIRED[1]:06X}',
               'was': 'the Slate dark window-chrome glyph set',
               'note': 'retired by ADR 0211: Slate takes its colours from the cell, so one set draws light and dark; never reused'}
    if not any(r.get('from') == retired['from'] for r in data['retired']):
        text = re.sub(r'("retired": \[\n(?:.*?\n)*?)(  \])',
                      lambda m: m.group(1).rstrip('\n') + ',\n    ' +
                      json.dumps(retired, ensure_ascii=False, separators=(', ', ':')) + '\n' + m.group(2),
                      text, count=1)
    body = ',\n'.join('    ' + json.dumps(e, ensure_ascii=False, separators=(', ', ':'))
                      for e in rows)
    new = re.sub(r'"cell_geometry": \[\n.*?\n  \]', lambda _: '"cell_geometry": [\n' + body + '\n  ]',
                 text, flags=re.S)
    json.loads(new)
    CATALOGUE.write_text(new, encoding='utf-8')


def check_catalogue():
    """Is every one of this script's entries in the catalogue exactly what
    the tables give, structured fields included, and the corners' too?"""
    data = json.loads(CATALOGUE.read_text(encoding='utf-8'))
    have = [e for e in data['cell_geometry'] if owned(e)]
    want = sorted(catalogue_entries(), key=lambda e: int(e['codepoint'], 16))
    corners = [e for e in data['cell_geometry'] if e['id'].startswith('bar-corner-')]
    ok = have == want and len(corners) == 2 and all(all(e.get(k) == v for k, v in CORNER.items()) for e in corners)
    print('catalogue matches the window tables' if ok else 'the catalogue differs from the window tables: run --catalogue')
    return 0 if ok else 1


def sheet(path, scale):
    from PIL import Image, ImageDraw
    w, h = 11, 22
    gap = 4
    cols = len(NAMES)
    # Chalk in black on white; Slate in the Pewter scheme (bg 214, fg 0,
    # SGR 58 white) and in a dark one (bg 60,60,67, fg 225, SGR 58 112).
    shown = [('chalk', (0, 0, 0), (255, 255, 255), None),
             ('slate-light', (0, 0, 0), (214, 214, 214), (255, 255, 255)),
             ('slate-light', (225, 225, 225), (60, 60, 67), (112, 112, 112))]
    image = Image.new('RGB', (cols * (w * scale + gap) + gap, 3 * (h * scale + gap) + gap), (90, 110, 150))
    d = ImageDraw.Draw(image)
    for row, (style, fg, bg, line) in enumerate(shown):
        for index in range(cols):
            rows = draw(style, index, w, h)
            ox, oy = gap + index * (w * scale + gap), gap + row * (h * scale + gap)
            for y, line_roles in enumerate(rows):
                for x, role in enumerate(line_roles):
                    fill = (150, 170, 205) if role == '.' else colour(style, role, fg, bg, line)
                    d.rectangle([ox + x * scale, oy + y * scale, ox + x * scale + scale - 1,
                                 oy + y * scale + scale - 1], fill=fill)
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
