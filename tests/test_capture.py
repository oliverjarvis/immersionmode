import struct

import pytest

from jpreader_tools import capture


def units(s):
    return [ord(c) for c in s]


def pack_message(seq, arc, dat, line, hdr_total, hdr_lines, text, full_len=None):
    full_len = len(text) if full_len is None else full_len
    body = struct.pack("<IiIIIHHHH", seq, arc, dat, line, hdr_total, hdr_lines, 0, len(text), full_len)
    return body + struct.pack(f"<{len(text)}H", *text) + bytes(2 * (1024 - len(text)))


class FakeIndex(capture.ScriptIndex):
    def __init__(self, files, headers):
        self._files = files            # {(arc, dat): [line, ...]}
        self._headers = headers        # {(hdr_lines, hdr_total): [(arc, dat), ...]}

    def line(self, arc, dat, line):
        lines = self._files.get((arc, dat))
        return lines[line] if lines is not None and line < len(lines) else None

    def by_header(self, hdr_lines, hdr_total):
        return self._headers.get((hdr_lines, hdr_total), [])


NAME_TAG = [0x0010, 2, 0x0100, 0x0000]
INDEX = FakeIndex(
    {(41, 22): [units("こんにちは"), units("やあ ") + NAME_TAG + units("！")],
     (41, 99): [units("べつ")]},
    {(2, 300): [(41, 99), (41, 22)]})


def test_sizes_match_the_payload():
    assert capture.MSG_SIZE == 2076
    assert capture.HEADER_SIZE == 16
    assert capture.DBG_SIZE == 232
    assert (capture.DBG_PAD_MASK, capture.DBG_PAD_POLLS, capture.DBG_TOUCH) == (32, 36, 40)
    assert capture.slot_offset(1) == 16
    assert capture.slot_offset(2) == 16 + 2076
    assert capture.slot_offset(9) == 16


def test_parse_header():
    header = capture.parse_header(struct.pack("<4I", capture.CAP_MAGIC, 5, 1, 2))
    assert header == capture.CaptureHeader(capture.CAP_MAGIC, 5, 1, 2)


def test_parse_message():
    message = capture.parse_message(pack_message(7, 41, 22, 1, 300, 2, units("やあ サトシ！")))
    assert message.seq == 7
    assert message.ident == capture.Ident(41, 22, 1, 300, 2)
    assert message.text == units("やあ サトシ！")
    assert message.full_len == 7


def test_parse_message_rejects_short_data():
    with pytest.raises(ValueError):
        capture.parse_message(bytes(100))


def test_parse_debug():
    block = capture.parse_debug(struct.pack(
        "<58I", capture.DBG_MAGIC, 0, 0x0FF00000, 0, 80 << 20, 1, 2, 3, 0x10, 4, 160, 48, 2, 7, 9, 6, 429, 0x0FE90000, 1,
        0x0FE20000, 0x0FE10000, 0x08123450, 512, 1, 3, 0x14000000, 9, 2, 5, 1, 1000, 28571, 0, 0, 0x34184000,
        3, 1, 2, 1, 400, 0x3E6B851F, 0x3F800000, 0x3F800000, 0x3F800000, 0x3E6B851F, 0x08200000, 0, 2, 1, 2, 2, 0, 0x08100040, 1, 3, 4, 1, 2))
    assert (block.mix_sets, block.mix_gets, block.mix_applies, block.mix_saves, block.mix_duck) == (3, 1, 2, 1, 400)
    assert (block.mix_out0, block.mix_player0, block.mix_players) == (0x3E6B851F, 0x3E6B851F, 0x08200000)
    assert (block.voice_clip, block.voice_speed, block.voice_next, block.voice_total, block.voice_fault) == (5, 1, 1000, 28571, 0)
    assert (block.voice_ptr, block.snd_pos, block.snd_active, block.snd_fed) == (0x08123450, 512, 1, 3)
    assert (block.lin_addr, block.dsp_calls, block.voice_starts) == (0x14000000, 9, 2)
    assert block.forget_reply == 1 and block.panel_image_addr == 0x0FE20000 and block.kokoro_addr == 0x0FE10000
    assert (block.net_fault, block.net_fault_arg, block.link_result) == (6, 429, 0x0FE90000)
    assert block.sock_buf_addr == 0x0FF00000
    assert block.app_mem_total == 80 << 20
    assert block.pad_mask == 0x10 and block.pad_polls == 4
    assert (block.touch_x, block.touch_y, block.touch_polls) == (160, 48, 2)
    assert (block.touch_hidden, block.touch_passed) == (7, 9)

def test_check_known_file():
    ok, why = INDEX.check(capture.parse_message(pack_message(1, 41, 22, 1, 300, 2, units("やあ サトシ！"))))
    assert ok and "file 22" in why


def test_check_wrong_text():
    ok, why = INDEX.check(capture.parse_message(pack_message(1, 41, 22, 0, 300, 2, units("さようなら"))))
    assert not ok and "does not match" in why


def test_check_memory_built_data_uses_header():
    ok, why = INDEX.check(capture.parse_message(
        pack_message(1, capture.ARC_MEMORY, capture.NONE, 0, 300, 2, units("こんにちは"))))
    assert ok and "file 22" in why


def test_check_unknown_origin_without_header():
    ok, why = INDEX.check(capture.parse_message(
        pack_message(1, capture.ARC_UNKNOWN, capture.NONE, capture.NONE, 0, 0, units("こんにちは"))))
    assert not ok and "origin" in why


def test_check_truncated_message():
    ok, why = INDEX.check(capture.parse_message(
        pack_message(1, 41, 22, 0, 300, 2, units("こんに"), full_len=5)))
    assert not ok and "truncated" in why


def test_check_missing_line():
    ok, why = INDEX.check(capture.parse_message(pack_message(1, 41, 22, 9, 300, 2, units("こんにちは"))))
    assert not ok


def test_parse_panel():
    state = capture.parse_panel(struct.pack(
        "<30I96sI23I", capture.PANEL_MAGIC, 1, 300, 3, 1, 2, 22, capture.FONT_OK, 0, 2400, 7, 4, 0x33438480, 0x8051, 123456,
        *([0] * 15), b"", 0, *([0] * 23)))
    assert state.draw_ticks == 123456
    assert capture.ticks_to_ms(268_111_856 // 1000) == pytest.approx(1.0, rel=1e-3)
    assert state.visible == 1 and state.frames == 300
    assert (state.block_count, state.live_block, state.view_block, state.scroll) == (3, 1, 2, 22)
    assert state.font_status == capture.FONT_OK and state.font_glyphs == 2400
    assert (state.msg_seq, state.taps, state.fb_addr, state.fb_format) == (7, 4, 0x33438480, 0x8051)


@pytest.mark.rom
def test_index_from_rom(rom_path):
    index = capture.ScriptIndex(rom_path)
    stored = index.line(41, 22, 0)
    assert stored is not None and stored[:3] == units("こちら")
    assert index.line(41, 5000, 0) is None
    from jpreader_tools.rom import Rom, parse_garc
    from jpreader_tools.text import header_key

    with Rom(rom_path) as rom:
        key = header_key(parse_garc(rom.romfs_file("a/0/4/1"))[22])
    assert (41, 22) in index.by_header(*key)


def test_panel_state_has_the_word_fields():
    values = list(range(1, 31))
    state = capture.parse_panel(struct.pack("<30I96sI23I", *values, b"", 0, *([0] * 23)))
    assert (state.unit_count, state.annot_status, state.selected) == (16, 17, 18)
    assert (state.card_entry, state.card_status, state.data_status, state.data_error) == (21, 22, 23, 24)


def test_panel_state_has_the_grammar_fields():
    words = list(range(1, 31))
    message = b"Timed out" + bytes(87)
    state = capture.parse_panel(struct.pack("<30I96sI23I", *words, message, 0, *([0] * 23)))
    assert (state.gr_open, state.gr_state, state.gr_failure, state.gr_http) == (25, 26, 27, 28)
    assert (state.gr_requests, state.gr_scroll, state.gr_message) == (29, 30, "Timed out")


def test_link_result():
    text = "\"Wait.\"".encode()
    data = struct.pack("<5I", 3, 0, 0, 200, len(text)) + text + bytes(10)
    assert capture.parse_link_result(data) == (3, 0, 0, 200, "\"Wait.\"")


def test_panel_state_ends_with_the_card_scroll():
    state = capture.parse_panel(struct.pack("<30I96sI23I", *range(1, 31), b"", 57, *([0] * 23)))
    assert capture.PANEL_SIZE == 30 * 4 + 96 + 4 + 23 * 4
    assert state.card_scroll == 57


def test_panel_state_has_kokoro_fields():
    state = capture.parse_panel(struct.pack("<30I96sI23I", *range(1, 31), b"", 57, *range(60, 83)))
    assert (state.kokoro_on, state.marks_known, state.marks_learning, state.card_kokoro, state.add_state) == (60, 61, 62, 63, 64)
    assert (state.kokoro_words, state.kokoro_syncs, state.outbox, state.kokoro_failure, state.kokoro_http) == (65, 66, 67, 68, 69)
    assert (state.voice_available, state.voice_requests) == (70, 71)
    assert (state.mix_open, state.mix_music, state.mix_effects, state.mix_voice, state.mix_duck) == (72, 73, 74, 75, 76)
    assert state.peek_open == 77
    assert (state.gr_points, state.gr_other, state.gr_buttons, state.gr_button_y, state.gr_add_state) == (78, 79, 80, 81, 82)
