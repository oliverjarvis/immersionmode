"""font.bin (the anti-aliased faces drawn on the bottom screen) and the payload's built-in face.

Glyphs are rendered from Noto Sans JP (SIL Open Font License 1.1) with four levels of coverage:
17 px Medium for dialog text and headwords, 13 px Regular for the rest of the card. Only the
characters the game's Japanese text and the card entries use go into font.bin. A third face, 13 px
Noto Sans Italic (same licence), holds Latin letters and punctuation for English in italics.
"""
from __future__ import annotations

import hashlib
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .text import SPECIAL_CHARS, Literal, load_archive, parse_segments

NOTO_COMMIT = "295d98a7a0c17c68f1341eaeea354e7960ea70d3"          # google/fonts
NOTO_URL = (f"https://raw.githubusercontent.com/google/fonts/{NOTO_COMMIT}/"
            "ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf")
NOTO_FILE = "NotoSansJP-wght.ttf"
NOTO_SHA256 = "c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f"
ITALIC_URL = f"https://raw.githubusercontent.com/google/fonts/{NOTO_COMMIT}/ofl/notosans/NotoSans-Italic.ttf"
ITALIC_FILE = "NotoSans-Italic.ttf"
ITALIC_SHA256 = "6c0681558d11213df5f979cb677f63acec40a774497ee9bee048c53eb485574c"
FONT_MAGIC = b"JPRF"
FONT_VERSION = 3
FONT_MAX_BYTES = 0x34000            # the payload's buffer for font.bin (display.c FONT_BYTES)
TEXT_FACE = (17, "Medium")          # dialog text and headwords
SMALL_FACE = (13, "Regular")        # the rest of the card, the block counter, messages
ITALIC_FACE = (13, None)            # English in italics: translations and grammar notes (a static font)
ITALIC_CHARS = (tuple(range(0x20, 0x7F)) + tuple(range(0xA0, 0x100))   # ASCII and Latin-1: Pokémon
                + (0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026))
JAPANESE_ARCHIVES = (30, 31, 40, 41)
EXTRA_RANGES = ((0x0020, 0x007E), (0x3000, 0x30FF), (0xFF01, 0xFF9F))
EXTRA_CHARS = (0x2015, 0x2025, 0x2026, 0x2640, 0x2642, 0x25B6, 0x25C0, 0x301C, 0xFF5E, 0x00C9, 0x00E9)   # É é: POKÉMON in the peek
SMALL_RANGES = ((0x0020, 0x007E), (0x3041, 0x30FC))
SMALL_CHARS = (0x00B7, 0x2026, 0x2192, 0x3001, 0x3002, 0x300C, 0x300D, 0x301C)
BUILTIN_CHARS = range(0x20, 0x7F)
STAND_INS = {cp: ord(ch) for cp, ch in SPECIAL_CHARS.items()}      # the game's private-use characters
BOX_REFERENCE = (0x6F22, 0x48)      # 漢, else H: how tall the box for a missing glyph is
NO_GLYPH = ""                 # Noto has no private-use glyphs: renders as its missing-glyph box


class FontError(Exception):
    pass


@dataclass(frozen=True)
class Glyph:
    advance: int
    x: int              # left edge, from the pen position
    y: int              # top edge, from the baseline (negative: above it)
    width: int
    height: int
    levels: bytes       # width * height coverage values 0..3, row by row


@dataclass(frozen=True)
class Face:
    size: int
    ascent: int
    descent: int
    glyphs: dict[int, Glyph]


def _fetch(cache: Path, name: str, url: str, sha256: str) -> Path:
    path = cache / "fonts" / name
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["curl", "-fL", "--retry", "3", "-o", str(path), url], check=True)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        path.unlink()
        raise FontError(f"{path.name} has SHA-256 {digest}, expected {sha256}")
    return path


def fetch_noto(cache: Path) -> Path:
    return _fetch(cache, NOTO_FILE, NOTO_URL, NOTO_SHA256)


def fetch_italic(cache: Path) -> Path:
    return _fetch(cache, ITALIC_FILE, ITALIC_URL, ITALIC_SHA256)


def _open(path: Path, size: int, style: str):
    from PIL import ImageFont

    face = ImageFont.truetype(str(path), size)
    if style is not None:   # a variable font's named instance; a static font has none
        face.set_variation_by_name(style)
    return face


def _render(face, ch: str) -> Glyph:
    from PIL import Image, ImageDraw

    size = face.size
    canvas = Image.new("L", (size * 4, size * 3), 0)
    ImageDraw.Draw(canvas).text((size, size * 2), ch, font=face, fill=255, anchor="ls")
    levels = canvas.point(lambda v: (v * 3 + 127) // 255)
    advance = round(face.getlength(ch))
    box = levels.getbbox()
    if box is None:
        return Glyph(advance, 0, 0, 0, 0, b"")
    crop = levels.crop(box)
    return Glyph(advance, box[0] - size, box[1] - size * 2, crop.width, crop.height, crop.tobytes())


def render_face(path: Path, size: int, style: str, charset) -> Face:
    face = _open(path, size, style)
    missing = _render(face, NO_GLYPH)
    glyphs = {}
    for cp in sorted(charset):
        if 0xE000 <= cp <= 0xF8FF and cp not in STAND_INS:
            continue
        glyph = _render(face, chr(STAND_INS.get(cp, cp)))
        if missing.levels and glyph == missing:
            continue
        glyphs[cp] = glyph
    ref = next((glyphs[cp] for cp in BOX_REFERENCE if cp in glyphs), None)
    ascent, descent = (-ref.y, max(ref.y + ref.height, 0)) if ref else (size, 0)
    return Face(size, ascent, descent, glyphs)


def _pack(glyph: Glyph) -> bytes:
    stride = (glyph.width + 3) // 4
    out = bytearray(stride * glyph.height)
    for row in range(glyph.height):
        for col in range(glyph.width):
            out[row * stride + col // 4] |= glyph.levels[row * glyph.width + col] << (6 - 2 * (col % 4))
    return bytes(out)


def _unpack(data: bytes, width: int, height: int) -> bytes:
    stride = (width + 3) // 4
    return bytes((data[row * stride + col // 4] >> (6 - 2 * (col % 4))) & 3
                 for row in range(height) for col in range(width))


def _align(out: bytearray) -> None:
    out += bytes(-len(out) % 4)


def build_font(faces, limit: int = FONT_MAX_BYTES) -> bytes:
    out = bytearray(FONT_MAGIC + struct.pack("<III", FONT_VERSION, len(faces), 0))
    table = len(out)
    out += bytes(16 * len(faces))
    for n, face in enumerate(faces):
        codes = sorted(face.glyphs)
        index_at = len(out)
        out += bytes(12 * len(codes))
        _align(out)
        data_at = len(out)
        for k, cp in enumerate(codes):
            glyph = face.glyphs[cp]
            struct.pack_into("<HBBBbbBI", out, index_at + 12 * k, cp, glyph.advance, glyph.width, glyph.height,
                             glyph.x, glyph.y, 0, len(out) - data_at)
            out += _pack(glyph)
        _align(out)
        struct.pack_into("<IIIBBBB", out, table + 16 * n, len(codes), index_at, data_at, face.size,
                         face.ascent, face.descent, 0)
    if len(out) > limit:
        raise FontError(f"font.bin would be {len(out)} bytes; the payload holds {limit}")
    return bytes(out)


def read_font(blob: bytes) -> list[Face]:
    if blob[:4] != FONT_MAGIC or struct.unpack_from("<I", blob, 4)[0] != FONT_VERSION:
        raise FontError(f"not a version {FONT_VERSION} font.bin file")
    faces = []
    for n in range(struct.unpack_from("<I", blob, 8)[0]):
        count, index_at, data_at, size, ascent, descent, _ = struct.unpack_from("<IIIBBBB", blob, 16 + 16 * n)
        glyphs = {}
        for k in range(count):
            cp, advance, width, height, x, y, _, offset = struct.unpack_from("<HBBBbbBI", blob, index_at + 12 * k)
            glyphs[cp] = Glyph(advance, x, y, width, height, _unpack(blob[data_at + offset:], width, height))
        faces.append(Face(size, ascent, descent, glyphs))
    return faces


def game_charset(rom) -> set[int]:
    chars: set[int] = set()
    for arc in JAPANESE_ARCHIVES:
        for lines in load_archive(rom, arc):
            for line in lines:
                for segment in parse_segments(line):
                    if isinstance(segment, Literal):
                        chars.update(segment.units)
    return chars


def wanted_charset(game_chars) -> set[int]:
    wanted = set(game_chars)
    for low, high in EXTRA_RANGES:
        wanted.update(range(low, high + 1))
    wanted.update(EXTRA_CHARS)
    return {cp for cp in wanted if 0x20 <= cp <= 0xFFFF}


def dict_charset(entries, labels) -> set[int]:
    chars: set[int] = set()
    for entry in entries:
        for text in (entry.headword, entry.reading, entry.pos, *entry.senses):
            chars.update(map(ord, text))
    for text in labels:
        chars.update(map(ord, text))
    return {cp for cp in chars if 0x20 <= cp <= 0xFFFF}


def text_charset(game_chars, dict_chars) -> set[int]:
    """The 17 px face: the game's text plus every Japanese character a card can show."""
    return wanted_charset(game_chars) | {cp for cp in dict_chars if cp >= 0x3000}


def small_charset(dict_chars) -> set[int]:
    """The 13 px face: Latin text of the cards, kana for readings, a few symbols."""
    wanted = {cp for cp in dict_chars if cp < 0x3000}
    for low, high in SMALL_RANGES:
        wanted.update(range(low, high + 1))
    return wanted | set(SMALL_CHARS)


def advance_function(faces):
    """How far the payload moves the pen for a character in the text face (font.c's fallbacks)."""
    order = list(faces[:2])

    def advance(cp: int) -> int:
        for face in order:
            if cp in face.glyphs:
                return face.glyphs[cp].advance
        size = faces[0].size
        return size if cp >= 0x2E80 else size // 2

    return advance


def builtin_header(blob: bytes) -> str:
    body = ",\n".join("    " + ", ".join(f"0x{b:02x}" for b in blob[i:i + 16]) for i in range(0, len(blob), 16))
    return ("/* generated by jpreader_tools.font; do not edit */\n"
            f"static const unsigned char jpr_builtin_font[{len(blob)}] __attribute__((aligned(4))) = {{\n"
            f"{body}\n}};\n")


def font_files(cache: Path, game_chars, dict_chars) -> tuple[bytes, bytes]:
    path = fetch_noto(cache)
    text = render_face(path, *TEXT_FACE, text_charset(game_chars, dict_chars))
    small = render_face(path, *SMALL_FACE, small_charset(dict_chars))
    italic = render_face(fetch_italic(cache), *ITALIC_FACE, set(ITALIC_CHARS))
    builtin = render_face(path, *SMALL_FACE, set(BUILTIN_CHARS))
    return build_font([text, small, italic]), build_font([builtin])
