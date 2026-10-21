#!/usr/bin/env python3
"""Offline Arabic companion experiment; source identity and shaping are pinned.

Hibr already emits presentation forms. Alias those missing in a modern source
face to its actual shaped glyph, retaining GSUB/GPOS for mark positioning.
This does not prove terminal layout, joining, mark placement or aesthetics.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import unicodedata
import fontTools
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', choices=['retro', 'modern'], required=True)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
manifest = json.loads((root / 'assets/fonts/arabic-fonts.source.json').read_text())
entry, build = manifest[args.profile], manifest['build']
if hashlib.sha256(args.source.read_bytes()).hexdigest() != entry['upstream_sha256']:
    raise SystemExit('unreviewed Arabic source')
if fontTools.__version__ != build['fonttools_version'] or unicodedata.unidata_version != build['unicode_decomposition_version']:
    raise SystemExit('use the pinned fontTools and Unicode decomposition versions')

class Glyph(c.Structure):
    _fields_ = [('codepoint', c.c_uint32), ('mask', c.c_uint32),
                ('cluster', c.c_uint32), ('var1', c.c_uint32), ('var2', c.c_uint32)]

hb = c.CDLL('/lib/x86_64-linux-gnu/libharfbuzz.so.0')
def bind(name, result, *arguments):
    f = getattr(hb, name)
    f.restype, f.argtypes = result, list(arguments)
    return f

version = bind('hb_version_string', c.c_char_p)
if version().decode() != build['harfbuzz_version']:
    raise SystemExit('use the pinned HarfBuzz version')
blob_create = bind('hb_blob_create_from_file_or_fail', c.c_void_p, c.c_char_p)
blob_destroy = bind('hb_blob_destroy', None, c.c_void_p)
face_create = bind('hb_face_create', c.c_void_p, c.c_void_p, c.c_uint)
face_destroy = bind('hb_face_destroy', None, c.c_void_p)
font_create = bind('hb_font_create', c.c_void_p, c.c_void_p)
font_destroy = bind('hb_font_destroy', None, c.c_void_p)
font_funcs = bind('hb_ot_font_set_funcs', None, c.c_void_p)
buffer_create = bind('hb_buffer_create', c.c_void_p)
buffer_destroy = bind('hb_buffer_destroy', None, c.c_void_p)
buffer_add = bind('hb_buffer_add_utf32', None, c.c_void_p, c.POINTER(c.c_uint32), c.c_int, c.c_uint, c.c_int)
buffer_guess = bind('hb_buffer_guess_segment_properties', None, c.c_void_p)
buffer_flags = bind('hb_buffer_set_flags', None, c.c_void_p, c.c_uint)
shape = bind('hb_shape', None, c.c_void_p, c.c_void_p, c.c_void_p, c.c_uint)
infos = bind('hb_buffer_get_glyph_infos', c.POINTER(Glyph), c.c_void_p, c.POINTER(c.c_uint))
font = TTFont(args.source, recalcTimestamp=False)
cmap, order = font.getBestCmap(), font.getGlyphOrder()
blob = blob_create(str(args.source).encode())
if not blob:
    raise SystemExit('cannot open shaping source')
face = face_create(blob, 0)
shaper = font_create(face)
font_funcs(shaper)
added, skipped, non_mono = [], [], []
mono_advance = font['hmtx'][cmap[0x0628]][0]
try:
    for cp in list(range(0xfb50, 0xfe00)) + list(range(0xfe70, 0xfefd)):
        if cp in cmap:
            continue
        decomposition = unicodedata.decomposition(chr(cp)).split()
        if not decomposition or decomposition[0] not in ('<isolated>', '<initial>', '<medial>', '<final>'):
            continue
        tag, values = decomposition[0], [int(x, 16) for x in decomposition[1:]]
        # ZWJ asks the real source shaper for the specified joining context;
        # remove its invisible placeholder, not a visible Arabic letter.
        if tag in ('<medial>', '<final>'):
            values.insert(0, 0x200d)
        if tag in ('<initial>', '<medial>'):
            values.append(0x200d)
        data = (c.c_uint32 * len(values))(*values)
        buffer = buffer_create()
        try:
            buffer_add(buffer, data, len(values), 0, len(values))
            buffer_flags(buffer, 8)
            buffer_guess(buffer)
            shape(shaper, buffer, None, 0)
            count = c.c_uint()
            glyphs = infos(buffer, c.byref(count))
            ids = [glyphs[i].codepoint for i in range(count.value)]
        finally:
            buffer_destroy(buffer)
        if len(ids) != 1 or not ids[0]:
            skipped.append(f'{cp:04X}')
            continue
        name = order[ids[0]]
        for table in font['cmap'].tables:
            if table.isUnicode():
                table.cmap[cp] = name
        added.append(f'{cp:04X}')
        if font['hmtx'][name][0] != mono_advance:
            non_mono.append({'cp':f'{cp:04X}', 'advance':font['hmtx'][name][0]})
finally:
    font_destroy(shaper)
    face_destroy(face)
    blob_destroy(blob)

family = entry['family']
postscript = family.replace(' ', '')
names = {1:family, 2:'Regular', 3:postscript+'-1.0', 4:family+' Regular',
         6:postscript+'-Regular', 16:family, 17:'Regular'}
for record in font['name'].names:
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
font.save(args.output, reorderTables=True)
print(json.dumps({'profile':args.profile, 'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
    'aliases_added':len(added), 'skipped':skipped, 'non_mono_aliases':non_mono,
    'scope':'font_alias_generation_not_terminal_acceptance'}, ensure_ascii=False))
