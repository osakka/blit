#!/usr/bin/env python3
"""The characters Hibr's desktop draws, from its wm/glyphs.hibr (Gitea #118).

    snapshot-hibr-ui.py <hibr checkout> <tag>            write assets/fonts/hibr-ui-glyphs.json
    snapshot-hibr-ui.py --check <hibr checkout> [<tag>]  exit 1 when the snapshot differs

Every scalar above ASCII that glyphs.hibr names -- as a literal character, a
\\uXXXX or \\UXXXXXXXX escape, or a run of \\xHH UTF-8 bytes -- sorted. The
snapshot records the Hibr tag and the file's SHA-256 it was taken from, so it
can be checked and regenerated rather than hand-kept. With no tag, --check uses
the checkout's newest v* tag: the snapshot is to follow the Hibr people run.

The icon gates (icon_clip_gate, custom_icon_gate) draw every scalar in it at
every size in every text font; fonts.source.json pins its hash and count.
"""
import hashlib
import json
import re
import subprocess
import sys

PATH = 'programs/desktop/wm/glyphs.hibr'
OUT = 'assets/fonts/hibr-ui-glyphs.json'
SCOPE = 'Hibr wm/glyphs.hibr Unicode literals and escaped scalars; not all applications'
ESCAPES = re.compile(r'\\U([0-9A-Fa-f]{8})|\\u([0-9A-Fa-f]{4,6})|((?:\\x[0-9A-Fa-f]{2})+)|([^\x00-\x7f])')


def scalars(text):
    found = set()
    for m in ESCAPES.finditer(text):
        if m.group(1):
            found.add(int(m.group(1), 16))
        elif m.group(2):
            found.add(int(m.group(2), 16))
        elif m.group(3):
            raw = bytes(int(h, 16) for h in re.findall(r'\\x([0-9A-Fa-f]{2})', m.group(3)))
            try:
                found.update(ord(c) for c in raw.decode('utf-8'))
            except UnicodeDecodeError:
                pass
        else:
            found.add(ord(m.group(4)))
    return sorted(c for c in found if c > 0x7f)


def git(checkout, *args):
    return subprocess.run(['git', '-C', checkout, *args], check=True, capture_output=True).stdout


def newest_tag(checkout):
    tags = git(checkout, 'tag', '--list', 'v*', '--sort=-version:refname').decode().split()
    if not tags:
        raise SystemExit('snapshot-hibr-ui: no v* tag in %s' % checkout)
    return tags[0]


def snapshot(checkout, tag):
    source = git(checkout, 'show', '%s:%s' % (tag, PATH))
    return {
        'version': 2,
        'scope': SCOPE,
        'hibr': tag,
        'source_sha256': hashlib.sha256(source).hexdigest(),
        'scalars': ['%04X' % c for c in scalars(source.decode('utf-8'))],
    }


def main(argv):
    check = argv[:1] == ['--check']
    if check:
        argv = argv[1:]
    if not 1 <= len(argv) <= 2 or (not check and len(argv) != 2):
        raise SystemExit(__doc__)
    checkout = argv[0]
    tag = argv[1] if len(argv) == 2 else newest_tag(checkout)
    want = snapshot(checkout, tag)
    if not check:
        with open(OUT, 'w', encoding='utf-8') as f:
            json.dump(want, f, indent=2)
            f.write('\n')
        print('%s: %d scalars from Hibr %s' % (OUT, len(want['scalars']), tag))
        return 0
    have = json.load(open(OUT, encoding='utf-8'))
    if have.get('source_sha256') == want['source_sha256'] and have.get('scalars') == want['scalars']:
        print('HIBR_UI_GLYPHS_PASS %d scalars, Hibr %s' % (len(want['scalars']), tag))
        return 0
    new = sorted(set(want['scalars']) - set(have.get('scalars', [])))
    gone = sorted(set(have.get('scalars', [])) - set(want['scalars']))
    print('HIBR_UI_GLYPHS_FAIL the snapshot is Hibr %s, not %s: %d new (%s), %d gone (%s); '
          'regenerate with tools/fonts/snapshot-hibr-ui.py <hibr> %s and update fonts.source.json'
          % (have.get('hibr', 'unrecorded'), tag, len(new), ' '.join(new[:12]), len(gone), ' '.join(gone[:12]), tag))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
