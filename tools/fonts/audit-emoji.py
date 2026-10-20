#!/usr/bin/env python3
"""Offline font-shaping audit, NOT Blit renderer acceptance or a runtime shaper.

Uses the supplied HarfBuzz shared library through its public C ABI. Does not
install dependencies, alter fonts or connect to a browser/display. Reports
all missing/split sequences; --require-complete makes coverage gaps fail.
"""
import argparse
import collections
import ctypes as c
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--font', type=Path, default=root / 'assets/fonts/NotoColorEmoji.ttf')
parser.add_argument('--harfbuzz', type=Path, default=Path('/lib/x86_64-linux-gnu/libharfbuzz.so.0'))
parser.add_argument('--require-complete', action='store_true')
args = parser.parse_args()
data_root = root / 'assets/unicode/emoji-18.0'
manifest = json.loads((data_root / 'source.json').read_text())
data = data_root / manifest['test_data']['file']
if hashlib.sha256(data.read_bytes()).hexdigest() != manifest['test_data']['sha256']:
    raise SystemExit('emoji corpus identity mismatch')

class Glyph(c.Structure):
    _fields_ = [('codepoint', c.c_uint32), ('mask', c.c_uint32),
                ('cluster', c.c_uint32), ('var1', c.c_uint32), ('var2', c.c_uint32)]

hb = c.CDLL(str(args.harfbuzz))
def bind(name, result, *arguments):
    function = getattr(hb, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function

version = bind('hb_version_string', c.c_char_p)
blob_create = bind('hb_blob_create_from_file_or_fail', c.c_void_p, c.c_char_p)
blob_destroy = bind('hb_blob_destroy', None, c.c_void_p)
face_create = bind('hb_face_create', c.c_void_p, c.c_void_p, c.c_uint)
face_destroy = bind('hb_face_destroy', None, c.c_void_p)
face_count = bind('hb_face_get_glyph_count', c.c_uint, c.c_void_p)
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
blob = blob_create(str(args.font).encode())
if not blob:
    raise SystemExit('cannot read font')
face = face_create(blob, 0)
font = None
try:
    if not face_count(face):
        raise SystemExit('invalid or empty font')
    font = font_create(face)
    font_funcs(font)
    counts = collections.defaultdict(collections.Counter)
    failures = []
    for line in data.read_text().splitlines():
        match = re.match(r'^([0-9A-F ]+)\s*;\s*([a-z-]+)\s*#\s*(.*)', line)
        if not match:
            continue
        scalars = [int(v, 16) for v in match[1].split()]
        status = match[2]
        values = (c.c_uint32 * len(scalars))(*scalars)
        buffer = buffer_create()
        try:
            buffer_add(buffer, values, len(values), 0, len(values))
            # HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES: after shaping, do not
            # count invisible format placeholders as visible picture glyphs.
            buffer_flags(buffer, 8)
            buffer_guess(buffer)
            shape(font, buffer, None, 0)
            length = c.c_uint()
            glyphs = infos(buffer, c.byref(length))
            ids = [glyphs[i].codepoint for i in range(length.value)]
            ok = len(ids) == 1 and ids[0] != 0
            counts[status]['one_nonzero_glyph' if ok else 'missing_or_split'] += 1
            if not ok:
                failures.append({'sequence': ' '.join(f'{v:X}' for v in scalars),
                                 'status': status, 'glyph_ids': ids, 'description': match[3]})
        finally:
            buffer_destroy(buffer)
    actual = {key: sum(value.values()) for key, value in counts.items()}
    if actual != manifest['entries']:
        raise SystemExit('corpus enumeration incomplete')
    rgi_failures = sum(counts[key]['missing_or_split'] for key in ('fully-qualified', 'component'))
    print(json.dumps({'scope': 'font_shaping_only_not_colour_raster_or_terminal_acceptance',
        'unicode_emoji_version': manifest['unicode_emoji_version'],
        'font': str(args.font), 'font_sha256': hashlib.sha256(args.font.read_bytes()).hexdigest(),
        'harfbuzz_version': version().decode(),
        'harfbuzz_sha256': hashlib.sha256(args.harfbuzz.read_bytes()).hexdigest(),
        'counts': dict(counts), 'rgi_failures': rgi_failures, 'failures': failures}, indent=2))
finally:
    if font:
        font_destroy(font)
    face_destroy(face)
    blob_destroy(blob)
if args.require_complete and failures:
    raise SystemExit(1)
