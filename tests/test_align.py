from jpreader_tools import align
from jpreader_tools.capture import ARC_MEMORY, NONE, Ident
from jpreader_tools.formats import AnnotFile, LineRecord, UnitRecord
from jpreader_tools.lines import Segment

TAG = 0x0010


def u(text: str) -> list[int]:
    return [ord(c) for c in text]


def run(text: str) -> Segment:
    return Segment(0, len(text), ord(text[0]), ord(text[-1]))


def tag(args: int = 1) -> Segment:
    return Segment(1, 3 + args, 0, 0)


def word(start, length, entry=7):
    return UnitRecord(start, length, "word", entry, None)


# stored: "やあ" {name tag: 0x0010, 2, 0x0100, 0x0000} "さん　げんき？"
RECORD = LineRecord((run("やあ"), tag(), run("さん　げんき？")), (word(0, 2), word(6, 2), word(9, 3)))


def test_plain_text_keeps_its_positions():
    record = LineRecord((run("こんにちは"),), (word(0, 5),))
    assert align.place(record, u("こんにちは")) == [align.Placed(0, 5, word(0, 5))]


def test_an_inserted_name_moves_the_units_after_it():
    placed = align.place(RECORD, u("やあ") + u("ヨウ") + u("さん　げんき？"))
    assert [(p.start, p.length) for p in placed] == [(0, 2), (4, 2), (7, 3)]


def test_a_tag_left_in_place_is_skipped():
    shown = u("やあ") + [TAG, 2, 0x0100, 0] + u("さん　げんき？")
    assert [p.start for p in align.place(RECORD, shown)] == [0, 6, 9]


def test_an_empty_insertion_is_fine():
    assert [p.start for p in align.place(RECORD, u("やあさん　げんき？"))] == [0, 2, 5]


def test_a_name_at_the_end_runs_to_the_end():
    record = LineRecord((run("きみは"), tag()), (word(0, 2),))
    assert [p.start for p in align.place(record, u("きみはハウ"))] == [0]
    assert align.place(record, u("きみ")) is None


def test_text_that_does_not_fit_gives_nothing():
    assert align.place(RECORD, u("やあヨウさん　げんき！")) is None      # last character changed
    assert align.place(RECORD, u("やあヨウさん　げん")) is None          # truncated
    assert align.place(RECORD, u("やあヨウさん　げんき？ね")) is None    # longer than the line


def test_a_scroll_tag_left_in_place_between_runs():
    record = LineRecord((run("ああ\n"), Segment(1, 3, 0, 0), run("いい")), (word(0, 2), word(6, 2)))
    shown = u("ああ\n") + [TAG, 1, 0xBE00] + u("いい")
    assert [p.start for p in align.place(record, shown)] == [0, 6]


ARCHIVES = {40: [AnnotFile(500, [RECORD]), AnnotFile(77, [None, RECORD])],
            41: [AnnotFile(500, [RECORD])]}
SHOWN = u("やあヨウさん　げんき？")


def test_lookup_by_file():
    status, placed = align.lookup(ARCHIVES, Ident(40, 0, 0, 0, 0), 0, SHOWN)
    assert status == align.OK and len(placed) == 3
    assert align.lookup(ARCHIVES, Ident(40, 1, 0, 0, 0), 0, SHOWN) == (align.NO_RECORD, [])
    assert align.lookup(ARCHIVES, Ident(40, 5, 0, 0, 0), 0, SHOWN) == (align.UNKNOWN_LINE, [])
    assert align.lookup(ARCHIVES, Ident(40, 0, NONE, 0, 0), 0, SHOWN) == (align.UNKNOWN_LINE, [])
    assert align.lookup(ARCHIVES, Ident(40, 0, 0, 0, 0), 0, u("ちがう")) == (align.MISMATCH, [])


def test_lookup_from_memory_uses_the_header_in_the_current_language():
    memory = Ident(ARC_MEMORY, NONE, 1, 77, 2)              # hdr_total 77, hdr_lines 2
    status, placed = align.lookup(ARCHIVES, memory, 0, SHOWN)
    assert status == align.OK and placed[1].start == 4
    assert align.lookup(ARCHIVES, memory, 1, SHOWN)[0] == align.UNKNOWN_LINE   # kanji: archive 41 only
    same_header = Ident(ARC_MEMORY, NONE, 0, 500, 1)
    assert align.lookup(ARCHIVES, same_header, 1, SHOWN)[0] == align.OK


def test_lookup_tries_every_file_with_the_header():
    archives = {40: [AnnotFile(9, [LineRecord((run("ちがう"),), (word(0, 3),))]), AnnotFile(9, [RECORD])]}
    status, _ = align.lookup(archives, Ident(ARC_MEMORY, NONE, 0, 9, 1), 0, SHOWN)
    assert status == align.OK


def test_a_name_holding_the_next_run_gives_nothing_rather_than_wrong_units():
    # {player}の{pokemon}は　げんき, the player called のぞみ: の could sit inside the name
    record = LineRecord((tag(), run("の"), tag(), run("は　げんき")), (UnitRecord(4, 1, "particle", 3, None),
                                                                      word(9, 3)))
    shown = u("のぞみ") + u("の") + u("モクロー") + u("は　げんき")
    assert align.place(record, shown) is None
    plain = u("ヨウ") + u("の") + u("モクロー") + u("は　げんき")   # no の in the name: one way to fit
    assert [p.start for p in align.place(record, plain)] == [2, 7]
