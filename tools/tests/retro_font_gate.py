#!/usr/bin/env python3
"""Pinned retro font identity, full fallback coverage and geometry contract."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('retro_builder', ROOT / 'tools/fonts/build-retro-font.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
manifest = json.loads(builder.MANIFEST.read_text())
data = (builder.MANIFEST.parent / manifest['output_file']).read_bytes()
assert hashlib.sha256(data).hexdigest() == manifest['output_sha256'], 'retro artifact identity differs'
fallback = builder.psf_glyphs(builder.checked(builder.MANIFEST.parent / manifest['fallback_file'],
                                             manifest['fallback_sha256']))
retro = builder.psf_glyphs(data)
assert set(fallback) <= set(retro), 'retro lost existing Unicode aliases'
for cp, bitmap in fallback.items():
    if 0x2500 <= cp <= 0x259f or cp == 0x25c6 or builder.unicodedata.combining(chr(cp)):
        assert retro[cp] == bitmap, f'protected geometry changed: U+{cp:04X}'
    if any(bitmap):
        assert any(retro[cp]), f'nonblank fallback became blank: U+{cp:04X}'
changed = sum(retro[cp] != fallback[cp] for cp in range(33,127))
assert changed >= 40, f'retro face not materially distinct: {changed} printable ASCII glyphs'
source = os.environ.get('BLIT_SPLEEN_SOURCE')
if source:
    assert builder.build(Path(source)) == data, 'regeneration differs from checked-in artifact'
print(f'RETRO_FONT_PASS mappings={len(retro)} fallback_mappings={len(fallback)} '
      f'distinct_ascii={changed} geometry=preserved regeneration={"exact" if source else "not_requested"}')
