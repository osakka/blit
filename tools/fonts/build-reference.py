#!/usr/bin/env python3
"""Generate repository-web glyph reference from pinned catalogues and fonts.

Build-time only; requires the existing fontTools 4.60.1 tooling.
--check compares without writing. No network or installed-font discovery.
"""
import argparse
import hashlib
import json
from pathlib import Path

import fontTools
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / 'assets/fonts'
FAMILIES = {
    'cod': 'Codicons', 'custom': 'Nerd custom symbols (not Blit originals)',
    'dev': 'Devicons', 'extra': 'Nerd extras', 'fa': 'Font Awesome',
    'fae': 'Font Awesome Extension', 'iec': 'IEC power symbols',
    'indent': 'Indent guides', 'indentation': 'Indentation guides',
    'linux': 'Font Logos / distributions', 'md': 'Material Design Icons',
    'oct': 'Octicons', 'pl': 'Powerline', 'ple': 'Powerline Extra Symbols',
    'pom': 'Pomicons', 'seti': 'Seti UI', 'weather': 'Weather Icons',
}


def load(name):
    return json.loads((FONTS / name).read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ranges(values):
    result = []
    for cp in sorted(values):
        if result and cp == result[-1][1] + 1:
            result[-1][1] = cp
        else:
            result.append([cp, cp])
    return [[f'{a:04X}', f'{b:04X}'] for a, b in result]


def generate():
    if fontTools.__version__ != '4.60.1':
        raise SystemExit('Use pinned fontTools 4.60.1 (same as font builders)')
    modern, arabic = load('fonts.source.json'), load('arabic-fonts.source.json')
    nerd = load(modern['catalogue']['file'])
    assert digest(FONTS / modern['catalogue']['file']) == modern['catalogue']['sha256']
    icons = load(modern['custom_icons']['catalogue'])
    assert digest(FONTS / modern['custom_icons']['catalogue']) == modern['custom_icons']['sources']['icons/catalogue.json']
    groups = {}
    for name, entry in nerd.items():
        if name == 'METADATA':
            continue
        cp = int(entry['code'], 16)
        assert entry['char'] == chr(cp), name
        family = name.split('-', 1)[0]
        assert family in FAMILIES, f'New family needs a reference title: {family}'
        groups.setdefault(family, []).append((name, cp))
    distinct = {cp for entries in groups.values() for _, cp in entries}
    assert len(distinct) == modern['catalogue']['unique_codepoints']
    # ADR 0195: the text fonts as shipped, and the glyph stack every one of
    # them draws through.
    stack = modern['stack']
    entries = [('text-' + Path(t['file']).stem, t) for t in modern['text']]
    entries += [('stack-icons', stack['icons']), ('stack-nerd', stack['nerd'])]
    entries += [('stack-' + Path(e['file']).stem, e) for e in stack['symbols'] + stack['unifont']]
    entries += [('arabic', arabic['modern']), ('colour-emoji', modern['colour']),
                ('text-emoji', modern['text_emoji'])]
    coverage = {'format_version': 1, 'scope': 'Exact native asset cmap, not runtime routing, ink, width or shaping conformance. Ranges are inclusive hexadecimal scalars.', 'faces': {}}
    cmaps = {}
    for role, entry in entries:
        path = FONTS / entry['file']
        assert digest(path) == entry['sha256'], path
        with TTFont(path) as face:
            cmap = face.getBestCmap()
        cmaps[role] = cmap
        coverage['faces'][role] = {'file': entry['file'], 'sha256': entry['sha256'],
                                    'scalar_count': len(cmap), 'ranges': ranges(cmap)}
    assert distinct <= cmaps['stack-nerd'].keys(), 'stack-nerd'
    assert all(int(i['codepoint'], 16) in cmaps['stack-icons'] for i in icons['icons']), 'stack-icons'
    outputs = {'coverage.json': json.dumps(coverage, indent=2) + '\n'}
    intro = '# Generated glyph catalogue\n\n[Developer guide](../font-reference.md) · [Special additions](special.md) · [Exact native coverage](coverage.json)\n\n'
    intro += 'Generated from pinned Nerd Fonts3.4.0 and packaged font cmaps. Do not edit these tables; run `tools/fonts/build-reference.py`.\n\n'
    intro += f'{sum(map(len, groups.values())):,} names/aliases, **{len(distinct):,} distinct codepoints**, drawn for every Blit font from the glyph stack (ADR 0195). Names can alias the same scalar.\n\n'
    intro += '| Category | Names/aliases | Distinct codepoints |\n| --- | ---: | ---: |\n'
    for family, items in sorted(groups.items()):
        intro += f'| [{FAMILIES[family]}]({family}.md) | {len(items)} | {len({cp for _, cp in items})} |\n'
        page = f'# {FAMILIES[family]}\n\n[Category index](README.md) · [Integration rules](../font-reference.md)\n\n'
        page += 'Every Blit font draws these, from the glyph stack. Symbols use the terminal foreground colour. The glyph column uses your browser font, not the Blit rasterizer; codepoints/JSON are authoritative.\n\n'
        page += '| Name | Codepoint | Copy glyph | JSON string |\n| --- | --- | --- | --- |\n'
        for name, cp in sorted(items):
            page += f'| `{name}` | `U+{cp:04X}` | `{chr(cp)}` | `{json.dumps(chr(cp), ensure_ascii=True)}` |\n'
        outputs[f'{family}.md'] = page
    intro += '\n## Native face inventory\n\nThese are asset cmap counts, not glyph counts, application coverage or effective runtime fallback inventories. Arabic companions contain other characters that are not automatically routed through them.\n\n'
    intro += '| Face role | Asset | Mapped scalars |\n| --- | --- | ---: |\n'
    for role, entry in coverage['faces'].items():
        intro += f'| {role} | `{entry["file"]}` | {entry["scalar_count"]} |\n'
    outputs['README.md'] = intro
    special = '# Blit/Hibr special additions\n\n[Developer guide](../font-reference.md) · [Canonical runtime catalogue](../../assets/fonts/icons/catalogue.json)\n\n'
    special += 'Generated from the same catalogue served by `GET /v1/fonts/icons`. These original intrinsic-colour icons are one cell each in every native font, from the glyph stack. Supplementary private-use assignments are a Blit contract, not standard Unicode or Nerd Fonts assignments.\n\n'
    special += '| Semantic ID | Codepoint | JSON string | Signal level |\n| --- | --- | --- | --- |\n'
    for icon in icons['icons']:
        cp = int(icon['codepoint'], 16)
        special += f'| `{icon["id"]}` | `U+{cp:04X}` | `{json.dumps(chr(cp))}` | {icon.get("signal_level", "—")} |\n'
    special += '\n## Themed text additions\n\nText aliases use the terminal foreground, not intrinsic colour.\n\n'
    special += '| Semantic ID | Codepoint | JSON string |\n| --- | --- | --- |\n'
    for icon in icons['icons']:
        if 'text_alias' in icon:
            cp = int(icon['text_alias'], 16)
            special += f'| `{icon["id"]}` (themed alias) | `U+{cp:04X}` | `{json.dumps(chr(cp))}` |\n'
    for icon in icons['text_glyphs']:
        cp = int(icon['codepoint'], 16)
        special += f'| `{icon["id"]}` | `U+{cp:04X}` | `{json.dumps(chr(cp))}` |\n'
    special += '\nBerkanan preserves its runic meaning; it is not the Bluetooth icon. Existing standard/Nerd assignments are not repurposed.\n'
    special += '\n## Cell geometry\n\nDrawn by the shared raster at every cell size, edge to edge, in the terminal foreground: no outline font carries them, so neighbouring cells join without a seam. One cell each, in every font.\n\n'
    special += '| Semantic ID | Codepoint | JSON string | Shape |\n| --- | --- | --- | --- |\n'
    for icon in icons.get('cell_geometry', []):
        cp = int(icon['codepoint'], 16)
        special += f'| `{icon["id"]}` | `U+{cp:04X}` | `{json.dumps(chr(cp))}` | {icon["shape"]} |\n'
    special += '\n[Production-raster specimen: text fonts, Blit icons, file icons, Nerd glyphs and emoji](../images/blit-fonts.png).\n'
    outputs['special.md'] = special
    emoji_root = ROOT / 'assets/unicode/emoji-18.0'
    emoji_manifest = json.loads((emoji_root / 'source.json').read_text(encoding='utf-8'))
    emoji_file = emoji_root / emoji_manifest['test_data']['file']
    assert digest(emoji_file) == emoji_manifest['test_data']['sha256']
    emoji_groups, statuses = {}, {}
    for line in emoji_file.read_text(encoding='utf-8').splitlines():
        if line.startswith('# group: '):
            group = line[len('# group: '):]
            emoji_groups[group] = []
        elif line.startswith('# subgroup: '):
            subgroup = line[len('# subgroup: '):]
        elif line and not line.startswith('#'):
            data, comment = line.split('#', 1)
            scalars, status = (p.strip() for p in data.split(';'))
            shown, version, name = comment.split(maxsplit=2)
            text = ''.join(chr(int(cp, 16)) for cp in scalars.split())
            assert text == shown and version.startswith('E')
            statuses[status] = statuses.get(status, 0) + 1
            emoji_groups[group].append((subgroup, name, text, scalars, status))
    assert statuses == emoji_manifest['entries']
    index = '# Emoji18.0 reference corpus\n\n[Developer guide](../font-reference.md) · [Glyph index](README.md)\n\n'
    index += 'Generated from the pinned Unicode test corpus, including qualification variants. This is a reference repertoire, not a claim of physical or end-to-end conformance. Emoji clusters require explicit emoji layout; scalar remains the default. See the developer guide before integration.\n\n'
    index += '| Category | Entries including variants |\n| --- | ---: |\n'
    for number, (group, items) in enumerate(emoji_groups.items(), 1):
        filename = f'emoji-{number:02}.md'
        index += f'| [{group}]({filename}) | {len(items)} |\n'
        page = f'# Emoji: {group}\n\n[Emoji categories](emoji.md) · [Integration rules](../font-reference.md)\n\n'
        page += 'Browser glyphs are not Blit raster evidence. Fully-qualified/component entries form RGI; minimally-qualified/unqualified rows are compatibility variants, not additional RGI emoji.\n\n'
        previous = None
        for subgroup, name, text, scalars, status in items:
            if subgroup != previous:
                page += f'\n## {subgroup}\n\n| Name | Copy sequence | Codepoints | JSON string | Qualification |\n| --- | --- | --- | --- | --- |\n'
                previous = subgroup
            safe_name = name.replace('|', '\\|')
            page += f'| {safe_name} | `{text}` | `{scalars}` | `{json.dumps(text)}` | {status} |\n'
        page += '\n[Unicode data licence](../../assets/unicode/emoji-18.0/LICENSE.txt).\n'
        outputs[filename] = page
    outputs['emoji.md'] = index
    outputs['README.md'] += '\n[All emoji categories and qualification variants](emoji.md).\n'
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/fonts')
    args = parser.parse_args()
    outputs = generate()
    stale = []
    for name, content in outputs.items():
        path = args.output / name
        if args.check:
            if not path.is_file() or path.read_text(encoding='utf-8') != content:
                stale.append(name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
    unexpected = {p.name for p in args.output.glob('*') if p.is_file()} - outputs.keys()
    if stale or unexpected:
        raise SystemExit(f'FONT_REFERENCE_FAIL stale={stale} unexpected={sorted(unexpected)}')
    print(f'FONT_REFERENCE_PASS pages={len(outputs)-1} native_faces={len(json.loads(outputs["coverage.json"])["faces"])} catalogue=10386_distinct check={args.check}')


if __name__ == '__main__':
    main()
