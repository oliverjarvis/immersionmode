"""JMdict (English) from the jmdict-simplified JSON release: download, parse and look up.

JMdict is the property of the Electronic Dictionary Research and Development Group and is used
under the Creative Commons Attribution-ShareAlike 4.0 licence.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import zipfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

JMDICT_RELEASE = "3.6.2+20261005200550"
JMDICT_URL = ("https://github.com/scriptin/jmdict-simplified/releases/download/"
              "3.6.2%2B20261005200550/jmdict-eng-3.6.2+20261005200550.json.zip")
JMDICT_FILE = "jmdict-eng-3.6.2.json.zip"
JMDICT_SHA256 = "4601650cb900865cb9b388d6a045835120b90fbe451048d319bb8999635d596f"
MAX_SENSES = 3
MAX_SENSE_CHARS = 80
POS_PREFIXES = {"verb": ("v",), "adj": ("adj",), "noun": ("n", "pn"), "adv": ("adv",), "suffix": ("suf",)}
POS_LABELS = {
    "v1": "verb (ichidan)", "v5": "verb (godan)", "vs": "verb (suru)", "vk": "verb (kuru)",
    "vz": "verb (zuru)", "adj-i": "i-adjective", "adj-ix": "i-adjective", "adj-na": "na-adjective",
    "adj-no": "noun (takes の)", "adj-pn": "pre-noun adjective", "n": "noun", "pn": "pronoun",
    "adv": "adverb", "adv-to": "adverb", "int": "interjection", "exp": "expression", "prt": "particle",
    "conj": "conjunction", "aux-v": "auxiliary verb", "aux-adj": "auxiliary adjective", "suf": "suffix",
    "pref": "prefix", "ctr": "counter", "cop": "copula",
}


class JmdictError(Exception):
    pass


ODD_TAGS = frozenset({"sK", "sk", "iK", "ik", "rK", "rk", "oK", "ok", "io"})   # search-only, irregular, rare, outdated


@dataclass(frozen=True)
class Entry:
    id: str
    kanji: tuple[str, ...]
    kana: tuple[str, ...]
    common: bool
    usually_kana: bool
    pos: tuple[str, ...]
    senses: tuple[str, ...]
    common_forms: frozenset = frozenset()   # the forms JMdict marks common
    odd_forms: frozenset = frozenset()      # search-only, irregular, rare or outdated forms: never a headword


def fetch(cache: Path) -> Path:
    path = cache / JMDICT_FILE
    if not path.exists():
        cache.mkdir(parents=True, exist_ok=True)
        subprocess.run(["curl", "-fL", "--retry", "3", "-o", str(path), JMDICT_URL], check=True)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != JMDICT_SHA256:
        path.unlink()
        raise JmdictError(f"{path.name} has SHA-256 {digest}, expected {JMDICT_SHA256}")
    return path


def _sense_text(sense) -> str:
    text = "; ".join(g["text"] for g in sense["gloss"] if g.get("lang", "eng") == "eng")
    return text if len(text) <= MAX_SENSE_CHARS else text[:MAX_SENSE_CHARS - 1] + "…"


def parse(data: dict) -> list[Entry]:
    entries = []
    for word in data["words"]:
        senses = [sense for sense in word["sense"] if sense["gloss"]]
        if not senses:
            continue
        entries.append(Entry(
            id=word["id"],
            kanji=tuple(k["text"] for k in word["kanji"]),
            kana=tuple(k["text"] for k in word["kana"]),
            common=any(k["common"] for k in word["kanji"] + word["kana"]),
            usually_kana="uk" in senses[0].get("misc", []),
            pos=tuple(senses[0]["partOfSpeech"]),
            senses=tuple(_sense_text(sense) for sense in senses[:MAX_SENSES]),
            common_forms=frozenset(k["text"] for k in word["kanji"] + word["kana"] if k["common"]),
            odd_forms=frozenset(k["text"] for k in word["kanji"] + word["kana"] if ODD_TAGS & set(k.get("tags", []))),
        ))
    return entries


def load(path: Path) -> list[Entry]:
    with zipfile.ZipFile(path) as archive:
        return parse(json.loads(archive.read(archive.namelist()[0])))


def pos_label(codes) -> str:
    for code in codes:
        if code in POS_LABELS:
            return POS_LABELS[code]
        for prefix in ("v5", "v1", "vs"):
            if code.startswith(prefix):
                return POS_LABELS[prefix]
    return codes[0] if codes else ""


def _all_kana(text: str) -> bool:
    return all("぀" <= ch <= "ヿ" for ch in text)


class Index:
    def __init__(self, entries):
        self._by_text: dict[str, list[tuple[int, Entry]]] = defaultdict(list)
        for order, entry in enumerate(entries):
            for text in dict.fromkeys(entry.kanji + entry.kana):
                self._by_text[text].append((order, entry))

    def candidates(self, lemma: str, reading: str = "", pos: str | None = None, limit: int = 8) -> list[Entry]:
        """Entries the lemma could be, best first: part of speech, then whether the form that matched is
        common, then whether the entry is, then usually-kana for kana lemmas, then JMdict's order."""
        # Looking up by sound only makes sense for kana: ３ reads さん, which is not 酸 (acid).
        by_sound = self._by_text.get(reading) if reading and _all_kana(lemma) else None
        matched = self._by_text.get(lemma)
        found, form = (matched, lemma) if matched else (by_sound or [], reading)
        if reading:
            found = [c for c in found if reading in c[1].kana] or found
        prefixes = POS_PREFIXES.get(pos, ())
        kana_lemma = _all_kana(lemma)

        def score(item):
            order, entry = item
            pos_match = bool(prefixes) and any(code.startswith(prefixes) for code in entry.pos)
            return (pos_match, form in entry.common_forms, entry.common, kana_lemma and entry.usually_kana, -order)

        return [entry for _order, entry in sorted(found, key=score, reverse=True)[:limit]]

    def lookup(self, lemma: str, reading: str = "", pos: str | None = None) -> Entry | None:
        found = self.candidates(lemma, reading, pos, limit=1)
        return found[0] if found else None


def _first_common(entry: Entry, forms: list[str], default: str) -> str:
    """The first form JMdict marks common, else the first form: the kana kokoro lists first (its dictionary
    writes kana common-first), so both sides pick the same one."""
    return next((form for form in forms if form in entry.common_forms), forms[0] if forms else default)


def front(entry: Entry, spelling: str, reading: str) -> str:
    """How a card writes the word (spec: word identity 3): usually-kana words in kana; else the spelling
    the text used when it is a proper form of the entry; else the first common kanji form; else the first
    proper form."""
    proper_kana = [k for k in entry.kana if k not in entry.odd_forms]
    proper_kanji = [k for k in entry.kanji if k not in entry.odd_forms]
    if entry.usually_kana and proper_kana:
        return reading if reading in proper_kana else _first_common(entry, proper_kana, spelling)
    if spelling in proper_kanji:
        return spelling
    for form in proper_kanji:
        if form in entry.common_forms:
            return form
    if proper_kanji:
        return proper_kanji[0]
    return reading if reading in proper_kana else _first_common(entry, proper_kana, spelling)
