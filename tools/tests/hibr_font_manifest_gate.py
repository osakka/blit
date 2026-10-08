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
    # Independent topology oracle for the terminal box-drawing set.  These
    # bytes intentionally mirror the 8x16 contract, not the generator code,
    # so a generator regression cannot make its own test pass.
    def box(style, topology):
        rows = {"light": (7,), "heavy": (7, 8), "double": (5, 10),
                "dashed": (7,), "rounded": (7,)}[style]
        columns = {"light": (3,), "heavy": (2, 3), "double": (2, 5),
                   "dashed": (3,), "rounded": (3,)}[style]
        bitmap = [[0] * 8 for _ in range(16)]
        if topology == "horizontal":
            xs = (0, 1, 4, 5) if style == "dashed" else range(8)
            for row in rows:
                for x in xs:
                    bitmap[row][x] = 1
        elif topology == "vertical":
            ys = range(0, 16, 2) if style == "dashed" else range(16)
            for y in ys:
                for column in columns:
                    bitmap[y][column] = 1
        else:
            right = topology in ("right_down", "right_up", "up_down_right")
            left = topology in ("left_down", "left_up", "up_down_left")
            down = topology in ("right_down", "left_down", "up_down_right", "up_down_left")
            up = topology in ("right_up", "left_up", "up_down_right", "up_down_left")
            if right:
                for row in rows:
                    for x in range(3, 8):
                        bitmap[row][x] = 1
            if left:
                for row in rows:
                    for x in range(4):
                        bitmap[row][x] = 1
            if down:
                for y in range(7, 16):
                    for column in columns:
                        bitmap[y][column] = 1
            if up:
                for y in range(8):
                    for column in columns:
                        bitmap[y][column] = 1
        return bytes(sum(1 << (7 - x) for x in range(8) if bitmap[y][x])
                     for y in range(16))

    box_expected = {
        0x2500: ("light", "horizontal"), 0x2501: ("heavy", "horizontal"),
        0x2502: ("light", "vertical"), 0x2504: ("dashed", "horizontal"),
        0x2506: ("dashed", "vertical"), 0x250C: ("light", "right_down"),
        0x2510: ("light", "left_down"), 0x2514: ("light", "right_up"),
        0x2518: ("light", "left_up"), 0x251C: ("light", "up_down_right"),
        0x2524: ("light", "up_down_left"), 0x2550: ("double", "horizontal"),
        0x2551: ("double", "vertical"), 0x2554: ("double", "right_down"),
        0x2557: ("double", "left_down"), 0x255A: ("double", "right_up"),
        0x255D: ("double", "left_up"), 0x2560: ("double", "up_down_right"),
        0x2563: ("double", "up_down_left"), 0x256D: ("rounded", "right_down"),
        0x256E: ("rounded", "left_down"), 0x256F: ("rounded", "left_up"),
        0x2570: ("rounded", "right_up"),
    }
    malformed = []
    for codepoint, description in box_expected.items():
        index = mapped[codepoint]
        actual = data[glyph_start + index * glyph_bytes:
                      glyph_start + (index + 1) * glyph_bytes]
        if actual != box(*description):
            malformed.append(f"U+{codepoint:04X}")
    if malformed:
        raise AssertionError(
            "malformed exact box glyphs: " + ", ".join(malformed)
        )
    special_expected = {
        0x223F: bytes((0x00, 0x00, 0x00, 0x03, 0x06, 0x0C, 0x18, 0x30,
                        0x60, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03)),
        0x2398: bytes((0x00, 0x1C, 0x14, 0x14, 0x14, 0x14, 0x17, 0x15,
                        0x15, 0x15, 0x15, 0x15, 0x1D, 0x00, 0x00, 0x00)),
        0x24D8: bytes((0x00, 0x18, 0x24, 0x42, 0x81, 0x81, 0x91, 0x81,
                        0x81, 0x81, 0x81, 0x42, 0x24, 0x18, 0x00, 0x00)),
        0x1F514: bytes((0x00, 0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E,
                         0x7E, 0x3C, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00)),
    }
    malformed = []
    for codepoint, expected in special_expected.items():
        index = mapped[codepoint]
        actual = data[glyph_start + index * glyph_bytes:
                      glyph_start + (index + 1) * glyph_bytes]
        if actual != expected:
            malformed.append(f"U+{codepoint:04X}")
    if malformed:
        raise AssertionError(
            "malformed exact special glyphs: " + ", ".join(malformed)
        )
    return len(required), len(mapped)


try:
    required_count, mapping_count = check_font()
except AssertionError as error:
    print(f"HIBR_FONT_MANIFEST_FAIL: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"HIBR_FONT_MANIFEST_PASS required={required_count} mappings={mapping_count}")
