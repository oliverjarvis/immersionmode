"""A stored line: its shape (text runs and tags) and its units at stored positions."""
from __future__ import annotations

from dataclasses import dataclass, replace

from .text import Literal, parse_segments

# Honorifics right after a tag follow an inserted name ("{name}さん"), so they are suffixes.
HONORIFICS = {"さん", "くん", "ちゃん", "さま", "様", "君"}


@dataclass(frozen=True)
class Segment:
    kind: int     # 0 text, 1 tag
    length: int   # in UTF-16 units
    first: int    # first and last units of a text run; 0 for a tag
    last: int


def _tag_length(tag) -> int:
    return 3 + len(tag.args)     # 0x0010, count, code, arguments


def segments_of(stored) -> list[Segment]:
    out = []
    for segment in parse_segments(stored):
        if isinstance(segment, Literal):
            out.append(Segment(0, len(segment.units), segment.units[0], segment.units[-1]))
        else:
            out.append(Segment(1, _tag_length(segment), 0, 0))
    return out


def text_runs(stored) -> list[tuple[int, str]]:
    runs = []
    offset = 0
    for segment in parse_segments(stored):
        if isinstance(segment, Literal):
            runs.append((offset, "".join(map(chr, segment.units))))
            offset += len(segment.units)
        else:
            offset += _tag_length(segment)
    return runs


def _chunk_starts(stored) -> list[bool]:
    """For each text run: does it start a chunk? Not after a tag that puts in a name or a number (codes 01xx, 02xx):
    「{name}が」. After a colour or a wait, it does."""
    out, joined = [], False
    for segment in parse_segments(stored):
        if isinstance(segment, Literal):
            out.append(not joined)
        joined = not isinstance(segment, Literal) and (segment.code >> 8) in (0x01, 0x02)
    return out


def line_units(analyzer, stored) -> list:
    out = []
    for (offset, run), starts in zip(text_runs(stored), _chunk_starts(stored)):
        units = analyzer.units(run, chunk_start=starts)
        if offset and units and units[0].start == 0 and units[0].surface in HONORIFICS:
            # look it up as written: the analyser may have read it as 三 or 酸
            units[0] = replace(units[0], kind="word", pos="suffix", lemma=units[0].surface,
                               normalized=units[0].surface, endings=())
        out += [replace(u, start=u.start + offset, end=u.end + offset) for u in units]
    return out
