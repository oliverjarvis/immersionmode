"""Katakana game names (Pokémon, moves, items, places, people) paired with their English names."""
from __future__ import annotations

import re
from dataclasses import replace

from .text import load_archive

KATAKANA = re.compile(r"^[\u30a0-\u30ff]+$")
LATIN = re.compile(r"[A-Za-z]")
MAX_NAME = 16
# The system text holds name lists (species, moves, items, abilities, places, people, trainer
# classes) among menu labels and the name-entry keyboard. A file is a name list if it holds one of
# these names; only name lists feed the glossary.
NAME_ANCHORS = ("フシギダネ", "メガトンパンチ", "モンスターボール", "シンクロ", "ハウオリシティ", "ククイ", "ミニスカート")
MIN_NAME_FILE_LINES = 20


def _text(units) -> str:
    return "".join(map(chr, units))


def glossary_from(japanese_archives, english, anchors=NAME_ANCHORS, min_lines=MIN_NAME_FILE_LINES) -> dict[str, str]:
    glossary: dict[str, str] = {}
    for files in japanese_archives:
        for f, jp_file in enumerate(files):
            if f >= len(english) or len(jp_file) < min_lines:
                continue
            if not any(_text(line) in anchors for line in jp_file):
                continue
            for jp_line, en_line in zip(jp_file, english[f]):
                if 0x0010 in jp_line or 0x0010 in en_line:
                    continue
                key, value = _text(jp_line), _text(en_line).strip()
                if (2 <= len(key) <= MAX_NAME and KATAKANA.match(key) and value != key
                        and LATIN.search(value) and "\n" not in value):
                    glossary.setdefault(key, value)
    return glossary


def build_glossary(rom) -> dict[str, str]:
    return glossary_from([load_archive(rom, 30), load_archive(rom, 31)], load_archive(rom, 32))


def merge_names(units, glossary: dict[str, str]) -> list:
    out = []
    i = 0
    while i < len(units):
        best = None
        joined = ""
        for j in range(i, len(units)):
            if not KATAKANA.match(units[j].surface) or (j > i and units[j].start != units[j - 1].end):
                break
            joined += units[j].surface
            if joined in glossary:
                best = (j, joined)
        if best is None:
            out.append(units[i])
            i += 1
            continue
        last, name = best
        out.append(replace(units[i], end=units[last].end, kind="name", surface=name, lemma=name, reading="",
                           pos="noun", endings=(), surface_reading=""))
        i = last + 1
    return out
