#!/usr/bin/env python3
"""Build Modern's licensed Hibr Nerd repertoire; fontTools 4.60.1, no network.

Input: upstream Nerd Fonts v3.4.0 HackNerdFontMono-Regular.ttf.
Keep Hack Unicode plus Font Awesome Nerd symbols, not unrelated icon packs.
The existing U+270E menu pencil receives the same outline as nf-fa-pencil.
"""
import argparse
import hashlib
from pathlib import Path
import fontTools
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
assert fontTools.__version__ == '4.60.1', 'use pinned fontTools4.60.1'
assert hashlib.sha256(args.source.read_bytes()).hexdigest() == \
    '03e60d3c1a9f8bef4e1f78836f80aacb9ec005260a6b094f5bfc10043bb115ab'
root = Path(__file__).resolve().parents[2]
font = TTFont(args.source, recalcTimestamp=False)
original = TTFont(root / 'assets/fonts/Hack-Regular.ttf')
keep = set(original.getBestCmap())
cmap = font.getBestCmap()
icons = {cp for cp, name in cmap.items() if name.startswith('fa-')}
keep |= icons | {0x270e}
# Upstream Mono icons can retain negative sidebearings/slight overhang.
# Fit complete outlines, never crop: a shared optical cap-height/side inset
# keeps icons readable within the same terminal advance as ordinary text.
glyph_set = font.getGlyphSet()
cap = original['glyf'][original.getBestCmap()[ord('H')]].yMax
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
names = {1: 'Blit Modern Nerd', 2: 'Regular', 3: 'BlitModernNerd-1.0',
         4: 'Blit Modern Nerd Regular', 6: 'BlitModernNerd-Regular',
         16: 'Blit Modern Nerd', 17: 'Regular'}
for entry in font['name'].names:
    if entry.nameID in names:
        entry.string = names[entry.nameID].encode(entry.getEncoding())
options = subset.Options()
options.name_IDs = ['*']
options.name_legacy = True
options.name_languages = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(unicodes=keep)
subsetter.subset(font)
font.save(args.output, reorderTables=True)
print(f'MODERN_NERD_BUILD_PASS icons={len(icons)} menu=U+270E outline=nf-fa-pencil')
