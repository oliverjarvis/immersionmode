"""Line up a kanji-script line's units with the same line in the kana script."""
from __future__ import annotations

import re

KANJI = re.compile(r"[々㐀-鿿]")


def _is_kanji(ch: str) -> bool:
    return bool(KANJI.match(ch))


def _unit_pattern(units, i: int, text: str) -> str:
    unit = units[i]
    touches_before = i > 0 and units[i - 1].end == unit.start and _is_kanji(text[unit.start]) \
        and _is_kanji(text[unit.start - 1])
    touches_after = i + 1 < len(units) and units[i + 1].start == unit.end and _is_kanji(text[unit.end - 1]) \
        and _is_kanji(text[unit.end])
    if (touches_before or touches_after) and unit.surface_reading:
        return re.escape(unit.surface_reading)
    pattern = []
    in_kanji = False
    for ch in unit.surface:
        if _is_kanji(ch):
            if not in_kanji:
                pattern.append("(?:.+?)")
            in_kanji = True
        else:
            pattern.append(re.escape(ch))
            in_kanji = False
    return "".join(pattern)


def project(kanji_text: str, units, kana_text: str) -> list[tuple[int, int]] | None:
    pieces = []
    position = 0
    for i, unit in enumerate(units):
        pieces.append(re.escape(kanji_text[position:unit.start]))
        pieces.append("(" + _unit_pattern(units, i, kanji_text) + ")")
        position = unit.end
    pieces.append(re.escape(kanji_text[position:]))
    match = re.fullmatch("".join(pieces), kana_text, re.DOTALL)
    if match is None:
        return None
    return [match.span(i + 1) for i in range(len(units))]
