import hashlib
import struct
from pathlib import Path

import pytest

from jpreader_tools import font

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build" / "cache"


def g(advance=10, x=1, y=-9, width=3, height=2, levels=bytes([0, 1, 2, 3, 3, 0])):
    return font.Glyph(advance, x, y, width, height, levels)


def test_build_and_read_round_trip():
    faces = [font.Face(17, 15, 2, {0x3042: g(), 0x41: g(width=0, height=0, levels=b"")}),
             font.Face(13, 10, 3, {0x61: g(advance=7)}),
             font.Face(13, 10, 3, {0x61: g(advance=6, x=2)})]
    blob = font.build_font(faces)
    assert blob[:4] == b"JPRF" and struct.unpack_from("<III", blob, 4) == (3, 3, 0)
    assert font.read_font(blob) == faces


def test_layout_of_the_index_and_rows():
    blob = font.build_font([font.Face(17, 15, 2, {0x3042: g()})])
    count, index_at, data_at, size, ascent, descent, _ = struct.unpack_from("<IIIBBBB", blob, 16)
    assert (count, size, ascent, descent) == (1, 17, 15, 2)
    assert struct.unpack_from("<HBBBbbBI", blob, index_at) == (0x3042, 10, 3, 2, 1, -9, 0, 0)
    assert data_at % 4 == 0
    assert blob[data_at:data_at + 2] == bytes([0b00011000, 0b11110000])   # 0 1 2 / 3 3 0, a byte per row, padded


def test_build_refuses_to_outgrow_the_payload_buffer():
    big = {cp: g(width=20, height=20, levels=bytes(400)) for cp in range(0x4E00, 0x4E00 + 100)}
    with pytest.raises(font.FontError, match="bytes"):
        font.build_font([font.Face(17, 15, 2, big)], limit=1000)


def test_charsets():
    dict_chars = font.dict_charset(
        [type("E", (), {"headword": "凄い", "reading": "すごい", "pos": "i-adjective", "senses": ("café", "→")})()],
        ["-te iru"])
    assert {ord("凄"), ord("é"), ord("→"), ord("-")} <= dict_chars
    text = font.text_charset({0x6F22}, dict_chars)
    assert {0x6F22, ord("凄"), 0x3042, 0x41} <= text and ord("→") not in text
    assert {ord("é"), ord("É")} <= text                    # the peek writes English names (POKÉMON) in this face
    small = font.small_charset(dict_chars)
    assert {ord("é"), 0x41, 0x3042, 0x30A2, 0x2192, 0x2026} <= small and ord("凄") not in small


def test_builtin_header():
    header = font.builtin_header(bytes(range(20)))
    assert "static const unsigned char jpr_builtin_font[20] __attribute__((aligned(4)))" in header
    assert "0x13" in header and header.count("0x") == 20


def test_fetch_noto_checks_the_digest(tmp_path):
    bad = tmp_path / "fonts" / font.NOTO_FILE
    bad.parent.mkdir()
    bad.write_bytes(b"not a font")
    with pytest.raises(font.FontError, match="SHA-256"):
        font.fetch_noto(tmp_path)
    assert not bad.exists()


def test_fetch_italic_checks_the_digest(tmp_path):
    bad = tmp_path / "fonts" / font.ITALIC_FILE
    bad.parent.mkdir()
    bad.write_bytes(b"not a font")
    with pytest.raises(font.FontError, match="SHA-256"):
        font.fetch_italic(tmp_path)
    assert not bad.exists()


def test_italic_face_is_latin_only():
    assert {0x61, 0x41, 0x20, 0x201C, 0x201D, 0x2018, 0x2019, 0x2013, 0x2014, 0x2026} <= set(font.ITALIC_CHARS)
    assert {0xE9, 0xE8, 0xFC, 0xF1} <= set(font.ITALIC_CHARS)    # Pokémon and other Latin-1 letters
    assert not any(cp >= 0x3000 for cp in font.ITALIC_CHARS)


def test_rendered_italic_glyphs():
    italic = font.fetch_italic(CACHE)
    face = font.render_face(italic, *font.ITALIC_FACE, {0x61, 0x201C, 0x3042})
    assert face.glyphs[0x61].advance > 0 and 3 in face.glyphs[0x61].levels
    assert 0x201C in face.glyphs
    assert 0x3042 not in face.glyphs          # no kana in Noto Sans: left to the upright faces


@pytest.fixture(scope="module")
def noto():
    return font.fetch_noto(CACHE)


def test_rendered_glyphs(noto):
    face = font.render_face(noto, 17, "Medium", {0x3042, 0x6F22, 0x20, 0xE08D, 0xE000})
    a = face.glyphs[0x3042]
    assert a.advance == 17 and 12 <= a.width <= 17 and -17 <= a.y <= -11
    assert set(a.levels) <= {0, 1, 2, 3} and 3 in a.levels and 1 in a.levels   # anti-aliased
    assert face.glyphs[0x20].width == 0 and face.glyphs[0x20].advance > 0
    assert face.glyphs[0xE08D] == font.render_face(noto, 17, "Medium", {0x2026}).glyphs[0x2026]  # the game's …
    assert 0xE000 not in face.glyphs                                     # no stand-in: left to the box
    assert face.ascent == -face.glyphs[0x6F22].y


def test_advances_fall_back_like_the_payload():
    faces = [font.Face(17, 15, 2, {0x3042: g(advance=17)}), font.Face(13, 10, 3, {0x41: g(advance=8)})]
    advance = font.advance_function(faces)
    assert (advance(0x3042), advance(0x41), advance(0x4E00), advance(0xE9)) == (17, 8, 17, 8)


@pytest.mark.rom
def test_fonts_for_the_real_game(rom, rom_path):
    from jpreader_tools.annotate import cached_files
    from jpreader_tools.formats import read_dict

    chars = font.game_charset(rom)
    _annot, dict_bin, _ids = cached_files(ROOT, rom_path, rom.code_sha256())
    dict_chars = font.dict_charset(*read_dict(dict_bin))
    blob, builtin = font.font_files(CACHE, chars, dict_chars)
    assert len(blob) <= font.FONT_MAX_BYTES, len(blob)
    print("font.bin", len(blob), "bytes")
    text, small, italic = font.read_font(blob)
    assert {0x61, 0x201C, 0x2026} <= set(italic.glyphs) and not any(cp >= 0x3000 for cp in italic.glyphs)
    missing = {cp for cp in chars if cp >= 0x20 and cp not in text.glyphs and not 0xE000 <= cp <= 0xF8FF}
    assert all(0xAC00 <= cp <= 0xD7A3 for cp in missing)    # a few Hangul syllables: Noto Sans JP has none
    assert not {cp for cp in dict_chars if cp >= 0x3000 and cp not in text.glyphs and cp not in small.glyphs}
    assert sorted(font.read_font(builtin)[0].glyphs) == list(range(0x20, 0x7F))
