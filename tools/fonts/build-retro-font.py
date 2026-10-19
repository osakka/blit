#!/usr/bin/env python3
"""Build the pinned Spleen/Hibr profile, retaining canonical cell geometry.

No runtime renderer or font dependency is introduced. Missing codepoints
come from the existing checked-in Hibr font, never a host-dependent fallback.
"""
import argparse
import hashlib
import json
import struct
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/fonts/Blit-Hibr-Spleen16.source.json'


def checked(path, expected):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f'source hash mismatch: {path}')
    return data


def psf_glyphs(data):
    magic, version, header, flags, count, stride, height, width = struct.unpack_from('<8I', data)
    if (magic, version, header, flags, stride, height, width) != (0x864AB572, 0, 32, 1, 16, 16, 8):
        raise ValueError('expected canonical 8x16 PSF2 fallback')
    table = data[header + count * stride:].split(b'\xff')
    if len(table) != count + 1 or table[-1]:
        raise ValueError('invalid PSF Unicode table')
    glyphs = {}
    for index, entry in enumerate(table[:-1]):
        bitmap = data[header + index * stride:header + (index + 1) * stride]
        if len(bitmap) != stride:
            raise ValueError('truncated PSF bitmap')
        # PSF sequences are not scalar aliases. Do not invent coverage for them.
        for char in entry.split(b'\xfe')[0].decode('utf-8'):
            glyphs.setdefault(ord(char), bitmap)
    return glyphs


def bdf_glyphs(data):
    glyphs = {}
    codepoint = None
    rows = None
    bbx = advance = None
    for line in data.decode('ascii').splitlines():
        if line.startswith('STARTCHAR '):
            codepoint = rows = bbx = advance = None
        elif line.startswith('ENCODING '):
            codepoint = int(line.split()[1])
        elif line.startswith('BBX '):
            bbx = tuple(map(int, line.split()[1:]))
        elif line.startswith('DWIDTH '):
            advance = tuple(map(int, line.split()[1:]))
        elif line == 'BITMAP':
            rows = []
        elif line == 'ENDCHAR':
            if codepoint is None or codepoint < 0:
                continue
            if bbx != (8, 16, 0, -4) or advance != (8, 0) or rows is None:
                raise ValueError(f'unsupported BDF metrics at U+{codepoint:04X}')
            bitmap = b''.join(rows)
            if len(bitmap) != 16 or codepoint in glyphs:
                raise ValueError('invalid or duplicate BDF glyph')
            glyphs[codepoint] = bitmap
            rows = None
        elif rows is not None:
            value = bytes.fromhex(line)
            if len(value) != 1:
                raise ValueError('invalid 8px BDF bitmap row')
            rows.append(value)
    if not all(cp in glyphs for cp in range(32, 127)):
        raise ValueError('BDF lacks printable ASCII')
    return glyphs


def build(source):
    manifest = json.loads(MANIFEST.read_text())
    upstream = bdf_glyphs(checked(source, manifest['source_sha256']))
    fallback = psf_glyphs(checked(MANIFEST.parent / manifest['fallback_file'],
                                 manifest['fallback_sha256']))
    result = dict(fallback)
    for cp, bitmap in upstream.items():
        # Keep established geometry (including the fitted control diamond)
        # and combining overlays; this is a text-face addition, not a new
        # interpretation of box junctions or half-blocks.
        protected = 0x2500 <= cp <= 0x259f or cp == 0x25c6 or unicodedata.combining(chr(cp))
        if cp in fallback and (protected or not any(bitmap)):
            continue
        result[cp] = bitmap
    ordered = sorted(result)
    header = struct.pack('<8I', 0x864AB572, 0, 32, 1, len(ordered), 16, 16, 8)
    return header + b''.join(result[cp] for cp in ordered) + b''.join(
        chr(cp).encode('utf-8') + b'\xff' for cp in ordered)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = build(args.source)
    args.output.write_bytes(data)
    print(f'RETRO_FONT_BUILT bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
