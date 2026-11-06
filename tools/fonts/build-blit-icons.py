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

An icon the catalogue marks `"mono": "disc-knockout"` is a badge: a disc with
marks on it (ADR 0220). From its one SVG the build derives one more glyph,
`<name>.mono`, that is in no cmap: the first path's first subpath is the disc,
filled solid, and every other path is knocked out of it. Its two COLR layers
are a white disc and black marks, the badge for a dark bar; the renderer
inverts it for a light one. Nothing is drawn by hand.
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
from fontTools.pens.boundsPen import BoundsPen
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
layers, mono_layers, palette = {}, {}, []
advances = {}
MONO = {'disc-knockout'}


def palette_index(rgba):
    if rgba not in palette:
        palette.append(rgba)
    return palette.index(rgba)


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
    for index, path in enumerate(svg.findall('{http://www.w3.org/2000/svg}path')):
        active = int(path.attrib.get('data-level', '0')) <= icon.get('signal_level', 4)
        fill = path.attrib['fill' if active else 'data-inactive-fill']
        rgba = tuple(int(fill[i:i + 2], 16) / 255 for i in (1, 3, 5)) + (1.0,)
        pen = TTGlyphPen(None)
        parse_path(path.attrib['d'], TransformPen(pen, transform))
        store(f'{name}.layer{index}', pen.glyph())
        colour_layers.append((f'{name}.layer{index}', palette_index(rgba)))
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
    if 'mono' in icon:
        if icon['mono'] not in MONO or not colour:
            raise SystemExit(f'{icon["id"]}: mono must be one of {sorted(MONO)}, on a colour icon')
        paths = svg.findall('{http://www.w3.org/2000/svg}path')
        d0 = paths[0].attrib['d'].strip()
        # Absolute commands only: a subpath is the text from one M to the next.
        subpaths = re.split(r'(?=M)', d0)
        if len(paths) < 2 or subpaths[0] != '' or 'm' in d0:
            raise SystemExit(f'{icon["id"]}: a disc-knockout icon is an absolute disc path and its marks')
        disc_d = subpaths[1]
        disc, ink = TTGlyphPen(None), TTGlyphPen(None)
        parse_path(disc_d, TransformPen(disc, transform))
        for path in paths[1:]:
            parse_path(path.attrib['d'], TransformPen(ink, transform))
        # The marks sit inside the disc, so the disc is the whole box: the
        # renderer fits the badge by its box and nothing moves between modes.
        boxes = []
        for d in [disc_d] + [p.attrib['d'] for p in paths]:
            pen = BoundsPen(None)
            parse_path(d, TransformPen(pen, transform))
            boxes.append(pen.bounds)
        if boxes[0] != (min(b[0] for b in boxes), min(b[1] for b in boxes),
                        max(b[2] for b in boxes), max(b[3] for b in boxes)):
            raise SystemExit(f'{icon["id"]}: the disc must be the outermost shape')
        store(f'{name}.mono.disc', disc.glyph())
        store(f'{name}.mono.ink', ink.glyph())
        store(f'{name}.mono', copy.deepcopy(glyphs[f'{name}.mono.disc']))
        advances[f'{name}.mono'] = ADVANCE * columns
        mono_layers[f'{name}.mono'] = [(f'{name}.mono.disc', palette_index((1.0, 1.0, 1.0, 1.0))),
                                       (f'{name}.mono.ink', palette_index((0.0, 0.0, 0.0, 1.0)))]
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
font['COLR'] = buildCOLR({**layers, **mono_layers}, version=0, glyphMap=font.getReverseGlyphMap())
font['CPAL'] = buildCPAL([palette])
font.save(args.output, reorderTables=True)
print(f'BLIT_ICONS_BUILD_PASS glyphs={len(cmap)} colour={len(layers)} '
      f'sha256={hashlib.sha256(args.output.read_bytes()).hexdigest()}')
