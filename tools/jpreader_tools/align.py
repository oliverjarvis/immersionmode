"""Where a line record's units sit in the text the game displayed (spec 6.3).

The payload does the same in jpr_annot_place and jpr_data_message; the end-to-end test uses this
module to check it unit for unit.
"""
from __future__ import annotations

from dataclasses import dataclass

from .capture import ARC_MEMORY, NONE
from .formats import UnitRecord

TAG_MARK = 0x0010
NONE_STATUS = 0          # the data files are not available
OK = 1
UNKNOWN_LINE = 2         # no file or line for this identity
NO_RECORD = 3            # the line has no units (nothing Japanese to look up)
MISMATCH = 4             # the displayed text does not fit the record
LANGUAGE_ARCHIVES = {0: (30, 40), 1: (31, 41)}   # system text, story text


@dataclass(frozen=True)
class Placed:
    start: int           # in the displayed text
    length: int
    unit: UnitRecord


def _run_at(shown, at: int, seg) -> bool:
    end = at + seg.length
    return seg.length > 0 and end <= len(shown) and shown[at] == seg.first and shown[end - 1] == seg.last


def _tag_at(shown, at: int, seg) -> bool:
    return at + seg.length <= len(shown) and shown[at] == TAG_MARK and shown[at + 1] == seg.length - 2


def _at(shown, at: int, seg) -> bool:
    return _run_at(shown, at, seg) if seg.kind == 0 else _tag_at(shown, at, seg)


def _segment_starts(segments, shown, latest: bool) -> list[int] | None:
    starts, at = [], 0
    for k, seg in enumerate(segments):
        starts.append(at)
        if seg.kind == 0:
            if not _run_at(shown, at, seg):
                return None
            at += seg.length
        elif _tag_at(shown, at, seg):
            at += seg.length
        elif k + 1 == len(segments):
            at = len(shown)                         # an inserted name ends the line
        else:
            nxt = segments[k + 1]
            spots = range(len(shown) - 1, at - 1, -1) if latest else range(at, len(shown))
            at = next((p for p in spots if _at(shown, p, nxt)), None)
            if at is None:
                return None
    return starts if at == len(shown) else None


def _placement(segments, shown) -> list[int] | None:
    """Where each segment starts, or None when the text does not fit or fits more than one way.

    An inserted name ends where the next segment is found. Taking the earliest such place at every
    replaced tag, and then the latest, gives the same answer exactly when the text fits only one
    way; when they differ (a name containing the next run, as のぞみ before の), the line gets no
    units rather than units in the wrong place.
    """
    earliest = _segment_starts(segments, shown, latest=False)
    if earliest is None or earliest != _segment_starts(segments, shown, latest=True):
        return None
    return earliest


def place(record, shown) -> list[Placed] | None:
    starts = _placement(record.segments, shown)
    if starts is None:
        return None
    stored, offset = [], 0
    for seg in record.segments:
        stored.append(offset)
        offset += seg.length
    out = []
    for unit in record.units:
        for seg, at, start in zip(record.segments, starts, stored):
            if seg.kind == 0 and start <= unit.start and unit.start + unit.length <= start + seg.length:
                out.append(Placed(at + unit.start - start, unit.length, unit))
                break
        else:
            return None
    return out


def _try(file, ident, shown) -> tuple[int, list[Placed]]:
    if ident.line >= len(file.lines):
        return UNKNOWN_LINE, []
    record = file.lines[ident.line]
    if record is None:
        return NO_RECORD, []
    placed = place(record, shown)
    return (OK, placed) if placed is not None else (MISMATCH, [])


def lookup(archives, ident, language: int, shown) -> tuple[int, list[Placed]]:
    """The units of a captured message, found the way the payload finds them."""
    if ident.line == NONE:
        return UNKNOWN_LINE, []
    if ident.arc >= 0 and ident.dat != NONE:
        files = archives.get(ident.arc, [])
        return _try(files[ident.dat], ident, shown) if ident.dat < len(files) else (UNKNOWN_LINE, [])
    if ident.arc != ARC_MEMORY:
        return UNKNOWN_LINE, []
    status = UNKNOWN_LINE
    for arc in LANGUAGE_ARCHIVES.get(language, ()):
        for file in archives.get(arc, []):
            if len(file.lines) != ident.hdr_lines or file.total != ident.hdr_total:
                continue
            found, placed = _try(file, ident, shown)
            if found == OK:
                return OK, placed
            status = max(status, found)               # a line that did not fit says more than none found
    return status, []
