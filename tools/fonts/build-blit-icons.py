#!/usr/bin/env python3
"""Build Blit's own icon font from the SVG catalogue; pinned fontTools 4.60.1.

ADR 0195: Blit's original icons are one small font in the glyph stack,
drawn beside any text font, instead of being baked into each outline
profile. The artwork, the COLR v0 layers and the U+270E pen alias are
exactly what the outline generator made; only the container changes. The
renderer fits each glyph to the cell at draw time (the icon rule: within
90% of the advance and the text font's cap height, centred), so the units
here are only a canvas: advance 600, cap height 700, em 1000.

An icon that spans columns (ADR 0197) is one codepoint per column, from
one drawing: each part carries the whole outline, drawn across the span
and shifted left by its column. Each part's advance is the span's
(600 per column), which is how the renderer knows it; its ink's centre
must lie within its own column's share, which is how it knows the column.

A monochrome pen says state by shape (ADR 0220), so the shape is decided here,
from the SVG's own structure, and the renderer only chooses the pen. An icon
whose paths are not all drawn (a level below the signal level) or that marks a
path with `data-mono` gets one more glyph, `<name>.mono`, in no cmap:
- the union of the active paths, so a level is carried by what is drawn;
- `data-mono="disc"` on a path draws only its first subpath, so a ring is a
  solid disc;
- `data-mono="knockout"` on a path cuts its shapes out of that: they are
  drawn against the nonzero winding rule, so inside the ink they are holes
  and, outside it, solid (a charging bolt is a hole in the fill and solid on
  the empty part of the battery). Knockout shapes must be simple, without holes.
An inactive path's `data-inactive-fill` may carry an alpha (#rrggbbaa): at
00 it is drawn nowhere in colour, yet its layer still counts in the icon's
box, so a state that hides a part (wiretap-1-off's drop) is fitted exactly
as the state that shows it.
An icon with neither has no `.mono` glyph: its colour glyph's coverage is its
monochrome shape. Nothing is drawn by hand.
"""
import argparse
import copy
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

import fontTools
from fontTools.colorLib.builder import buildCOLR, buildCPAL
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.areaPen import AreaPen
from fontTools.pens.reverseContourPen import ReverseContourPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'assets/fonts/fonts.source.json').read_text())
if fontTools.__version__ != manifest['build']['fonttools_version']:
    raise SystemExit('use the fontTools version pinned in fonts.source.json')
custom = manifest['custom_icons']
fonts = root / 'assets/fonts'
for path, expected in custom['sources'].items():
    if hashlib.sha256((fonts / path).read_bytes()).hexdigest() != expected:
        raise SystemExit(f'unreviewed custom artwork: {path}')
catalogue = json.loads((fonts / custom['catalogue']).read_text())

UPEM, ADVANCE, CAP, ASCENT, DESCENT = 1000, 600, 700, 800, -200
order = ['.notdef']
glyphs = {'.notdef': TTGlyphPen(None).glyph()}
cmap = {}
layers, palette = {}, []
advances = {}
SVG = '{http://www.w3.org/2000/svg}path'


def subpaths(d):
    """The subpaths of an absolute path: the text from one M to the next."""
    parts = re.split(r'(?=M)', d.strip())
    if parts[0] != '' or 'm' in d:
        raise SystemExit('a mono path must use absolute M subpaths')
    return parts[1:]


def mono_glyph(icon, paths, transform):
    """The monochrome shape of an icon (see the module text), or None when
    its colour glyph's coverage already is it."""
    level = icon.get('signal_level', 4)
    marks = [p.attrib.get('data-mono') for p in paths]
    if any(m not in (None, 'disc', 'knockout') for m in marks):
        raise SystemExit(f'{icon["id"]}: data-mono is disc or knockout')
    if all(int(p.attrib.get('data-level', '0')) <= level for p in paths) and not any(marks):
        return None
    pen, sign, cuts = TTGlyphPen(None), 0, []
    for path, mark in zip(paths, marks):
        if int(path.attrib.get('data-level', '0')) > level:
            continue
        if mark == 'knockout':
            cuts += subpaths(path.attrib['d'])
            continue
        d = path.attrib['d']
        if mark == 'disc':
            d = subpaths(d)[0]
        if not sign:
            area = AreaPen(None)
            parse_path(subpaths(d)[0], TransformPen(area, transform))
            sign = 1 if area.value > 0 else -1
        parse_path(d, TransformPen(pen, transform))
    for cut in cuts:
        area = AreaPen(None)
        parse_path(cut, TransformPen(area, transform))
        # Against the ink's winding, so it is a hole where there is ink.
        target = pen if (area.value > 0) != (sign > 0) else ReverseContourPen(pen)
        parse_path(cut, TransformPen(target, transform))
    return pen.glyph()


def store(name, glyph):
    glyphs[name] = glyph
    order.append(name)


for icon in catalogue['icons'] + catalogue.get('text_glyphs', []):
    cp = int(icon['codepoint'], 16)
    colour = icon.get('colour', True)
    if colour and not 0x100000 <= cp <= 0x10fffd:
        raise SystemExit('a colour icon must sit in private-use plane 16')
    svg = ET.parse(fonts / 'icons' / icon['source']).getroot()
    vx, vy, vw, vh = map(float, svg.attrib['viewBox'].split())
    columns, column = icon.get('columns', 1), icon.get('column', 0)
    scale = min(ADVANCE * columns * .9 / vw, CAP / vh)
    transform = (scale, 0, 0, -scale,
                 (ADVANCE * columns - vw * scale) / 2 - vx * scale - column * ADVANCE,
                 (CAP + vh * scale) / 2 + vy * scale)
    name = 'blit.' + icon['id']
    silhouette = TTGlyphPen(None)
    colour_layers = []
    paths = svg.findall(SVG)
    for index, path in enumerate(paths):
        active = int(path.attrib.get('data-level', '0')) <= icon.get('signal_level', 4)
        fill = path.attrib['fill' if active else 'data-inactive-fill']
        # #rrggbb, or #rrggbbaa: an inactive part may be clear (alpha 00),
        # drawn nowhere but still part of the icon's box.
        if not re.fullmatch(r'#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?', fill):
            raise SystemExit(f'{icon["id"]}: a fill is #rrggbb or #rrggbbaa')
        rgba = tuple(int(fill[i:i + 2], 16) / 255 for i in (1, 3, 5)) + \
            ((int(fill[7:9], 16) / 255,) if len(fill) == 9 else (1.0,))
        if rgba not in palette:
            palette.append(rgba)
        pen = TTGlyphPen(None)
        parse_path(path.attrib['d'], TransformPen(pen, transform))
        store(f'{name}.layer{index}', pen.glyph())
        colour_layers.append((f'{name}.layer{index}', palette.index(rgba)))
        if active:
            parse_path(path.attrib['d'], TransformPen(silhouette, transform))
    glyph = silhouette.glyph()
    store(name, glyph)
    # A part spanning columns says so in its advance; the renderer finds its
    # column from its ink's centre, which must lie within its column's share.
    advances[name] = ADVANCE * columns
    if columns > 1 and glyph.numberOfContours:
        glyph.recalcBounds(glyphs)
        centre = (glyph.xMin + glyph.xMax) / 2
        if abs(centre - (ADVANCE * columns / 2 - column * ADVANCE)) > ADVANCE * .4:
            raise SystemExit(f'{icon["id"]}: its ink is off centre for column {column}')
    if colour:
        layers[name] = colour_layers
        mono = mono_glyph(icon, paths, transform)
        if mono is not None:
            store(name + '.mono', mono)
            advances[name + '.mono'] = ADVANCE * columns
    cmap[cp] = name
    # The standard writing pencil stays monochrome and themed; the
    # private-use glyph is its intrinsic-colour counterpart.
    if 'text_alias' in icon:
        store(name + '.text', copy.deepcopy(glyph))
        cmap[int(icon['text_alias'], 16)] = name + '.text'

builder = FontBuilder(UPEM, isTTF=True)
builder.setupGlyphOrder(order)
builder.setupCharacterMap(cmap)
builder.setupGlyf(glyphs)
builder.setupHorizontalMetrics({n: (advances.get(n, ADVANCE), getattr(glyphs[n], 'xMin', 0))
                                for n in order})
builder.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
builder.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT, usWinAscent=ASCENT,
                 usWinDescent=-DESCENT, sCapHeight=CAP)
builder.setupPost(isFixedPitch=1)
builder.setupNameTable({'familyName': 'Blit Icons', 'styleName': 'Regular',
                        'psName': 'BlitIcons-Regular', 'version': 'Version 1.000',
                        'licenseDescription': 'Blit original artwork; see icons/LICENSE.txt'})
builder.setupHead(created=0, modified=0)
font = builder.font
font['head'].created = font['head'].modified = 0
font['OS/2'].panose.bProportion = 9  # monospaced
font['COLR'] = buildCOLR(layers, version=0, glyphMap=font.getReverseGlyphMap())
font['CPAL'] = buildCPAL([palette])
font.save(args.output, reorderTables=True)
print(f'BLIT_ICONS_BUILD_PASS glyphs={len(cmap)} colour={len(layers)} '
      f'sha256={hashlib.sha256(args.output.read_bytes()).hexdigest()}')
