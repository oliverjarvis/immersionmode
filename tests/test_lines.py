import pytest

from jpreader_tools.analyze import Analyzer
from jpreader_tools.lines import Segment, line_units, segments_of, text_runs

NAME = [0x0010, 2, 0x0100, 0x0000]           # player name (4 units)
CLEAR = [0x0010, 1, 0xBE01]                  # wait and clear (3 units)
COLOUR = [0x0010, 2, 0xFF00, 0x0001]         # colour (4 units)


def units(s):
    return [ord(c) for c in s]


@pytest.fixture(scope="module")
def analyzer():
    return Analyzer()


STORED = units("やあ　") + NAME + units("！") + CLEAR + units("\nポケモンを") + COLOUR + units("あずけている")


def test_segments_of():
    assert segments_of(STORED) == [
        Segment(0, 3, ord("や"), 0x3000),
        Segment(1, 4, 0, 0),
        Segment(0, 1, ord("！"), ord("！")),
        Segment(1, 3, 0, 0),
        Segment(0, 6, 0x000A, ord("を")),
        Segment(1, 4, 0, 0),
        Segment(0, 6, ord("あ"), ord("る")),
    ]


def test_text_runs():
    assert text_runs(STORED) == [(0, "やあ　"), (7, "！"), (11, "\nポケモンを"), (21, "あずけている")]


def test_positions_skip_tags(analyzer):
    found = {u.surface: (u.start, u.end) for u in line_units(analyzer, STORED)}
    assert found["やあ"] == (0, 2)
    assert found["ポケモン"] == (12, 16)
    assert found["を"] == (16, 17)
    assert found["あずけている"] == (21, 27)
    for surface, (start, end) in found.items():
        assert "".join(map(chr, STORED[start:end])) == surface


def test_honorific_after_a_name_tag_is_a_suffix(analyzer):
    stored = NAME + units("さん／しまめぐりに")
    first = line_units(analyzer, stored)[0]
    assert (first.surface, first.pos, first.normalized) == ("さん", "suffix", "さん")
