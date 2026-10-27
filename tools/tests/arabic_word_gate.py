#!/usr/bin/env python3
"""Independent whole-visual-word versus cell-local OpenType positioning.

Uses HarfBuzz's OT font funcs in design units, not the renderer's FT font,
cache or pixel oracle. No bidi: specimens are already Hibr visual output.
"""
import ctypes as c
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
hb = c.CDLL('/lib/x86_64-linux-gnu/libharfbuzz.so.0')
def bind(name, result, *arguments):
    f = getattr(hb, name)
    f.restype, f.argtypes = result, list(arguments)
    return f

class Info(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in ('codepoint', 'mask', 'cluster', 'var1', 'var2')]
class Position(c.Structure):
    _fields_ = [(n, c.c_int32) for n in ('x_advance', 'y_advance', 'x_offset', 'y_offset', 'var')]
class Feature(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in ('tag', 'value', 'start', 'end')]

blob_open = bind('hb_blob_create_from_file_or_fail', c.c_void_p, c.c_char_p)
blob_close = bind('hb_blob_destroy', None, c.c_void_p)
face_open = bind('hb_face_create', c.c_void_p, c.c_void_p, c.c_uint)
face_close = bind('hb_face_destroy', None, c.c_void_p)
font_open = bind('hb_font_create', c.c_void_p, c.c_void_p)
font_close = bind('hb_font_destroy', None, c.c_void_p)
font_funcs = bind('hb_ot_font_set_funcs', None, c.c_void_p)
buffer_open = bind('hb_buffer_create', c.c_void_p)
buffer_close = bind('hb_buffer_destroy', None, c.c_void_p)
add = bind('hb_buffer_add_utf32', None, c.c_void_p, c.POINTER(c.c_uint32), c.c_int, c.c_uint, c.c_int)
direction = bind('hb_buffer_set_direction', None, c.c_void_p, c.c_int)
script = bind('hb_buffer_set_script', None, c.c_void_p, c.c_uint32)
language = bind('hb_language_from_string', c.c_void_p, c.c_char_p, c.c_int)
set_language = bind('hb_buffer_set_language', None, c.c_void_p, c.c_void_p)
shape = bind('hb_shape', None, c.c_void_p, c.c_void_p, c.POINTER(Feature), c.c_uint)
infos = bind('hb_buffer_get_glyph_infos', c.POINTER(Info), c.c_void_p, c.POINTER(c.c_uint))
positions = bind('hb_buffer_get_glyph_positions', c.POINTER(Position), c.c_void_p, c.POINTER(c.c_uint))
features = (Feature * 7)(*[Feature(int.from_bytes(t.encode(), 'big'), 0, 0, 0xffffffff)
                          for t in ('isol', 'init', 'medi', 'fina', 'rlig', 'liga', 'calt')])

def shaped(font, scalars):
    b = buffer_open()
    try:
        data = (c.c_uint32 * len(scalars))(*scalars)
        add(b, data, len(scalars), 0, len(scalars))
        direction(b, 4)  # LTR: already visual presentation forms
        script(b, int.from_bytes(b'Arab', 'big'))
        set_language(b, language(b'ar', -1))
        shape(font, b, features, len(features))
        count = c.c_uint()
        ids = infos(b, c.byref(count))
        pos = positions(b, None)
        x = y = 0
        result = []
        for i in range(count.value):
            if not ids[i].codepoint:
                raise AssertionError('missing glyph in reference word')
            result.append((ids[i].codepoint, x + pos[i].x_offset, y + pos[i].y_offset))
            x += pos[i].x_advance
            y += pos[i].y_advance
        return result, x, y
    finally:
        buffer_close(b)

samples = json.loads((root / 'assets/fonts/arabic-specimens.json').read_text())['samples']
for profile in ('Modern',):  # ADR 0195: the one Arabic companion
    blob = blob_open(str(root / f'assets/fonts/BlitArabic{profile}.ttf').encode())
    if not blob:
        raise SystemExit('cannot open reference font')
    face = face_open(blob, 0)
    font = font_open(face)
    font_funcs(font)
    try:
        for sample in samples:
            scalars = list(map(ord, sample['visual']))
            cells = []
            for cp in scalars:
                if cells and (0x064b <= cp <= 0x065f or cp == 0x0670):
                    cells[-1].append(cp)
                else:
                    cells.append([cp])
            whole, advance, vertical = shaped(font, scalars)
            separate, x, y = [], 0, 0
            for cell in cells:
                glyphs, dx, dy = shaped(font, cell)
                separate.extend((gid, gx + x, gy + y) for gid, gx, gy in glyphs)
                x += dx
                y += dy
            assert (whole, advance, vertical) == (separate, x, y), (profile, sample['logical'], whole, separate)
        print(f'ARABIC_WORD_PASS profile={profile} samples={len(samples)} whole_word_vs_cells=exact scope=font_units_not_scanout')
    finally:
        font_close(font)
        face_close(face)
        blob_close(blob)
