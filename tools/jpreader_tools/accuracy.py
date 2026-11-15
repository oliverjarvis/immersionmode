"""How often a game word points at the JMdict entry its line means (spec: word identity 9), measured on a
hand-checked gold set: tests/data/gold-entries.tsv holds line references, the word, and the right JMdict id
(0: no JMdict entry fits, so the card should show no meaning). No game lines are kept in the repository."""
from __future__ import annotations

import random
from dataclasses import dataclass
from pathlib import Path

from . import decide, formats
from .annotate import PAIRS, Annotator, _text
from .text import load_archive, load_archive_with_totals

HEADER = "archive\tfile\tline\tunit\tsurface\tlemma\tjmdict_id\tambiguous\tnote"


@dataclass(frozen=True)
class GoldRow:
    archive: int
    file: int
    line: int
    unit: int
    surface: str
    lemma: str
    jmdict_id: int
    ambiguous: bool
    note: str = ""

    @property
    def ref(self) -> tuple[int, int, int, int]:
        return (self.archive, self.file, self.line, self.unit)


def write_gold(path: Path, rows: list[GoldRow]) -> None:
    lines = [HEADER] + [f"{r.archive}\t{r.file}\t{r.line}\t{r.unit}\t{r.surface}\t{r.lemma}\t{r.jmdict_id}\t"
                        f"{int(r.ambiguous)}\t{r.note}" for r in rows]
    path.write_text("\n".join(lines) + "\n")


def read_gold(path: Path) -> list[GoldRow]:
    rows = []
    for line in path.read_text().splitlines()[1:]:
        if not line.strip():
            continue
        a, f, n, u, surface, lemma, jid, amb, *note = line.split("\t")
        rows.append(GoldRow(int(a), int(f), int(n), int(u), surface, lemma, int(jid), amb == "1",
                            note[0] if note else ""))
    return rows


def chosen_from(annot_bin: bytes, ids_bin: bytes) -> dict[tuple[int, int, int, int], int]:
    """Each kanji-mode unit's JMdict id as built (0: a name, particle, lemma-only card or no entry)."""
    _stamp, ids, _pairs = formats.read_ids(ids_bin)
    out = {}
    for arc, files in formats.read_annot(annot_bin).items():
        if arc not in (kanji for kanji, _kana in PAIRS):
            continue
        for f, annot_file in enumerate(files):
            for n, record in enumerate(annot_file.lines):
                for u, unit in enumerate(record.units if record else ()):
                    out[(arc, f, n, u)] = ids[unit.entry] if unit.entry is not None else 0
    return out


def score_rows(gold: list[GoldRow], chosen: dict) -> dict:
    right = [row for row in gold if chosen.get(row.ref, 0) == row.jmdict_id]
    ambiguous = [row for row in gold if row.ambiguous]
    right_ambiguous = [row for row in ambiguous if chosen.get(row.ref, 0) == row.jmdict_id]
    wrong = [(row, chosen.get(row.ref, 0)) for row in gold if chosen.get(row.ref, 0) != row.jmdict_id]
    return {"overall": len(right) / len(gold) if gold else 0.0,
            "ambiguous": len(right_ambiguous) / len(ambiguous) if ambiguous else 0.0,
            "count": len(gold), "ambiguous_count": len(ambiguous), "wrong": wrong[:30], "wrong_count": len(wrong)}


@dataclass(frozen=True)
class Sampled:
    ref: tuple[int, int, int, int]
    surface: str
    lemma: str
    candidates: tuple
    ambiguous: bool
    kanji: str
    kana: str
    english: str
    before: str


def sample(rom, annotator: Annotator, index, count: int = 1500, ambiguous: int = 700, seed: int = 9) -> list[Sampled]:
    """Kanji-mode word units: `ambiguous` with more than one candidate entry, the rest from all others."""
    many, one = [], []
    for kanji_arc, kana_arc in PAIRS:
        english = load_archive(rom, kanji_arc + 1)
        for f, ((_t, kanji_lines), (_k, kana_lines)) in enumerate(zip(load_archive_with_totals(rom, kanji_arc),
                                                                       load_archive_with_totals(rom, kana_arc))):
            before = ""
            for n, stored in enumerate(kanji_lines):
                units = annotator.units(stored)
                text = _text(stored)
                kana = _text(kana_lines[n]) if n < len(kana_lines) else ""
                en = _text(english[f][n]).replace("\n", " ") if f < len(english) and n < len(english[f]) else ""
                for u, unit in enumerate(units):
                    if unit.kind != "word":
                        continue
                    found = tuple(decide._candidates(unit, index))
                    if not found:
                        continue
                    item = Sampled((kanji_arc, f, n, u), unit.surface, unit.lemma, found,
                                   len({e.id for e in found}) >= 2, text, kana, en, before)
                    (many if item.ambiguous else one).append(item)
                if units:
                    before = text
    rng = random.Random(seed)
    picked = rng.sample(many, min(ambiguous, len(many))) + rng.sample(one, min(count - ambiguous, len(one)))
    return sorted(picked, key=lambda s: s.ref)


def worksheet(items: list[Sampled]) -> str:
    """One block per word for a labeller: the line in kanji and kana, the game's English, and the candidates."""
    out = []
    for item in items:
        a, f, n, u = item.ref
        out += [f"### {a}\t{f}\t{n}\t{u}\t{item.surface}\t{item.lemma}\t{int(item.ambiguous)}",
                f"Before: {item.before}", f"Kanji: {item.kanji}", f"Kana: {item.kana}", f"English: {item.english}",
                f"Word: {item.surface} (dictionary form {item.lemma})"]
        out += [decide._describe(entry) for entry in item.candidates]
        out.append("")
    return "\n".join(out)
