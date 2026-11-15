"""Annotate every Japanese line of the game: units, card entries and ending labels."""
from __future__ import annotations

import hashlib
import re
import random
import struct
from collections import Counter
from dataclasses import dataclass, replace
from importlib import metadata
from pathlib import Path

from . import claude, decide, jmdict
from .analyze import Analyzer, Vocabulary, to_hiragana
from .dictionary import CardEntry, Resolver, is_content
from .formats import AnnotFile, LineRecord, UnitRecord, paired_files, write_ids
from .glossary import build_glossary, merge_names
from .inflect import DATA_DIR, inflection_labels, label, particle_notes
from .lines import line_units, segments_of, text_runs
from .project import project
from .rom import Rom
from .text import load_archive, load_archive_with_totals, render

PAIRS = ((31, 30), (41, 40))        # (kanji archive, kana archive), system text then story text
SOURCES = ("analyze", "lines", "project", "glossary", "dictionary", "formats", "annotate", "inflect",
           "jmdict", "text", "decide")
NOTICE = """jpreader data files

dict.bin contains material from JMdict (https://www.edrdg.org/jmdict/j_jmdict.html), the property
of the Electronic Dictionary Research and Development Group, used under the Creative Commons
Attribution-ShareAlike 4.0 licence (https://creativecommons.org/licenses/by-sa/4.0/).

font.bin and the payload's built-in letters are rendered from Noto Sans JP
(https://fonts.google.com/noto/specimen/Noto+Sans+JP), and font.bin's italic letters from Noto Sans
Italic (https://fonts.google.com/noto/specimen/Noto+Sans), both used under the SIL Open Font
License 1.1.

annot.bin and dict.bin also contain names taken from your own copy of the game. They are for your
own use: do not share them.
"""


@dataclass
class Coverage:
    content: int = 0
    covered: int = 0
    decided_none: int = 0     # no JMdict entry fits, as decided for the line (decide.py): an answer, not a gap

    def add(self, resolved, choices: dict[int, int] | None = None) -> None:
        choices = choices or {}
        for i, (unit, entry) in enumerate(resolved):
            if is_content(unit):
                self.content += 1
                if entry is not None and entry.kind != "lemma":
                    self.covered += 1
                elif choices.get(i) == 0:
                    self.decided_none += 1

    @property
    def share(self) -> float:
        return (self.covered + self.decided_none) / self.content if self.content else 1.0


@dataclass
class Annotations:
    archives: dict
    entries: list
    labels: list
    coverage: dict
    projected: int
    fallbacks: int
    uncovered: dict
    decisions: dict | None = None
    forms: dict | None = None           # JMdict id -> every spelling of the word, for ids.bin (9C)


class Annotator:
    def __init__(self, analyzer, resolver, glossary: dict[str, str], labels: dict[str, str]):
        self.analyzer = analyzer
        self.resolver = resolver
        self.glossary = glossary
        self.label_table = labels
        self.entries: list[CardEntry] = []
        self.labels: list[str] = []
        self._entry_ids: dict[CardEntry, int] = {}
        self._label_ids: dict[str, int] = {}
        self.projected = 0
        self.fallbacks = 0
        self.last_projected = False   # whether kana_units' last line lined up with its kanji line

    def _entry_id(self, entry: CardEntry | None) -> int | None:
        if entry is None:
            return None
        if entry not in self._entry_ids:
            self._entry_ids[entry] = len(self.entries)
            self.entries.append(entry)
        return self._entry_ids[entry]

    def _label_id(self, text: str | None) -> int | None:
        if text is None:
            return None
        if text not in self._label_ids:
            self._label_ids[text] = len(self.labels)
            self.labels.append(text)
        return self._label_ids[text]

    def units(self, stored) -> list:
        return merge_names(line_units(self.analyzer, stored), self.glossary)

    def record(self, stored, units, choices: dict[int, int] | None = None):
        if not units:
            return None, []
        choices = choices or {}
        resolved = [(unit, self.resolver.resolve(unit, choices.get(i))) for i, unit in enumerate(units)]
        records = tuple(UnitRecord(u.start, u.end - u.start, u.kind, self._entry_id(entry),
                                   self._label_id(label(u.endings, self.label_table)))
                        for u, entry in resolved)
        return LineRecord(tuple(segments_of(stored)), records), resolved

    def kana_units(self, kana_stored, kanji_stored, kanji_units) -> list:
        kanji_runs, kana_runs = text_runs(kanji_stored), text_runs(kana_stored)
        same_shape = [s.kind for s in segments_of(kanji_stored)] == [s.kind for s in segments_of(kana_stored)]
        if same_shape and len(kanji_runs) == len(kana_runs):
            out = []
            for (k_offset, k_text), (n_offset, n_text) in zip(kanji_runs, kana_runs):
                inside = [replace(u, start=u.start - k_offset, end=u.end - k_offset) for u in kanji_units
                          if k_offset <= u.start < k_offset + len(k_text)]
                spans = project(k_text, inside, n_text)
                if spans is None:
                    break
                out += [replace(u, start=n_offset + a, end=n_offset + b, surface=n_text[a:b])
                        for u, (a, b) in zip(inside, spans)]
            else:
                self.projected += 1
                self.last_projected = True
                return out
        self.fallbacks += 1
        self.last_projected = False
        return self.units(kana_stored)


def kana_choices(projected: bool, kanji_choices: dict[int, int] | None) -> dict[int, int] | None:
    """A kana line inherits its kanji line's decisions only when its units are that line's units, lined up."""
    return kanji_choices if projected else None


def _text(stored) -> str:
    return "".join(run for _offset, run in text_runs(stored))


_KANJI = re.compile(r"[々\u3400-\u9fff]")
VOCABULARY_POS = ("verb", "adj", "noun", "adv")
GRAMMAR_POS = ("prt", "conj", "exp", "aux", "cop")


def game_vocabulary(rom, analyzer, glossary: dict[str, str]) -> Vocabulary:
    """The words the game writes in kanji in one script and in kana in the other (stage 16), from each pair of lines
    whose units line up: (kanji lemma, reading) and how often, kept only where SudachiPy reads the kanji as the game's
    kana. The analyser repairs kana words it broke with them (analyze.Analyzer._repair)."""
    annotator = Annotator(analyzer, None, glossary, {})
    words: Counter = Counter()
    surfaces: dict = {}
    for kanji_arc, kana_arc in PAIRS:
        for (_kt, kanji_lines), (_nt, kana_lines) in zip(load_archive_with_totals(rom, kanji_arc),
                                                         load_archive_with_totals(rom, kana_arc)):
            for kanji_stored, kana_stored in zip(kanji_lines, kana_lines):
                units = annotator.units(kanji_stored)
                kana_units = annotator.kana_units(kana_stored, kanji_stored, units)
                if not annotator.last_projected:
                    continue
                for k, n in zip(units, kana_units):
                    if (k.kind == "word" and k.pos in VOCABULARY_POS and _KANJI.search(k.surface)
                            and not _KANJI.search(n.surface) and to_hiragana(n.surface) == k.surface_reading):
                        words[(k.lemma, k.reading)] += 1
                        surfaces.setdefault((k.lemma, k.reading), set()).add(to_hiragana(n.surface))
    return Vocabulary(words, surfaces)


def grammar_kana(words) -> frozenset:
    """Kana JMdict lists as particles, conjunctions, expressions, auxiliaries or copulas: a repair never swallows them."""
    return frozenset(k for w in words if any(p.startswith(GRAMMAR_POS) for p in w.pos) for k in w.kana)


def annotate_rom(rom, annotator: Annotator, decisions=None, index=None, ask=None) -> Annotations:
    """Two passes: analyse every kanji line and collect the words with more than one candidate entry; ask
    (decide.run) once for all of them; then record each line with its decisions. `ask(asks) -> summary`."""
    archives: dict[int, list] = {}
    coverage: dict[int, Coverage] = {}
    uncovered: Counter = Counter()
    files = []                                  # (kanji_arc, kana_arc, kanji files, kana files, english files)
    for kanji_arc, kana_arc in PAIRS:
        english = load_archive(rom, kanji_arc + 1) if decisions is not None else []
        files.append((kanji_arc, kana_arc, load_archive_with_totals(rom, kanji_arc),
                      load_archive_with_totals(rom, kana_arc), english))

    analysed: dict[tuple, tuple] = {}           # (arc, file, line) -> (units, ask key or None)
    asks = []
    for kanji_arc, kana_arc, kanji_files, kana_files, english in files:
        for f, ((_kt, kanji_lines), (_nt, kana_lines)) in enumerate(zip(kanji_files, kana_files)):
            before = ""
            for n, kanji_stored in enumerate(kanji_lines):
                units = annotator.units(kanji_stored)
                key = None
                if decisions is not None and units:
                    text = _text(kanji_stored)
                    kana_text = _text(kana_lines[n]) if n < len(kana_lines) else ""
                    en_file = english[f] if f < len(english) else []
                    en_text = _text(en_file[n]).replace("\n", " ") if n < len(en_file) else ""
                    found = decide.asks_for_line(units, text_runs(kanji_stored), kana_text, en_text, before, index)
                    if found is not None:
                        asks.append(found)
                        key = found.key
                    before = text
                analysed[(kanji_arc, f, n)] = (units, key)
    summary = ask(asks) if (decisions is not None and ask is not None) else None

    for kanji_arc, kana_arc, kanji_files, kana_files, _english in files:
        archives[kanji_arc], archives[kana_arc] = [], []
        coverage[kanji_arc], coverage[kana_arc] = Coverage(), Coverage()
        for f, ((kanji_total, kanji_lines), (kana_total, kana_lines)) in enumerate(zip(kanji_files, kana_files)):
            kanji_out, kana_out = [], []
            for n, kanji_stored in enumerate(kanji_lines):
                units, key = analysed[(kanji_arc, f, n)]
                choices = decisions.get(key) if (decisions is not None and key) else None
                record, resolved = annotator.record(kanji_stored, units, choices)
                kanji_out.append(record)
                coverage[kanji_arc].add(resolved, choices)
                uncovered.update(u.lemma for u, entry in resolved
                                 if (entry is None or entry.kind == "lemma") and is_content(u))
                if n < len(kana_lines):
                    kana_stored = kana_lines[n]
                    kana_units = annotator.kana_units(kana_stored, kanji_stored, units)
                    inherited = kana_choices(annotator.last_projected, choices)
                    record, resolved = annotator.record(kana_stored, kana_units, inherited)
                    kana_out.append(record)
                    coverage[kana_arc].add(resolved, inherited)
            for kana_stored in kana_lines[len(kanji_lines):]:
                kana_out.append(annotator.record(kana_stored, annotator.units(kana_stored))[0])
            archives[kanji_arc].append(AnnotFile(kanji_total, kanji_out))
            archives[kana_arc].append(AnnotFile(kana_total, kana_out))
    return Annotations(archives, annotator.entries, annotator.labels, coverage, annotator.projected,
                       annotator.fallbacks, dict(uncovered), summary)


def decisions_path(root: Path) -> Path:
    return root / "build" / "cache" / "decisions.json"


def build_files(root: Path, rom_path: Path):
    """annot.bin, dict.bin and the result. Words with more than one candidate entry are decided by Claude
    (decide.py) unless JPREADER_LLM=0 or there is no ANTHROPIC_API_KEY; decisions already made are always used."""
    import os

    words = jmdict.load(jmdict.fetch(root / "build" / "cache"))
    index = jmdict.Index(words)
    decisions = decide.Decisions(decisions_path(root))
    key = None if os.environ.get("JPREADER_LLM") == "0" else claude.read_key(root)
    budget = float(os.environ.get("JPREADER_LLM_BUDGET_USD", "4.0"))

    def ask(asks):
        return decide.run(asks, decisions, key, budget, root / "build" / "cache", print)

    with Rom(rom_path) as rom:
        glossary = build_glossary(rom)
        vocabulary = game_vocabulary(rom, Analyzer(), glossary)
        annotator = Annotator(Analyzer(vocabulary, grammar_kana(words)), Resolver(index, glossary, particle_notes()),
                              glossary, inflection_labels())
        result = annotate_rom(rom, annotator, decisions, index, ask)
    used = {entry.jmdict_id for entry in result.entries if entry.jmdict_id}
    result.forms = {int(w.id): list(dict.fromkeys(w.kanji + w.kana)) for w in words if int(w.id) in used}
    return (*paired_files(result.archives, result.entries, result.labels), result)


def _cache_key(code_sha256: str, decisions_digest: str = "") -> str:
    digest = hashlib.sha256(code_sha256.encode())
    digest.update(f"{decide.PROMPT_VERSION}:{claude.MODEL}:{decisions_digest}".encode())
    digest.update(jmdict.JMDICT_SHA256.encode())
    for name in ("inflections.tsv", "particles.tsv"):
        digest.update((DATA_DIR / name).read_bytes())
    here = Path(__file__).resolve().parent
    for name in SOURCES:
        digest.update((here / f"{name}.py").read_bytes())
    for package in ("sudachipy", "sudachidict_core"):
        digest.update(metadata.version(package).encode())
    return digest.hexdigest()[:16]


def _claude_usable(root: Path) -> bool:
    import os

    return os.environ.get("JPREADER_LLM") != "0" and bool(claude.read_key(root))


def cached_files(root: Path, rom_path: Path, code_sha256: str) -> tuple[bytes, bytes, bytes]:
    """annot.bin, dict.bin and ids.bin, built once per input: the key covers the code, the data and the
    decisions made. A build that left words undecided (no key, offline, over budget) is redone once Claude
    can be asked, so it never silently stands in for a decided one."""
    import json

    cache = root / "build" / "cache"

    def paths_for(key: str) -> list[Path]:
        return [cache / f"{name}-{key}.bin" for name in ("annot", "dict", "ids")] + [cache / f"meta-{key}.json"]

    found = paths_for(_cache_key(code_sha256, decide.Decisions(decisions_path(root)).digest()))
    if all(path.exists() for path in found):
        undecided = json.loads(found[3].read_text()).get("undecided", 0)
        if not (undecided and _claude_usable(root)):
            return tuple(path.read_bytes() for path in found[:3])
    annot, dictionary, result = build_files(root, rom_path)
    ids = write_ids(result.entries, struct.unpack_from("<I", dictionary, 24)[0], result.forms or {})
    undecided = (result.decisions or {}).get("undecided", 0)
    made = paths_for(_cache_key(code_sha256, decide.Decisions(decisions_path(root)).digest()))
    cache.mkdir(parents=True, exist_ok=True)
    for path, data in zip(made, (annot, dictionary, ids)):
        path.write_bytes(data)
    made[3].write_text(json.dumps({"undecided": undecided}))
    return annot, dictionary, ids


def report(result: Annotations) -> str:
    names = {30: "system text, kana", 31: "system text, kanji", 40: "story text, kana", 41: "story text, kanji"}
    lines = ["| Archive | Content words | With an entry | Decided: none fits | Coverage |", "|---|---|---|---|---|"]
    total = Coverage()
    for arc in sorted(result.coverage):
        c = result.coverage[arc]
        total.content += c.content
        total.covered += c.covered
        total.decided_none += c.decided_none
        lines.append(f"| {arc} ({names[arc]}) | {c.content} | {c.covered} | {c.decided_none} | {c.share:.1%} |")
    lines.append(f"| **All** | {total.content} | {total.covered} | {total.decided_none} | **{total.share:.1%}** |")
    top = ", ".join(f"{lemma} ({n})" for lemma, n in Counter(result.uncovered).most_common(30))
    d = result.decisions
    if d:
        lines += ["", f"Decisions: {d['asked']} lines asked, {d['decided']} decided now ({d['cached']} already cached), "
                      f"{d['undecided']} undecided, {d.get('given_up', 0)} left to ranking after {decide.MAX_TRIES} tries, "
                      f"{d['ignored']} answers ignored; batch {d['batch_id']}; "
                      f"estimate ${d['cost_estimate_usd']:.2f}."]
    lines += ["", f"Kana lines lined up with their kanji line: {result.projected}; analysed on their own: "
              f"{result.fallbacks}.", f"Card entries: {len(result.entries)}; ending labels: {len(result.labels)}.",
              "", f"Most frequent words without an entry: {top}."]
    return "\n".join(lines)


def describe_line(stored, record, entries, labels) -> list[str]:
    out = ["`" + render(stored).replace("\n", "／") + "`", ""]
    for unit in record.units:
        surface = "".join(map(chr, stored[unit.start:unit.start + unit.length]))
        entry = entries[unit.entry] if unit.entry is not None else None
        if entry is None or entry.kind == "lemma":
            out.append(f"- {surface} ({unit.kind}): no entry")
            continue
        reading = f" 【{entry.reading}】" if entry.reading else ""
        ending = f" — {labels[unit.label]}" if unit.label is not None else ""
        out.append(f"- {surface} → {entry.headword}{reading}, {entry.pos}: {entry.senses[0]}{ending}")
    return out + [""]


def sample(rom, result: Annotations, count: int, seed: int) -> str:
    story = load_archive(rom, 41)
    candidates = [(f, n) for f, file in enumerate(result.archives[41]) for n, record in enumerate(file.lines)
                  if record is not None and len(record.units) >= 3]
    chosen = sorted(random.Random(seed).sample(candidates, min(count, len(candidates))))
    out = []
    for f, n in chosen:
        out.append(f"### Story file {f}, line {n}")
        out += describe_line(story[f][n], result.archives[41][f].lines[n], result.entries, result.labels)
    return "\n".join(out)
