#!/usr/bin/env python3
"""Build Blit's renamed outline profiles; pinned fontTools 4.60.1, no network.

Keep the source Unicode repertoire and complete Nerd Fonts v3.4.0 catalogue.
Fit complete icon outlines, not bitmap enlargement or cropped overhangs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import fontTools
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', choices=['modern', 'retro'], required=True)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'assets/fonts/modern-fonts.source.json').read_text())
if fontTools.__version__ != manifest['build']['fonttools_version']:
    raise SystemExit('use the fontTools version pinned in modern-fonts.source.json')
source = manifest['outline' if args.profile == 'modern' else 'retro']
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
glyph_set = font.getGlyphSet()
font['glyf'][cmap[ord('H')]].recalcBounds(font['glyf'])
cap = font['glyf'][cmap[ord('H')]].yMax
for name in sorted({cmap[cp] for cp in icons}):
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
for table in font['cmap'].tables:
    if table.isUnicode():
        table.cmap[0x270e] = cmap[0xf040]
# A renamed derivative, with upstream notices retained in name/license tables.
family = 'Blit ' + args.profile.title() + ' Nerd'
postscript = family.replace(' ', '')
names = {1: family, 2: 'Regular', 3: postscript + '-2.0',
         4: family + ' Regular', 6: postscript + '-Regular',
         16: family, 17: 'Regular'}
for entry in font['name'].names:
    if entry.nameID in names:
        entry.string = names[entry.nameID].encode(entry.getEncoding())
font.save(args.output, reorderTables=True)
print(f'OUTLINE_NERD_BUILD_PASS profile={args.profile} icons={len(icons)} '
      f'menu=U+270E sha256={hashlib.sha256(args.output.read_bytes()).hexdigest()}')
