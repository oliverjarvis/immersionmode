"""Decode Gen 7 game text files and compare stored lines with displayed text."""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

KEY_BASE = 0x7C89
KEY_ADVANCE = 0x2983
TAG_MARK = 0x0010
TAG_SCROLL = 0xBE00   # wait for a button, then scroll one line
TAG_CLEAR = 0xBE01    # wait for a button, then clear the box
SPECIAL_CHARS = {0xE07F: " ", 0xE08D: "…", 0xE08E: "♂", 0xE08F: "♀"}


class TextFormatError(Exception):
    pass


@dataclass(frozen=True)
class Literal:
    units: tuple[int, ...]


@dataclass(frozen=True)
class Tag:
    code: int
    args: tuple[int, ...]


def _trim(units: list[int]) -> list[int]:
    """Cut at the first terminator that is not inside a tag."""
    i = 0
    n = len(units)
    while i < n and units[i] != 0:
        if units[i] == TAG_MARK and i + 1 < n:
            i += 2 + units[i + 1]
        else:
            i += 1
    return units[:min(i, n)]


def header_key(blob: bytes) -> tuple[int, int]:
    """(line count, total length) as the game's message object sees them."""
    _sections, line_count, total_len = struct.unpack_from("<HHI", blob, 0)
    return line_count, total_len


def decode_text_file(blob: bytes) -> list[list[int]]:
    """Return every line as UTF-16 code units, terminator and padding removed."""
    if len(blob) < 0x10:
        raise TextFormatError("text file is shorter than its header")
    sections, line_count, _total, initial_key, section_off = struct.unpack_from("<HHIII", blob, 0)
    if sections != 1 or initial_key != 0 or section_off != 0x10:
        raise TextFormatError("unsupported text file header")
    lines = []
    key = KEY_BASE
    for i in range(line_count):
        offset, length, _unused = struct.unpack_from("<iHH", blob, section_off + 4 + i * 8)
        raw = struct.unpack_from(f"<{length}H", blob, section_off + offset)
        k = key
        line = []
        for unit in raw:
            line.append(unit ^ k)
            k = ((k << 3) | (k >> 13)) & 0xFFFF
        key = (key + KEY_ADVANCE) & 0xFFFF
        lines.append(_trim(line))
    return lines


def parse_segments(units) -> list[Literal | Tag]:
    segments: list[Literal | Tag] = []
    run: list[int] = []
    i = 0
    n = len(units)
    while i < n:
        if units[i] != TAG_MARK:
            run.append(units[i])
            i += 1
            continue
        if i + 1 >= n or units[i + 1] < 1 or i + 2 + units[i + 1] > n:
            raise TextFormatError("tag runs past the end of the line")
        if run:
            segments.append(Literal(tuple(run)))
            run = []
        count = units[i + 1]
        segments.append(Tag(units[i + 2], tuple(units[i + 3:i + 2 + count])))
        i += 2 + count
    if run:
        segments.append(Literal(tuple(run)))
    return segments


def render(units) -> str:
    """Human-readable form; tags become {...}."""
    parts = []
    for segment in parse_segments(units):
        if isinstance(segment, Literal):
            parts.append("".join(SPECIAL_CHARS.get(u, chr(u)) for u in segment.units))
        elif segment.code == TAG_SCROLL:
            parts.append("{r}")
        elif segment.code == TAG_CLEAR:
            parts.append("{c}")
        else:
            parts.append("{%04X%s}" % (segment.code, "".join(":%04X" % a for a in segment.args)))
    return "".join(parts)


def matches_displayed(stored, displayed) -> bool:
    """True when `displayed` could be `stored` after the game replaced its tags.

    Literal text must appear unchanged and in order. Anything may stand where a
    tag was: an inserted name, a number, or the tag itself left in place.
    """
    pattern = "".join(
        re.escape("".join(map(chr, segment.units))) if isinstance(segment, Literal) else ".*?"
        for segment in parse_segments(stored))
    return re.fullmatch(pattern, "".join(map(chr, displayed)), re.DOTALL) is not None


def _strip_line_breaks(block: list[int]) -> list[int]:
    start, end = 0, len(block)
    while start < end and block[start] == 0x000A:
        start += 1
    while end > start and block[end - 1] == 0x000A:
        end -= 1
    return block[start:end]


def split_blocks(units) -> list[list[int]]:
    """Visible units of each block the top-screen box shows: the payload's rule, without wrapping."""
    blocks: list[list[int]] = [[]]
    drop_line_break = False
    for segment in parse_segments(units):
        if isinstance(segment, Tag):
            if segment.code == TAG_CLEAR:
                blocks.append([])
                drop_line_break = True
            continue
        for unit in segment.units:
            if unit == 0x000A and drop_line_break:
                drop_line_break = False
                continue
            drop_line_break = False
            blocks[-1].append(unit)
    blocks = [_strip_line_breaks(block) for block in blocks]
    if len(blocks) > 1 and not blocks[-1]:
        blocks.pop()
    return blocks


def archive_path(arc_id: int) -> str:
    return f"a/{arc_id // 100}/{arc_id // 10 % 10}/{arc_id % 10}"


def load_archive(rom, arc_id: int) -> list[list[list[int]]]:
    from .rom import parse_garc

    return [decode_text_file(blob) for blob in parse_garc(rom.romfs_file(archive_path(arc_id)))]


def load_archive_with_totals(rom, arc_id: int) -> list[tuple[int, list[list[int]]]]:
    """Each file's header total (spec 6.3: how memory-built messages are found) and its lines."""
    from .rom import parse_garc

    return [(header_key(blob)[1], decode_text_file(blob)) for blob in parse_garc(rom.romfs_file(archive_path(arc_id)))]
