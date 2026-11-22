#!/usr/bin/env python3
"""Writes src/blit_font_registry.h (ADR 0243 step 1) from the one font
registry, assets/fonts/fonts.source.json: every face Blit ships, its id,
role, how it is drawn, its file and family. The code names faces by role
from this header, never by file, and GET /v1/fonts reports its table.

ADR 0243 step 3: the registry's `sets` level declares every glyph set Blit
answers for by set/name. This script derives the set ids from each names
source (the Nerd names file's prefixes make the nerd-* sets), checks every
name against the address grammar of docs/protocols/glyph-sets.md, and writes
the set list and the retired names to the header; the server reads the names
themselves from the packaged files at start (src/blit_glyph_sets.h).

The header is FreeType-free and header-only (blit_font.c includes it), and
is committed; tools/tests/font_registry_gate.rb checks it equals this
script's output. Standard library only.

    python3 tools/fonts/build-font-registry.py src/blit_font_registry.h
    python3 tools/fonts/build-font-registry.py --check
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / 'assets/fonts/fonts.source.json'
HEADER = ROOT / 'src/blit_font_registry.h'
# The roles the code draws with: one file each, or an ordered list.
SINGLE = ['icons', 'nerd', 'colour', 'text_emoji', 'arabic_words',
          'pixel_fallback', 'pixel_wide']
LISTED = ['symbols', 'unifont']
# docs/protocols/glyph-sets.md section 4.1: a set id is an owner token and
# hyphen-joined parts, 1-32 octets; a name 1-96 octets.
SET_ID = re.compile(r'[a-z][a-z0-9]*(-[a-z0-9]+)*')
NAME = re.compile(r'[a-z0-9][a-z0-9_-]{0,95}')
SCOPES = ['portable', 'terminal-local']
FORMATS = ['blit-catalogue', 'nerd-glyphnames']
# The Glyph Protocol's registration block and, above it, the rest of Blit's
# old icon range (glyph sets section 9): no set may place a glyph there.
NO_GLYPHS = range(0x100000, 0x100494 + 1)


def faces(node):
    """Every face in the registry, in the file's order: an object with a role."""
    if isinstance(node, dict):
        if 'role' in node:
            yield node
            return
        for value in node.values():
            yield from faces(value)
    elif isinstance(node, list):
        for value in node:
            yield from faces(value)


def c_string(value):
    if value is None:
        return 'NULL'
    # Printable ASCII with nothing to escape, in C or JSON, and no % (a
    # file name is pasted into a format string, blit_raster.c's sibling).
    if not re.fullmatch(r'[ -~]*', value) or any(c in value for c in '"\\%'):
        raise SystemExit(f'not a plain C string: {value!r}')
    return f'"{value}"'


def codepoint(text, where):
    if not isinstance(text, str) or not re.fullmatch(r'[0-9A-Fa-f]{1,6}', text) or int(text, 16) > 0x10FFFF:
        raise SystemExit(f'{where}: not a codepoint: {text!r}')
    value = int(text, 16)
    if value in NO_GLYPHS:
        raise SystemExit(f'{where}: U+{value:X} is in the registration block or the old icon range')
    return value


def glyph_sets(registry, face_ids):
    """The sets level: each names source, the set ids derived from it, and
    the retired names, every name checked against the grammar."""
    level = registry.get('sets')
    if not isinstance(level, dict):
        raise SystemExit('the registry has no sets level')
    owners = level.get('owners')
    sources, sets, retired = [], [], []
    for key, entry in level.items():
        if not isinstance(entry, dict) or 'names' not in entry:
            continue
        where = f'sets.{key}'
        if entry.get('owner') not in owners:
            raise SystemExit(f'{where}: owner {entry.get("owner")!r} is not registered')
        if entry.get('scope') not in SCOPES:
            raise SystemExit(f'{where}: scope {entry.get("scope")!r}')
        faces = entry.get('faces')
        if not faces or any(face not in face_ids for face in faces):
            raise SystemExit(f'{where}: faces {faces} are not registry faces')
        names = entry['names']
        if names.get('format') not in FORMATS:
            raise SystemExit(f'{where}: names format {names.get("format")!r}')
        path = ROOT / 'assets/fonts' / names['file']
        document = json.loads(path.read_text(encoding='utf-8'))
        table = {}   # set id -> {name: codepoint}
        if names['format'] == 'blit-catalogue':
            if names.get('lists') != ['icons', 'text_glyphs', 'cell_geometry'] or entry.get('version') != 'sha256':
                raise SystemExit(f'{where}: a catalogue is read from its three lists and versioned by its SHA-256')
            version, lists, prefix = None, ','.join(names['lists']), None
            table[key] = {}
            for listed in names['lists']:
                for glyph in document[listed]:
                    table[key].setdefault(glyph['id'], [])
                    table[key][glyph['id']].append(codepoint(glyph['codepoint'], f"{where}/{glyph['id']}"))
        else:
            if names.get('split') != 'prefix':
                raise SystemExit(f'{where}: a Nerd names file is split by prefix')
            meta = document.pop('METADATA', {})
            version, lists = entry.get('version'), None
            if meta.get('version') != version:
                raise SystemExit(f"{where}: version {version!r}, the file says {meta.get('version')!r}")
            for name, glyph in document.items():
                prefix, _, rest = name.partition('-')
                table.setdefault(f"{entry['owner']}-{prefix}", {}).setdefault(rest, []).append(
                    codepoint(glyph['code'], f'{where}/{name}'))
        if version is not None and not re.fullmatch(r'[a-z0-9.-]{1,64}', version):
            raise SystemExit(f'{where}: version {version!r}')
        for set_id, members in table.items():
            if not (SET_ID.fullmatch(set_id) and len(set_id) <= 32 and
                    (set_id == entry['owner'] or set_id.startswith(entry['owner'] + '-'))):
                raise SystemExit(f'{where}: set id {set_id!r}')
            for name, points in members.items():
                if not NAME.fullmatch(name) or len(points) != 1:
                    raise SystemExit(f'{where}: {set_id}/{name!r} is not one name for one glyph')
            sets.append((set_id, len(sources), set_id[len(entry['owner']) + 1:] or None))
        for name, record in sorted(entry.get('retired_names', {}).items()):
            by = record.get('replaced_by', [])
            if not NAME.fullmatch(name) or name in table.get(key, {}) or len(table) != 1:
                raise SystemExit(f'{where}: retired name {name!r} is malformed, live, or in a split source')
            if any(b not in table[key] for b in by):
                raise SystemExit(f'{where}: {name} is replaced by a name the set does not have: {by}')
            # A catalogue's retired name had a codepoint its retired list keeps.
            if names['format'] == 'blit-catalogue':
                point = codepoint(record.get('codepoint'), f'{where}/{name}')
                if not any(int(r['from'], 16) <= point <= int(r['to'], 16) for r in document['retired']):
                    raise SystemExit(f'{where}: retired {name} at U+{point:X} is not in the catalogue\'s retired list')
            retired.append((key, name, ','.join(by) or None))
        sources.append((key, entry['owner'], entry['scope'], names['format'], names['file'],
                        version, ','.join(faces), lists))
    ids = [s[0] for s in sets]
    if len(set(ids)) != len(ids):
        raise SystemExit(f'two sources make one set: {ids}')
    return sources, sorted(sets), retired


def generate():
    registry = json.loads(REGISTRY.read_text(encoding='utf-8'))
    roles, draws = registry['registry']['roles'], registry['registry']['draws']
    table = list(faces(registry))
    ids = set()
    for face in table:
        for key in ('id', 'role', 'draw', 'file'):
            if not isinstance(face.get(key), str) or not face[key]:
                raise SystemExit(f'a face without {key}: {face}')
        if face['role'] not in roles:
            raise SystemExit(f"{face['id']}: unknown role {face['role']}")
        if face['draw'] not in draws:
            raise SystemExit(f"{face['id']}: unknown draw {face['draw']}")
        if face['id'] in ids:
            raise SystemExit(f"two faces are {face['id']}")
        ids.add(face['id'])
    missing = sorted(set(roles) - {face['role'] for face in table})
    if missing:
        raise SystemExit(f'roles with no face: {missing}')
    by_role = {}
    for face in table:
        by_role.setdefault(face['role'], []).append(face['file'])
    out = [
        '/* Generated by tools/fonts/build-font-registry.py from the font registry,',
        '   assets/fonts/fonts.source.json (ADR 0243). Do not edit: change the',
        '   registry and run the generator; font_registry_gate checks they agree.',
        '   Header-only and FreeType-free: blit_font.c includes it. */',
        '#ifndef BLIT_FONT_REGISTRY_H',
        '#define BLIT_FONT_REGISTRY_H',
        '',
        '/* Each role\'s face, relative to the fonts root (blit_font_root_file). */',
    ]
    for role in SINGLE:
        files = by_role.get(role, [])
        if len(files) != 1:
            raise SystemExit(f'role {role} needs one face, has {files}')
        out.append(f'#define BLIT_FACE_{role.upper()} {c_string(files[0])}')
    out.append('')
    out.append('/* Roles of several faces, tried in this order. */')
    for role in LISTED:
        files = by_role.get(role, [])
        out.append(f'#define BLIT_FACE_{role.upper()}_COUNT {len(files)}')
        out.append(f'#define BLIT_FACE_{role.upper()} {{ ' +
                   ', '.join(c_string(f) for f in files) + ' }')
    out += [
        '',
        "/* The icon catalogue the icons are built from and /v1/fonts/icons serves. */",
        f"#define BLIT_FONT_ICON_CATALOGUE {c_string(registry['custom_icons']['catalogue'])}",
        '',
        '/* The whole registry, one X(id, role, draw, file, family) per face in the',
        '   registry\'s order; family is NULL where the face has none (a PSF). */',
        f'#define BLIT_FONT_REGISTRY_COUNT {len(table)}',
        '#define BLIT_FONT_REGISTRY(X) \\',
    ]
    for face in table:
        out.append('    X(' + ', '.join(c_string(face.get(key)) for key in
                                        ('id', 'role', 'draw', 'file', 'family')) + ') \\')
    out[-1] = out[-1][:-2]
    sources, sets, retired = glyph_sets(registry, ids)
    out += [
        '',
        '/* ADR 0243 step 3: the glyph sets (docs/protocols/glyph-sets.md), from the',
        '   registry\'s sets level. One X(key, owner, scope, format, file, version,',
        '   faces, lists) per names source, read from the fonts root at start',
        '   (src/blit_glyph_sets.h): version NULL is the SHA-256 of the file, else',
        '   the release the file\'s own METADATA must say; faces and lists are',
        '   comma-joined (lists: a catalogue\'s lists, in order). */',
        f'#define BLIT_GLYPH_SOURCE_COUNT {len(sources)}',
        '#define BLIT_GLYPH_SOURCES(X) \\',
    ]
    for source in sources:
        out.append('    X(' + ', '.join(c_string(v) for v in source) + ') \\')
    out[-1] = out[-1][:-2]
    out += [
        '',
        '/* Every set, derived from its source (an index into the sources), in',
        '   bytewise order of id: X(id, source, prefix); prefix is the names',
        '   file\'s own (the Nerd name <prefix>-<rest> is nerd-<prefix>/<rest>),',
        '   NULL for a source that is one set. */',
        f'#define BLIT_GLYPH_SET_COUNT {len(sets)}',
        '#define BLIT_GLYPH_SETS(X) \\',
    ]
    for set_id, source, prefix in sets:
        out.append(f'    X({c_string(set_id)}, {source}, {c_string(prefix)}) \\')
    out[-1] = out[-1][:-2]
    out += [
        '',
        '/* Names a set published and no longer has: answered retired for good,',
        '   never unknown, never reused. X(set, name, replaced_by), replaced_by',
        '   comma-joined or NULL. */',
        f'#define BLIT_GLYPH_RETIRED_COUNT {len(retired)}',
        '#define BLIT_GLYPH_RETIRED(X) \\',
    ]
    for record in retired:
        out.append('    X(' + ', '.join(c_string(v) for v in record) + ') \\')
    out[-1] = out[-1][:-2]
    out += ['', '#endif', '']
    return '\n'.join(out)


def main():
    text = generate()
    if sys.argv[1:] == ['--check']:
        if HEADER.read_text(encoding='utf-8') != text:
            raise SystemExit(f'{HEADER.relative_to(ROOT)} is stale: run {Path(__file__).relative_to(ROOT)}')
        return
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == '-':
        sys.stdout.write(text)
    else:
        Path(sys.argv[1]).write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
