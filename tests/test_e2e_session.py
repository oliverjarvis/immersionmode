import struct
from types import SimpleNamespace

import pytest

from jpreader_tools import capture, e2e, emu, formats
from jpreader_tools.dictionary import CardEntry
from jpreader_tools.align import Placed
from jpreader_tools.formats import UnitRecord


class MemoryRpc:
    """Stands in for emu.Rpc over a dict of address -> bytearray blocks."""

    def __init__(self, blocks):
        self.blocks = blocks
        self.writes = []

    def _find(self, addr):
        for base, block in self.blocks.items():
            if base <= addr < base + len(block):
                return base, block
        raise AssertionError(f"unexpected address {addr:#x}")

    def read(self, addr, size):
        base, block = self._find(addr)
        return bytes(block[addr - base:addr - base + size])

    def write(self, addr, data):
        base, block = self._find(addr)
        block[addr - base:addr - base + len(data)] = data
        self.writes.append((addr, bytes(data)))


CAP = 0x6E0000
DBG = 0x6F0000


def make_rpc():
    cap = bytearray(capture.HEADER_SIZE + capture.RING * capture.MSG_SIZE)
    struct.pack_into("<4I", cap, 0, capture.CAP_MAGIC, 0, 0, 0)
    dbg = bytearray(struct.pack("<58I", capture.DBG_MAGIC, 0, 0x0FF00000, 0, 80 << 20, 100, 200, 5, *([0] * 50)))
    return MemoryRpc({CAP: cap, DBG: dbg, 0x66FA10: bytearray(b"\x01")})


def put_message(rpc, seq, text):
    block = rpc.blocks[CAP]
    head = struct.pack("<IiIIIHHHH", seq, 41, 22, 0, 300, 2, 0, len(text), len(text))
    offset = capture.slot_offset(seq)
    block[offset:offset + len(head)] = head
    struct.pack_into(f"<{len(text)}H", block, offset + len(head), *text)
    struct.pack_into("<I", block, 4, seq)


def test_session_reads_blocks():
    session = e2e.Session(make_rpc(), {"jpr_cap": CAP, "jpr_dbg": DBG})
    assert session.debug().sock_buf_addr == 0x0FF00000
    assert session.header().seq == 0
    assert session.language() == 1


def test_session_reads_a_message_and_notices_overwrites():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    put_message(rpc, 1, [0x3042, 0x3044])
    assert session.message(1).text == [0x3042, 0x3044]
    put_message(rpc, 9, [0x3046])            # same slot as message 1
    assert session.message(1) is None
    assert session.message(9).text == [0x3046]


def test_press_writes_mask_and_poll_count_together():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    session.press("A", polls=6, settle=0)
    assert rpc.writes == [(DBG + capture.DBG_PAD_MASK, struct.pack("<II", e2e.BUTTONS["A"], 6))]


def test_button_bits_match_the_game():
    assert e2e.BUTTONS == {
        "LEFT": 0x1, "RIGHT": 0x2, "UP": 0x4, "DOWN": 0x8, "A": 0x10, "B": 0x20,
        "X": 0x40, "Y": 0x80, "L": 0x100, "R": 0x200, "START": 0x1000,
        "STICK_LEFT": 0x10000, "STICK_RIGHT": 0x20000, "STICK_UP": 0x40000, "STICK_DOWN": 0x80000,
    }


def test_tap_writes_position_and_polls_together():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    session.tap(160, 48, polls=4, settle=0)
    assert rpc.writes == [(DBG + capture.DBG_TOUCH, struct.pack("<III", 160, 48, 4))]


def test_results_document_lists_the_figures():
    debug = capture.parse_debug(struct.pack("<58I", capture.DBG_MAGIC, 0, 0x0FF00000, 0, 80 << 20, 7 << 20, 60 << 20, 9, *([0] * 50)))
    text = e2e.results_document(
        passed=True, debug_at_start=debug, peak_used=61 << 20, language=1, validated=25, failures=1,
        ends=3, soak_seconds=600, origins={"archive 41": 20, "memory": 5}, log=["#1 OK ..."])
    assert "| Application memory | 80.0 MB |" in text
    assert "| Peak used during the run | 61.0 MB |" in text
    assert "| Lines validated | 25 |" in text
    assert "archive 41: 20" in text
    assert "PASSED" in text


def test_name_prompt_is_recognised_in_kana_and_kanji():
    assert e2e.is_name_prompt([ord(c) for c in "なまえの　とうろくも\nきみに　おまかせ　するよ！"])
    assert e2e.is_name_prompt([ord(c) for c in "名前の　登録も\nきみに　おまかせ　するよ！"])
    assert not e2e.is_name_prompt([ord(c) for c in "いきし　で　いいかい？"])


def test_name_entry_types_three_different_characters_then_confirms():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    e2e.enter_name(session, wait=0)
    masks = [struct.unpack("<II", data)[0] for _addr, data in rpc.writes]
    names = {bit: name for name, bit in e2e.BUTTONS.items()}
    assert [names[m] for m in masks] == ["A", "DOWN", "A", "RIGHT", "A", "RIGHT", "A", "START", "A"]


PANEL = 0x6F1000


def test_session_reads_the_panel():
    rpc = make_rpc()
    rpc.blocks[PANEL] = bytearray(struct.pack(capture.PANEL_FORMAT, capture.PANEL_MAGIC, 1, 5, 2, 0, 1, 0, 1, 0, 2400,
                                                 3, 1, 0, 0x8051, 0, *([0] * 15), b"", 0, *([0] * 23)))
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG, "jpr_panel": PANEL})
    assert session.panel().view_block == 1


def test_parse_key():
    assert e2e.parse_key("A") == ("press", "A", 1)
    assert e2e.parse_key("STICK_UP*4") == ("press", "STICK_UP", 4)
    assert e2e.parse_key("TAP:160,48") == ("tap", 160, 48)
    assert e2e.parse_key("WAIT:1.5") == ("wait", 1.5)
    with pytest.raises(ValueError):
        e2e.parse_key("JUMP")


def test_wait_for():
    calls = []
    assert e2e.wait_for(lambda: calls.append(1) or len(calls) >= 3, timeout=1, interval=0)
    assert not e2e.wait_for(lambda: False, timeout=0.05, interval=0.01)


def test_stage2_document():
    text = e2e.stage2_document(
        mode="kana", passed=False,
        checks={"font loaded": (True, "2400 glyphs"), "panel goes away": (False, "visible 1")},
        log=["#1 shown"], images=[("Dialog open", "images/stage2-kana-dialog.png")])
    assert text.startswith("# Stage 2 results (kana)")
    assert "Result: **FAILED**" in text
    assert "| font loaded | pass | 2400 glyphs |" in text
    assert "| panel goes away | FAIL | visible 1 |" in text
    assert "![Dialog open](images/stage2-kana-dialog.png)" in text

def adv(cp):
    return 17 if cp >= 0x3000 else 8


def test_first_line_skips_tags_and_leading_breaks():
    text = [0x000A, 0x0010, 1, 0xBE00] + [0x3042] * 3 + [0x000A, 0x3044]
    assert e2e.first_line(text, adv) == [(4, 0, 17), (5, 17, 17), (6, 34, 17)]


def test_first_line_wraps_like_the_panel():
    text = [0x3042] * 20
    line = e2e.first_line(text, adv)
    assert len(line) == 18 and line[-1][1] == 17 * 17           # 18 * 17 = 306 fits in 308


def test_first_line_stops_at_a_clear():
    text = [0x3042, 0x0010, 1, 0xBE01, 0x000A, 0x3044]
    assert e2e.first_line(text, adv) == [(0, 0, 17)]


def test_tap_target_picks_a_unit_with_an_entry_on_the_first_line():
    text = [0x3042, 0x3044, 0x3046, 0x000A, 0x3048]
    placed = [Placed(0, 1, UnitRecord(0, 1, "particle", None, None)),
              Placed(1, 2, UnitRecord(1, 2, "word", 4, None)),
              Placed(4, 1, UnitRecord(4, 1, "word", 5, None))]
    assert e2e.tap_target(text, placed, adv) == (1, e2e.PANEL_TEXT_X + 17 + 8, e2e.PANEL_TEXT_TOP + 12)
    assert e2e.tap_target(text, placed[:1], adv) is None


def test_tap_target_prefers_a_word_further_along_the_line():
    text = [0x3042, 0x3044, 0x3046, 0x3048]
    placed = [Placed(0, 2, UnitRecord(0, 2, "word", 4, None)), Placed(2, 2, UnitRecord(2, 2, "word", 5, None))]
    assert e2e.tap_target(text, placed, adv) == (1, e2e.PANEL_TEXT_X + 34 + 8, e2e.PANEL_TEXT_TOP + 12)


def test_rows_match_the_spec_table():
    messages = [row.message for row in e2e.ROWS]
    assert messages == [
        "Grammar needs kokoro: run jpreader kokoro-link",
        "No Wi-Fi connection",
        "Network is busy (the game is online)",
        "Could not reach kokoro (code 4",
        "Could not verify kokoro (check the console's date)",
        "Token rejected: run jpreader kokoro-link",
        "kokoro is busy, try again soon",
        "kokoro could not explain that, try again",
        "Timed out",
        "Could not reach kokoro (code 5900)",
    ]


def test_write_config(tmp_path):
    path = e2e.write_config(tmp_path, kokoro=("https://localhost:1", "kkr_x"), debug=True)
    assert path.read_text() == "debug=1\nkokoro_url=https://localhost:1\nkokoro_token=kkr_x\n"
    assert e2e.write_config(tmp_path).read_text() == "debug=0\n"
    assert e2e.write_config(tmp_path, voice=False).read_text() == "debug=0\nvoice=0\n"


def test_session_fault():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    session.fault(e2e.FAULT_STATUS, 429)
    assert struct.unpack_from("<II", rpc.blocks[DBG], capture.DBG_FAULT) == (6, 429)


def test_session_forget_reply():
    rpc = make_rpc()
    session = e2e.Session(rpc, {"jpr_cap": CAP, "jpr_dbg": DBG})
    session.forget_reply()
    assert capture.DBG_FORGET_REPLY == 72
    assert struct.unpack_from("<I", rpc.blocks[DBG], capture.DBG_FORGET_REPLY) == (1,)


from jpreader_tools.dictionary import CardEntry


def test_standin_ids_split_by_id():
    entries = [CardEntry("dictionary", f"w{k}", "", "noun", ("x",), 1000000 + k) for k in range(60)]
    entries.append(CardEntry("name", "名前", "", "name", ("Name",)))
    ids = e2e.standin_ids(entries, set())
    for word, state in ids.items():
        assert state == ("known" if word % 3 == 0 else "learning") and word % 3 != 2
    assert ids and all(word >= 1000000 for word in ids)
    assert 1000000 not in e2e.standin_ids(entries, {1000000})     # a word kokoro holds by spelling is left to that


def test_standin_fallback_takes_a_kanji_spelling_only_one_word_has():
    from jpreader_tools.jmdict import Entry
    def word(i, kanji, kana):
        return Entry(str(i), tuple(kanji), tuple(kana), False, True, (), (), frozenset(), frozenset())
    words = [word(1006970, ["其れ", "其"], ["それ"]), word(2, ["方"], ["かた"]), word(3, ["方"], ["ほう"])]
    entries = [CardEntry("dictionary", "それ", "それ", "pronoun", ("that",), 1006970),
               CardEntry("dictionary", "かた", "かた", "noun", ("way",), 2),
               CardEntry("dictionary", "ほう", "ほう", "noun", ("direction",), 3)]
    fallback, used = e2e.standin_fallback(entries, words)
    assert fallback == {"其れ": "known"} and used == {1006970}       # 方 names two words: never a fallback


def test_expected_marks_count_dictionary_units():
    entries = [CardEntry("dictionary", "猫", "", "noun", ("cat",), 1467640),
               CardEntry("particle", "は", "", "particle", ("t",)),
               CardEntry("dictionary", "犬", "", "noun", ("dog",), 1250050),
               CardEntry("dictionary", "それ", "", "pronoun", ("that",), 1006970)]
    unit = lambda e: SimpleNamespace(unit=SimpleNamespace(entry=e))
    placed = [unit(0), unit(1), unit(2), unit(None), unit(0), unit(3)]
    assert e2e.expected_marks(placed, entries, {1467640: "known", 1250050: "learning"}, {1006970}) == (3, 1)




def test_a_run_starts_without_the_real_kokoro(tmp_path):
    mod = tmp_path / "mod"
    (mod / "sdmc" / "jpreader").mkdir(parents=True)
    (mod / "sdmc" / "jpreader" / "config.txt").write_text(
        "debug=0\nkokoro_url=https://real.convex.site\nkokoro_token=kkr_real\n")   # what build() writes from .env
    profile = tmp_path / "profile"
    e2e.install(profile, mod)
    config = emu.profile_dirs(profile)["data"] / "sdmc" / "jpreader" / "config.txt"
    assert config.read_text() == "debug=0\n"
