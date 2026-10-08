#!/usr/bin/env python3
"""Verify the checked-in runtime PSF covers every Hibr glyph we generate."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "assets/fonts/Blit-Hibr-Terminus16.psf"
GENERATOR = ROOT / "tools/build-hibr-font.py"


def load_required() -> tuple[int, ...]:
    spec = importlib.util.spec_from_file_location("build_hibr_font", GENERATOR)
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load the font generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return tuple(module.REQUIRED)


def check_font() -> tuple[int, int]:
    data = FONT.read_bytes()
    if data[:4] != b"\x72\xb5\x4a\x86":
        raise AssertionError("runtime font is not PSF2")
    version, header_size, flags, glyph_count, glyph_bytes, height, width = (
        struct.unpack_from("<7I", data, 4)
    )
    if version != 0 or header_size != 32 or not flags & 1:
        raise AssertionError("runtime font lacks a PSF2 UTF-8 map")
    if (width, height, glyph_bytes) != (8, 16, 16):
        raise AssertionError(
            f"unexpected runtime geometry {width}x{height}/{glyph_bytes}"
        )
    glyph_start = header_size
    table_start = glyph_start + glyph_count * glyph_bytes
    if table_start > len(data):
        raise AssertionError("runtime font glyph data is truncated")

    mapped: dict[int, int] = {}
    glyph = 0
    cursor = table_start
    while cursor < len(data) and glyph < glyph_count:
        value = data[cursor]
        cursor += 1
        if value == 0xFF:
            glyph += 1
            continue
        if value == 0xFE:
            continue
        length = 1
        if value & 0x80:
            if value & 0xE0 == 0xC0:
                length = 2
            elif value & 0xF0 == 0xE0:
                length = 3
            elif value & 0xF8 == 0xF0:
                length = 4
            else:
                raise AssertionError("invalid UTF-8 in runtime font map")
        if cursor + length - 1 > len(data):
            raise AssertionError("truncated UTF-8 in runtime font map")
        raw = bytes([value]) + data[cursor:cursor + length - 1]
        cursor += length - 1
        try:
            codepoint = ord(raw.decode("utf-8"))
        except UnicodeDecodeError as error:
            raise AssertionError("invalid UTF-8 in runtime font map") from error
        mapped.setdefault(codepoint, glyph)

    if glyph != glyph_count:
        raise AssertionError(
            f"runtime font map terminated at {glyph}/{glyph_count} glyphs"
        )

    required = load_required()
    missing = sorted(set(required) - set(mapped))
    if missing:
        formatted = ", ".join(f"U+{value:04X}" for value in missing)
        raise AssertionError(f"missing required Hibr glyphs: {formatted}")

    blank = []
    for codepoint in required:
        index = mapped[codepoint]
        bitmap = data[glyph_start + index * glyph_bytes:
                      glyph_start + (index + 1) * glyph_bytes]
        if not any(bitmap):
            blank.append(codepoint)
    if blank:
        formatted = ", ".join(f"U+{value:04X}" for value in blank)
        raise AssertionError(f"blank required Hibr glyphs: {formatted}")
    block_rows = {
        0x2580: range(0, 8), 0x2581: range(14, 16),
        0x2582: range(12, 16), 0x2583: range(10, 16),
        0x2584: range(8, 16), 0x2585: range(6, 16),
        0x2586: range(4, 16), 0x2587: range(2, 16),
        0x2588: range(0, 16),
    }
    malformed = []
    for codepoint, rows in block_rows.items():
        index = mapped[codepoint]
        expected = bytes(0xFF if row in rows else 0 for row in range(16))
        actual = data[glyph_start + index * glyph_bytes:
                      glyph_start + (index + 1) * glyph_bytes]
        if actual != expected:
            malformed.append(f"U+{codepoint:04X}")
    if malformed:
        raise AssertionError(
            "malformed exact block glyphs: " + ", ".join(malformed)
        )
    return len(required), len(mapped)


try:
    required_count, mapping_count = check_font()
except AssertionError as error:
    print(f"HIBR_FONT_MANIFEST_FAIL: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"HIBR_FONT_MANIFEST_PASS required={required_count} mappings={mapping_count}")
