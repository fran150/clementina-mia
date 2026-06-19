#!/usr/bin/env python3
"""Generate canonical MIA charset .bin files from various source fonts.

A MIA charset .bin is a flat image of up to 512 glyphs (8 bytes each), stored as
256-glyph blocks of 2048 bytes. The loader split-loads it: block i goes into
plane 0 of CHR bank i, so a two-block charset fills plane 0 of banks 0 and 1 and
both halves can be on screen at once via the per-cell CHR_ALT attribute:

    block 0 (0x0000..0x07FF) -> CHR bank 0, plane 0 : primary   (ASCII text)
    block 1 (0x0800..0x0FFF) -> CHR bank 1, plane 0 : alternate (graphics / line set)

Layout rules ("canonical" / ASCII):
  * Block 0 slot N renders the glyph for ASCII codepoint N. Printable 0x20-0x7E
    is populated; 0x00-0x1F and 0x7F are blank; 0x80-0xFF reserved/extended.
    The 6502 nametable therefore stores ASCII bytes directly (no screen codes).
  * Block 1 carries a graphics / line-drawing set, selected per cell with the
    CHR_ALT attribute bit (the layer's alternate CHR bank).

Bytes are stored pre-reversed to MIA pixel order (bit 0 = leftmost pixel), so
both the firmware (memcpy from a generated C array) and the emulator (copy from
an embedded asset) load each block with a flat copy.

Usage:
    python3 scripts/generate_charset.py [profile] [--out PATH] [--preview]

Profiles:
    openroms   MEGA65 open-roms PETSCII set, remapped to ASCII (default)
               (more profiles -- unscii, x16 -- are added in a later phase)
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHARSETS_DIR = REPO_ROOT / "charsets"
DEFAULT_OPENROMS_ROM = REPO_ROOT / "scripts" / "chargen_openroms.rom"

PLANE_SIZE = 2048          # 256 glyphs * 8 rows
GLYPH_ROWS = 8
CHARSET_SIZE = PLANE_SIZE * 2


def reverse_bits(value: int) -> int:
    result = 0
    for i in range(8):
        result |= ((value >> i) & 1) << (7 - i)
    return result


REVERSE_TABLE = [reverse_bits(b) for b in range(256)]


def glyph(rows) -> bytes:
    """Build an 8-byte glyph from 8 strings of '#'/'.' (MSB = leftmost pixel)."""
    assert len(rows) == GLYPH_ROWS, "a glyph needs exactly 8 rows"
    out = bytearray(GLYPH_ROWS)
    for y, row in enumerate(rows):
        row = row.ljust(8, ".")
        out[y] = sum((1 << (7 - x)) for x in range(8) if row[x] == "#")
    return bytes(out)


def hflip(g: bytes) -> bytes:
    """Mirror a glyph left<->right (MSB-left bytes)."""
    return bytes(REVERSE_TABLE[b] for b in g)


# Glyphs the C64/PETSCII set lacks, drawn in a chunky style to match open-roms.
# Keys are ASCII codepoints. Backslash (0x5C) is derived from '/' at runtime.
SYNTH_GLYPHS = {
    0x5E: glyph([  # ^ caret
        "...##...",
        "..####..",
        ".##..##.",
        "........",
        "........",
        "........",
        "........",
        "........",
    ]),
    0x5F: glyph([  # _ underscore
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "######..",
        "........",
    ]),
    0x60: glyph([  # ` grave accent
        ".##.....",
        "..##....",
        "...#....",
        "........",
        "........",
        "........",
        "........",
        "........",
    ]),
    0x7B: glyph([  # {
        "...###..",
        "..##....",
        "..##....",
        ".##.....",
        "..##....",
        "..##....",
        "...###..",
        "........",
    ]),
    0x7C: glyph([  # |
        "...##...",
        "...##...",
        "...##...",
        "...##...",
        "...##...",
        "...##...",
        "...##...",
        "........",
    ]),
    0x7D: glyph([  # }
        ".###....",
        "...##...",
        "...##...",
        "....##..",
        "...##...",
        "...##...",
        ".###....",
        "........",
    ]),
    0x7E: glyph([  # ~
        "........",
        "........",
        ".###.##.",
        "##.###..",
        "........",
        "........",
        "........",
        "........",
    ]),
}


def _slice(block: bytes, code: int) -> bytes:
    off = code * GLYPH_ROWS
    return block[off:off + GLYPH_ROWS]


def c64_lower_to_ascii_plane(block: bytes) -> bytearray:
    """Remap a C64 'lowercase/uppercase' screen-code block to an ASCII plane.

    Source screen codes: @=0, a-z=1-26, [=27, ]=29, 0x20-0x3F punctuation/digits
    already aligned, A-Z=65-90.
    """
    plane = bytearray(PLANE_SIZE)  # all blank

    def put(cp: int, g: bytes):
        plane[cp * GLYPH_ROWS:cp * GLYPH_ROWS + GLYPH_ROWS] = g

    # space, punctuation, digits (0x20-0x3F) and uppercase A-Z (0x41-0x5A) are
    # already at their ASCII positions in the source block.
    for cp in range(0x20, 0x40):
        put(cp, _slice(block, cp))
    for cp in range(0x41, 0x5B):
        put(cp, _slice(block, cp))

    put(0x40, _slice(block, 0))    # @
    put(0x5B, _slice(block, 27))   # [
    put(0x5D, _slice(block, 29))   # ]
    put(0x5C, hflip(_slice(block, 0x2F)))  # \  (mirror of /)

    for i in range(26):            # a-z  <-  screen codes 1..26
        put(0x61 + i, _slice(block, 1 + i))

    for cp, g in SYNTH_GLYPHS.items():
        put(cp, g)

    return plane


def profile_openroms(rom_path: Path) -> bytes:
    rom = rom_path.read_bytes()
    if len(rom) != 4096:
        raise SystemExit(f"error: expected 4096-byte charset, got {len(rom)} from {rom_path}")

    src_graphics = rom[0:PLANE_SIZE]                 # uppercase / graphics set
    src_text = rom[PLANE_SIZE:2 * PLANE_SIZE]        # lowercase / uppercase set

    text_block = c64_lower_to_ascii_plane(src_text)  # ASCII-ordered text set
    graphics_block = bytearray(src_graphics)         # graphics / line set kept as-is

    # Split-load layout: block 0 (primary bank) = ASCII text, block 1 (alt bank)
    # = graphics. The loader places block i into plane 0 of CHR bank i.
    out = bytearray(CHARSET_SIZE)
    out[0:PLANE_SIZE] = bytes(REVERSE_TABLE[b] for b in text_block)
    out[PLANE_SIZE:] = bytes(REVERSE_TABLE[b] for b in graphics_block)
    return bytes(out)


PROFILES = {
    "openroms": lambda: profile_openroms(DEFAULT_OPENROMS_ROM),
}


def preview(data: bytes, text: str):
    """Render a sample string from block 0 (the ASCII text set), MIA pixel order."""
    p1 = data[0:PLANE_SIZE]
    for cp in [ord(c) for c in text]:
        print(f"\n'{chr(cp)}' (0x{cp:02X})")
        base = cp * GLYPH_ROWS
        for y in range(GLYPH_ROWS):
            b = p1[base + y]
            print("".join("#" if (b >> x) & 1 else "." for x in range(8)))


def main() -> int:
    args = [a for a in sys.argv[1:]]
    do_preview = "--preview" in args
    args = [a for a in args if a != "--preview"]

    out_path = None
    if "--out" in args:
        i = args.index("--out")
        out_path = Path(args[i + 1])
        del args[i:i + 2]

    profile = args[0] if args else "openroms"
    if profile not in PROFILES:
        raise SystemExit(f"unknown profile '{profile}'. known: {', '.join(PROFILES)}")

    data = PROFILES[profile]()
    assert len(data) == CHARSET_SIZE

    if out_path is None:
        CHARSETS_DIR.mkdir(exist_ok=True)
        out_path = CHARSETS_DIR / f"{profile}.bin"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)
    print(f"wrote {out_path} ({len(data)} bytes, profile '{profile}')")

    if do_preview:
        preview(data, "Aa Bb Zz 09 @[]\\_{|}~")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
