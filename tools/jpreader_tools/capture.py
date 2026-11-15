"""Read the payload's memory blocks and check captured dialog against the script."""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from .rom import Rom, parse_garc
from .text import archive_path, decode_text_file, header_key, matches_displayed

CAP_MAGIC = 0x4352504A      # "JPRC"
DBG_MAGIC = 0x4452504A      # "JPRD"
HEADER_SIZE = 16
MSG_MAX = 1024
MSG_HEAD = "<IiIIIHHHH"     # seq, arc, dat, line, hdr_total, hdr_lines, reserved, len, full_len
MSG_SIZE = struct.calcsize(MSG_HEAD) + 2 * MSG_MAX
RING = 8
DBG_FORMAT = "<58I"
DBG_SIZE = struct.calcsize(DBG_FORMAT)
DBG_PAD_MASK = 32           # offset of pad_mask; pad_polls follows it
DBG_PAD_POLLS = 36
DBG_TOUCH = 40              # touch_x, touch_y, touch_polls
DBG_FAULT = 60              # net_fault, net_fault_arg
DBG_FORGET_REPLY = 72       # written by the driver: the panel forgets its stored reply
DBG_VOICE_TEST = 132        # written by the driver: messages stand in for story file 27's lines
DBG_MIX_TEST = 46 * 4      # u32 mix_test: written by the driver (debug builds)
DBG_PEEK_TOGGLE = 51 * 4   # u32 peek_toggle: written by the driver (debug builds): a SELECT press
ARC_MEMORY = -1
ARC_UNKNOWN = -2
NONE = 0xFFFFFFFF
HEADER_ARCHIVES = (30, 31, 40, 41, 32, 42)   # Japanese kana and kanji, then English


@dataclass(frozen=True)
class CaptureHeader:
    magic: int
    seq: int
    open: int
    end_count: int


@dataclass(frozen=True)
class Ident:
    arc: int
    dat: int
    line: int
    hdr_total: int
    hdr_lines: int


@dataclass(frozen=True)
class Message:
    seq: int
    ident: Ident
    text: list[int]
    full_len: int


@dataclass(frozen=True)
class DebugBlock:
    magic: int
    protect_result: int
    sock_buf_addr: int
    sock_buf_result: int
    app_mem_total: int
    app_mem_used_init: int
    app_mem_used_now: int
    polls: int
    pad_mask: int
    pad_polls: int
    touch_x: int
    touch_y: int
    touch_polls: int
    touch_hidden: int
    touch_passed: int
    net_fault: int
    net_fault_arg: int
    link_result: int
    forget_reply: int
    panel_image_addr: int
    kokoro_addr: int
    voice_ptr: int
    snd_pos: int
    snd_active: int
    snd_fed: int
    lin_addr: int
    dsp_calls: int
    voice_starts: int
    voice_clip: int
    voice_speed: int
    voice_next: int
    voice_total: int
    voice_fault: int
    voice_test: int
    voice_ring: int
    mix_sets: int
    mix_gets: int
    mix_applies: int
    mix_saves: int
    mix_duck: int
    mix_out0: int
    mix_out2: int
    mix_shadow0: int
    mix_voice: int
    mix_player0: int
    mix_players: int
    mix_test: int
    menus_shown: int
    menu_showing: int
    menu_count: int
    menu_found: int
    peek_toggle: int
    hid_addr: int
    select_presses: int
    screen_msgs: int
    screen_texts: int
    screen_matched: int
    peek_composes: int


PANEL_MAGIC = 0x5052504A    # "JPRP"
PANEL_FORMAT = "<30I96sI23I"  # 30 words, the grammar message, the card scroll, then kokoro, voice, the mixer, the peek
                              # and the grammar points (stage 17)
PANEL_SIZE = struct.calcsize(PANEL_FORMAT)
FONT_OK = 1
CARD_OK = 1
DATA_OK = 1
SYSTEM_TICK_HZ = 268_111_856   # the 3DS's CPU tick counter


@dataclass(frozen=True)
class PanelState:
    magic: int
    visible: int
    frames: int
    block_count: int
    live_block: int
    view_block: int
    scroll: int
    font_status: int
    font_error: int
    font_glyphs: int
    msg_seq: int
    taps: int
    fb_addr: int
    fb_format: int
    draw_ticks: int
    unit_count: int
    annot_status: int
    selected: int
    sel_start: int
    sel_len: int
    card_entry: int
    card_status: int
    data_status: int
    data_error: int
    gr_open: int
    gr_state: int
    gr_failure: int
    gr_http: int
    gr_requests: int
    gr_scroll: int
    gr_message: str
    card_scroll: int
    kokoro_on: int
    marks_known: int
    marks_learning: int
    card_kokoro: int
    add_state: int
    kokoro_words: int
    kokoro_syncs: int
    outbox: int
    kokoro_failure: int
    kokoro_http: int
    voice_available: int
    voice_requests: int
    mix_open: int
    mix_music: int
    mix_effects: int
    mix_voice: int
    mix_duck: int
    peek_open: int
    gr_points: int
    gr_other: int
    gr_buttons: int
    gr_button_y: int
    gr_add_state: int


def ticks_to_ms(ticks: int) -> float:
    return ticks * 1000 / SYSTEM_TICK_HZ


def parse_panel(data: bytes) -> PanelState:
    *words, message, card_scroll = struct.unpack_from(PANEL_FORMAT, data)[:32]
    kokoro = struct.unpack_from(PANEL_FORMAT, data)[32:]
    return PanelState(*words, message.split(b"\0", 1)[0].decode("utf-8", "replace"), card_scroll, *kokoro)


LINK_RESULT_SIZE = 20 + 4504  # struct jpr_grammar_result: five words and the text (grammar.h JPR_REPLY_MAX)


def parse_link_result(data: bytes) -> tuple[int, int, int, int, str]:
    """The worker's last result: id, failure, code, HTTP status, and the reply or kokoro's message."""
    ident, failure, code, status, length = struct.unpack_from("<5I", data)
    return ident, failure, code, status, data[20:20 + min(length, len(data) - 20)].decode("utf-8", "replace")


def slot_offset(seq: int) -> int:
    return HEADER_SIZE + ((seq - 1) & (RING - 1)) * MSG_SIZE


def parse_header(data: bytes) -> CaptureHeader:
    return CaptureHeader(*struct.unpack_from("<4I", data))


def parse_message(data: bytes) -> Message:
    if len(data) < MSG_SIZE:
        raise ValueError(f"a message slot is {MSG_SIZE} bytes, got {len(data)}")
    seq, arc, dat, line, hdr_total, hdr_lines, _reserved, length, full_len = struct.unpack_from(MSG_HEAD, data)
    length = min(length, MSG_MAX)
    text = list(struct.unpack_from(f"<{length}H", data, struct.calcsize(MSG_HEAD)))
    return Message(seq, Ident(arc, dat, line, hdr_total, hdr_lines), text, full_len)


def parse_debug(data: bytes) -> DebugBlock:
    return DebugBlock(*struct.unpack_from(DBG_FORMAT, data))


class ScriptIndex:
    """The game's stored text, loaded from the ROM one archive at a time."""

    def __init__(self, rom_path: Path):
        self._rom_path = rom_path
        self._archives: dict[int, list[list[list[int]]] | None] = {}
        self._header_index: dict[tuple[int, int], list[tuple[int, int]]] | None = None

    def _archive(self, arc: int):
        if arc not in self._archives:
            try:
                with Rom(self._rom_path) as rom:
                    blobs = parse_garc(rom.romfs_file(archive_path(arc)))
                self._archives[arc] = [decode_text_file(blob) for blob in blobs]
            except (FileNotFoundError, ValueError):
                self._archives[arc] = None
        return self._archives[arc]

    def line(self, arc: int, dat: int, line: int) -> list[int] | None:
        files = self._archive(arc) if 0 <= arc < 1000 else None
        if files is None or dat >= len(files) or line >= len(files[dat]):
            return None
        return files[dat][line]

    def by_header(self, hdr_lines: int, hdr_total: int) -> list[tuple[int, int]]:
        if self._header_index is None:
            index: dict[tuple[int, int], list[tuple[int, int]]] = {}
            with Rom(self._rom_path) as rom:
                for arc in HEADER_ARCHIVES:
                    for dat, blob in enumerate(parse_garc(rom.romfs_file(archive_path(arc)))):
                        index.setdefault(header_key(blob), []).append((arc, dat))
            self._header_index = index
        return self._header_index.get((hdr_lines, hdr_total), [])

    def check(self, message: Message) -> tuple[bool, str]:
        ident = message.ident
        if message.full_len > len(message.text):
            return False, f"truncated: the game had {message.full_len} units, {len(message.text)} were kept"
        if ident.line == NONE:
            return False, "unknown origin: no line number was recorded"
        if ident.arc >= 0 and ident.dat != NONE:
            candidates = [(ident.arc, ident.dat)]
        elif ident.hdr_lines or ident.hdr_total:
            candidates = self.by_header(ident.hdr_lines, ident.hdr_total)
        else:
            return False, "unknown origin: neither a file nor a header was recorded"
        for arc, dat in candidates:
            stored = self.line(arc, dat, ident.line)
            if stored is not None and matches_displayed(stored, message.text):
                return True, f"archive {arc} file {dat} line {ident.line}"
        return False, f"text does not match line {ident.line} of {candidates[:4] or 'any known file'}"
