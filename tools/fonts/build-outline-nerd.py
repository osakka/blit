#!/usr/bin/env python3
"""Build Blit's renamed outline profiles; pinned fontTools 4.60.1, no network.

Keep the source Unicode repertoire and complete Nerd Fonts v3.4.0 catalogue.
Fit complete icon outlines, not bitmap enlargement or cropped overhangs.
"""
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path
import fontTools
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path
from fontTools.colorLib.builder import buildCOLR, buildCPAL
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.boundsPen import BoundsPen

parser = argparse.ArgumentParser(description=__doc__)
# Each profile: its manifest section and the name its family takes.
PROFILES = {'modern': ('outline', 'Modern'), 'retro': ('retro', 'Retro'),
            'jetbrainless': ('jetbrainless', 'JetBrainless')}
parser.add_argument('--profile', choices=sorted(PROFILES), required=True)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--symbols', type=Path, required=True,
                    help="directory holding the manifest's reviewed symbol sources (ADR 0191)")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'assets/fonts/modern-fonts.source.json').read_text())
if fontTools.__version__ != manifest['build']['fonttools_version']:
    raise SystemExit('use the fontTools version pinned in modern-fonts.source.json')
source = manifest[PROFILES[args.profile][0]]
if hashlib.sha256(args.source.read_bytes()).hexdigest() != source['upstream_sha256']:
    raise SystemExit(f'unreviewed {args.profile} source: SHA256 mismatch')
font = TTFont(args.source, recalcTimestamp=False)
cmap = font.getBestCmap()
catalogue = root / 'assets/fonts' / manifest['catalogue']['file']
if hashlib.sha256(catalogue.read_bytes()).hexdigest() != manifest['catalogue']['sha256']:
    raise SystemExit('unreviewed Nerd catalogue: SHA256 mismatch')
icons = {int(v['code'], 16) for v in json.loads(catalogue.read_text()).values()
         if isinstance(v, dict) and 'code' in v}
if len(icons) != manifest['catalogue']['unique_codepoints'] or not icons <= cmap.keys():
    raise SystemExit('source does not cover the complete reviewed Nerd catalogue')
# Upstream Mono icons can retain negative sidebearings/slight overhang.
# Fit complete outlines, never crop: a shared optical cap-height/side inset
# keeps icons readable within the same terminal advance as ordinary text.
# Geometric Shapes are standalone symbols too (Hibr's window buttons, file
# and scroll marks): Hack draws them edge to edge, which reads as clipped.
# The corner triangles are cell-corner geometry (Hibr's resize grip) and
# keep touching their edges.
corners = {0x25E2, 0x25E3, 0x25E4, 0x25E5, 0x25F8, 0x25F9, 0x25FA, 0x25FF}
shapes = {cp for cp in range(0x25A0, 0x2600) if cp in cmap and cp not in corners}
glyph_set = font.getGlyphSet()
font['glyf'][cmap[ord('H')]].recalcBounds(font['glyf'])
cap = font['glyf'][cmap[ord('H')]].yMax
for name in sorted({cmap[cp] for cp in icons | shapes}):
    glyph = font['glyf'][name]
    glyph.recalcBounds(font['glyf'])
    if not hasattr(glyph, 'xMin') or glyph.xMax == glyph.xMin:
        continue
    advance = font['hmtx'][name][0]
    scale = min(1.0, advance * .9 / (glyph.xMax - glyph.xMin),
                cap / max(1, glyph.yMax - glyph.yMin))
    tx = advance / 2 - scale * (glyph.xMin + glyph.xMax) / 2
    ty = cap / 2 - scale * (glyph.yMin + glyph.yMax) / 2
    pen = TTGlyphPen(glyph_set)
    glyph_set[name].draw(TransformPen(pen, (scale, 0, 0, scale, tx, ty)))
    fitted = pen.glyph()
    fitted.recalcBounds(font['glyf'])
    font['glyf'][name] = fitted
    font['hmtx'][name] = (advance, fitted.xMin)
# Original artwork is generated from one editable SVG catalogue for both
# profiles. Plane16 mappings never replace Nerd or Unicode emoji assignments.
custom = manifest['custom_icons']
custom_root = root / 'assets/fonts'
for path, expected in custom['sources'].items():
    if hashlib.sha256((custom_root / path).read_bytes()).hexdigest() != expected:
        raise SystemExit(f'unreviewed custom artwork: {path}')
custom_catalogue = json.loads((custom_root / custom['catalogue']).read_text())
layers, palette = {}, []
order = list(font.getGlyphOrder())
advance = font['hmtx'][cmap[ord('H')]][0]
vertical = font['vmtx'].metrics if 'vmtx' in font else None
vertical_metric = vertical[cmap[ord('H')]] if vertical is not None else None

def store_glyph(name, glyph):
    glyph.recalcBounds(font['glyf'])
    font['glyf'][name] = glyph
    font['hmtx'][name] = (advance, glyph.xMin)
    if vertical is not None:
        vertical[name] = vertical_metric
    order.append(name)

for icon in custom_catalogue['icons'] + custom_catalogue.get('text_glyphs', []):
    cp = int(icon['codepoint'], 16)
    intrinsic_colour = icon.get('colour', True)
    if not intrinsic_colour and cp in cmap:
        continue
    if cp in cmap or (intrinsic_colour and not 0x100000 <= cp <= 0x10fffd):
        raise SystemExit('custom icon mapping collides or is outside private-use plane16')
    svg = ET.parse(custom_root / 'icons' / icon['source']).getroot()
    vx, vy, vw, vh = map(float, svg.attrib['viewBox'].split())
    scale = min(advance * .9 / vw, cap / vh)
    transform = (scale, 0, 0, -scale,
                 (advance - vw * scale) / 2 - vx * scale,
                 (cap + vh * scale) / 2 + vy * scale)
    name = 'blit.' + icon['id']
    silhouette = TTGlyphPen(None)
    colour_layers = []
    for index, path in enumerate(svg.findall('{http://www.w3.org/2000/svg}path')):
        active = int(path.attrib.get('data-level', '0')) <= icon.get('signal_level', 4)
        colour = path.attrib['fill' if active else 'data-inactive-fill']
        rgba = tuple(int(colour[i:i+2], 16) / 255 for i in (1, 3, 5)) + (1.0,)
        if rgba not in palette:
            palette.append(rgba)
        pen = TTGlyphPen(None)
        parse_path(path.attrib['d'], TransformPen(pen, transform))
        layer_name = f'{name}.layer{index}'
        glyph = pen.glyph()
        store_glyph(layer_name, glyph)
        colour_layers.append((layer_name, palette.index(rgba)))
        if active:
            parse_path(path.attrib['d'], TransformPen(silhouette, transform))
    glyph = silhouette.glyph()
    store_glyph(name, glyph)
    if intrinsic_colour:
        layers[name] = colour_layers
    for table in font['cmap'].tables:
        if table.isUnicode() and (cp <= 0xffff or table.format in (12, 13)):
            table.cmap[cp] = name
    # Keep the standard writing-pencil character monochrome/themed; the
    # private-use Hibr glyph is the explicit intrinsic-colour counterpart.
    if 'text_alias' in icon:
        alias = int(icon['text_alias'], 16)
        alias_name = name + '.text'
        store_glyph(alias_name, glyph)
        for table in font['cmap'].tables:
            if table.isUnicode():
                table.cmap[alias] = alias_name
# ADR 0191: the symbol blocks the base font lacks -- Claude Code's and
# Codex's markers and spinners, btop's arrows -- from reviewed Noto sources,
# the first that has each codepoint. Never a codepoint the base font has:
# its text keeps its look. At natural size (source units scaled to ours),
# shrunk only to fit the advance and the line, centred in the advance.
# Braille is cell geometry, drawn by Blit's raster, not taken here.
symbols = manifest['symbols']
symbol_sources = []
for entry in symbols['sources']:
    path = args.symbols / entry['file']
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry['upstream_sha256']:
        raise SystemExit(f'unreviewed symbol source: {entry["file"]}')
    licence = custom_root / entry['license_file']
    if hashlib.sha256(licence.read_bytes()).hexdigest() != entry['license_sha256']:
        raise SystemExit(f'unreviewed symbol licence: {entry["license_file"]}')
    source_font = TTFont(path, recalcTimestamp=False)
    symbol_sources.append((source_font.getBestCmap(), source_font.getGlyphSet(),
                           source_font['head'].unitsPerEm))
units = font['head'].unitsPerEm
ascent, descent = font['hhea'].ascent, font['hhea'].descent
added = 0
for low, high in symbols['ranges']:
    for cp in range(int(low, 16), int(high, 16) + 1):
        if cp in cmap or 0x2800 <= cp <= 0x28ff:
            continue
        for source_cmap, source_glyphs, source_units in symbol_sources:
            if cp in source_cmap:
                break
        else:
            continue
        recording = DecomposingRecordingPen(source_glyphs)
        source_glyphs[source_cmap[cp]].draw(recording)
        bounds = BoundsPen(None)
        recording.replay(bounds)
        if bounds.bounds is None:
            continue
        base = units / source_units
        x_min, y_min, x_max, y_max = (v * base for v in bounds.bounds)
        fit = min(1.0, advance * .9 / max(1.0, x_max - x_min),
                  (ascent - descent) * .95 / max(1.0, y_max - y_min))
        centre = (y_min + y_max) / 2
        ty = centre * (1 - fit)
        top, bottom = fit * y_max + ty, fit * y_min + ty
        if top > ascent:
            ty -= top - ascent
        elif bottom < descent:
            ty += descent - bottom
        tx = advance / 2 - fit * (x_min + x_max) / 2
        pen = TTGlyphPen(None)
        recording.replay(TransformPen(pen, (base * fit, 0, 0, base * fit, tx, ty)))
        name = 'blit.sym.%04X' % cp
        store_glyph(name, pen.glyph())
        for table in font['cmap'].tables:
            if table.isUnicode() and (cp <= 0xffff or table.format in (12, 13)):
                table.cmap[cp] = name
        added += 1
font.setGlyphOrder(order)
font['COLR'] = buildCOLR(layers, version=0, glyphMap=font.getReverseGlyphMap())
font['CPAL'] = buildCPAL([palette])
# A renamed derivative, with upstream notices retained in name/license tables.
family = 'Blit ' + PROFILES[args.profile][1] + ' Nerd'
postscript = family.replace(' ', '')
names = {1: family, 2: 'Regular', 3: postscript + '-3.0',
         4: family + ' Regular', 6: postscript + '-Regular',
         16: family, 17: 'Regular'}
for entry in font['name'].names:
    if entry.nameID in names:
        entry.string = names[entry.nameID].encode(entry.getEncoding())
font.save(args.output, reorderTables=True)
print(f'OUTLINE_NERD_BUILD_PASS profile={args.profile} icons={len(icons)} '
      f'symbols={added} menu=U+270E sha256={hashlib.sha256(args.output.read_bytes()).hexdigest()}')
