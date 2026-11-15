import struct

import pytest

from jpreader_tools import text


def units(s: str) -> list[int]:
    return [ord(c) for c in s]


def make_text_file(lines: list[list[int]]) -> bytes:
    table = b""
    data = b""
    key = text.KEY_BASE
    base = 4 + 8 * len(lines)
    for line in lines:
        k = key
        encrypted = []
        for unit in line:
            encrypted.append(unit ^ k)
            k = ((k << 3) | (k >> 13)) & 0xFFFF
        key = (key + text.KEY_ADVANCE) & 0xFFFF
        table += struct.pack("<iHH", base + len(data), len(line), 0)
        data += struct.pack(f"<{len(encrypted)}H", *encrypted)
        data += b"\0" * (-len(data) % 4)
    section = struct.pack("<I", 4 + len(table) + len(data)) + table + data
    return struct.pack("<HHIII", 1, len(lines), len(section), 0, 0x10) + section


NAME_TAG = [0x0010, 2, 0x0100, 0x0000]       # player name
CLEAR_TAG = [0x0010, 1, 0xBE01]
NUMBER_TAG = [0x0010, 3, 0x0204, 0x0000, 0x0000]


def test_decode_strips_terminator_and_padding():
    blob = make_text_file([units("こんにちは") + [0, 0, 0], units("AB") + [0]])
    assert text.decode_text_file(blob) == [units("こんにちは"), units("AB")]


def test_decode_keeps_zero_arguments_of_a_final_tag():
    line = units("No. ") + NUMBER_TAG
    assert text.decode_text_file(make_text_file([line + [0, 0]])) == [line]


def test_decode_rejects_unknown_header():
    with pytest.raises(text.TextFormatError):
        text.decode_text_file(struct.pack("<HHIII", 2, 0, 4, 0, 0x10) + bytes(8))


def test_header_key():
    blob = make_text_file([units("AB") + [0]])
    assert text.header_key(blob) == (1, len(blob) - 0x10)


def test_parse_segments_splits_literals_and_tags():
    segments = text.parse_segments(units("やあ ") + NAME_TAG + units("！"))
    assert segments == [
        text.Literal(tuple(units("やあ "))),
        text.Tag(0x0100, (0x0000,)),
        text.Literal(tuple(units("！"))),
    ]


def test_parse_segments_rejects_truncated_tag():
    with pytest.raises(text.TextFormatError):
        text.parse_segments([0x0010, 5, 0x0100])


def test_render_shows_tags():
    line = units("やあ ") + NAME_TAG + CLEAR_TAG + [0xE08D]
    assert text.render(line) == "やあ {0100:0000}{c}…"


def test_matches_displayed_with_inserted_name():
    stored = units("やあ ") + NAME_TAG + units("！")
    assert text.matches_displayed(stored, units("やあ サトシ！"))


def test_matches_displayed_with_empty_name():
    stored = units("やあ ") + NAME_TAG + units("！")
    assert text.matches_displayed(stored, units("やあ ！"))


def test_matches_displayed_rejects_different_literal_text():
    stored = units("やあ ") + NAME_TAG + units("！")
    assert not text.matches_displayed(stored, units("やあ サトシ？"))
    assert not text.matches_displayed(stored, units("よう サトシ！"))


def test_matches_displayed_allows_control_tags_left_in_place():
    stored = units("一") + CLEAR_TAG + units("二")
    assert text.matches_displayed(stored, units("一") + CLEAR_TAG + units("二"))


def test_matches_displayed_treats_text_literally():
    stored = units("(.*)？")
    assert text.matches_displayed(stored, units("(.*)？"))
    assert not text.matches_displayed(stored, units("あいう？"))


def test_archive_path():
    assert text.archive_path(41) == "a/0/4/1"
    assert text.archive_path(30) == "a/0/3/0"


CLEAR = [0x0010, 1, 0xBE01]
SCROLL = [0x0010, 1, 0xBE00]


def test_split_blocks_at_clear_tags():
    line = units("こちら\nコーナー！") + CLEAR + units("\nひいた\nくじ") + SCROLL + units("\nどう？")
    assert text.split_blocks(line) == [units("こちら\nコーナー！"), units("ひいた\nくじ\nどう？")]


def test_split_blocks_drops_blank_edges_and_an_empty_last_block():
    assert text.split_blocks(units("\n\nあ\n") + CLEAR) == [units("あ")]
    assert text.split_blocks([]) == [[]]


def test_split_blocks_keeps_inserted_name_tags_invisible():
    assert text.split_blocks(units("やあ ") + NAME_TAG + units("！")) == [units("やあ ！")]


@pytest.mark.rom
def test_story_archive_from_rom(rom):
    kanji = text.load_archive(rom, 41)
    assert len(kanji) == 1124
    assert sum(len(lines) for lines in kanji) == 16161
    assert text.render(kanji[22][0]).startswith("こちら　ＩＤくじセンター\n抽選　コーナー！{c}")
