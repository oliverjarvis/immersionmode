"""The entry a unit's card shows: a game name, a particle note, or a JMdict entry."""
from __future__ import annotations

import re
from dataclasses import dataclass

from .jmdict import front, pos_label

JAPANESE = re.compile(r"[぀-ヿ㐀-鿿]")
PARTICLE_POS = ("prt", "aux", "exp", "conj")    # JMdict entries a particle may resolve to


@dataclass(frozen=True)
class CardEntry:
    kind: str                # "dictionary", "particle", "name", or "lemma" (no match: form and reading only)
    headword: str
    reading: str
    pos: str
    senses: tuple[str, ...]
    jmdict_id: int = 0       # the JMdict entry ("dictionary" kind only); dict.bin v3 and ids.bin carry it


def is_content(unit) -> bool:
    """Counted for coverage: words and names written in Japanese script."""
    return unit.kind in ("word", "name") and bool(JAPANESE.search(unit.surface))


class Resolver:
    def __init__(self, index, glossary: dict[str, str], notes: dict[str, str]):
        self._index = index
        self._glossary = glossary
        self._notes = notes
        self._cache: dict[tuple, CardEntry | None] = {}

    def resolve(self, unit, choice: int | None = None) -> CardEntry | None:
        """The unit's entry: ranking's pick, or, with a decision for this occurrence (decide.py), the entry
        with that JMdict id (0: none fits, so the card shows the lemma alone)."""
        if choice is not None:
            return self._chosen(unit, choice)
        key = (unit.kind, unit.lemma, unit.reading, unit.pos, unit.normalized)
        if key not in self._cache:
            self._cache[key] = self._resolve(unit)
        return self._cache[key]

    def _chosen(self, unit, choice: int) -> CardEntry | None:
        if choice == 0:
            return self._lemma(unit)
        for via in dict.fromkeys(form for form in (unit.normalized, unit.lemma) if form):
            for entry in self._index.candidates(via, unit.reading, unit.pos):
                if int(entry.id) == choice:
                    return self._card(unit, entry, via)
        return self.resolve(unit)                 # not a candidate (decide.py never answers so): ranking

    @staticmethod
    def _card(unit, entry, via: str) -> CardEntry:
        """The card for a found entry. `via` found it (Sudachi's normalized form or the lemma); the headword
        keeps the game's own spelling of the dictionary form (unit.lemma) when that is a proper form."""
        reading = unit.reading if unit.reading in entry.kana else (entry.kana[0] if entry.kana else "")
        return CardEntry("dictionary", front(entry, unit.lemma, reading), reading, pos_label(entry.pos), entry.senses,
                         int(entry.id))

    @staticmethod
    def _lemma(unit) -> CardEntry:
        """No match: the card still shows the dictionary form and its reading (spec 7.3, step 4)."""
        return CardEntry("lemma", unit.lemma, unit.reading, "", ())

    def _resolve(self, unit) -> CardEntry | None:
        if unit.kind == "name":
            english = self._glossary.get(unit.lemma)
            return CardEntry("name", unit.lemma, "", "name", (english,)) if english else self._lemma(unit)
        if unit.kind == "particle":
            for form in (unit.lemma, unit.normalized):        # よー is a stretched よ
                if form in self._notes:
                    return CardEntry("particle", form, "", "particle", (self._notes[form],))
        entry, via = None, unit.lemma
        if unit.normalized and unit.normalized != unit.lemma:   # いう → 言う, 勝てる → 勝つ
            entry, via = self._index.lookup(unit.normalized, unit.reading, unit.pos), unit.normalized
        if entry is None:
            entry, via = self._index.lookup(unit.lemma, unit.reading, unit.pos), unit.lemma
        if entry is not None and unit.kind == "particle" and not any(
                code.startswith(PARTICLE_POS) for code in entry.pos):
            entry = None                                          # い is not 胃 (stomach)
        if entry is None:
            return self._lemma(unit)
        return self._card(unit, entry, via)
