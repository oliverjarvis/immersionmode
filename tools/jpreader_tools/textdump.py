"""Every line of the game's text, English beside Japanese kanji and kana, with who says it."""
from __future__ import annotations

import csv
import json
from pathlib import Path

from . import speakers
from .text import load_archive, render

# (set, English archive, kanji archive, kana archive)
SETS = (("system", 32, 31, 30), ("story", 42, 41, 40))
FIELDS = ("set", "file", "line", "kind", "speaker", "speaker_type", "gender", "age", "speaker_source", "guessed",
          "speaker_tag", "speaker_model", "en", "ja_kanji", "ja_kana")
# Labels a language model gave story lines the scripts leave open, by reading each line and its
# neighbours: file, line, kind, speaker, gender, age. Their kinds add "name": a bare name or label in a list
GUESSES = Path(__file__).resolve().parents[1] / "data" / "line-guesses.tsv"
OPEN_KINDS = {"unknown", "message"}


def rows(rom) -> dict[str, list[dict]]:
    out = {}
    for name, en_id, kanji_id, kana_id in SETS:
        en, kanji, kana = (load_archive(rom, a) for a in (en_id, kanji_id, kana_id))
        about = speakers.describe(rom, en, kanji) if name == "story" else {}
        out[name] = []
        for f, files in enumerate(zip(en, kanji, kana, strict=True)):
            for i, (e, k, h) in enumerate(zip(*files, strict=True)):
                s = about.get((f, i)) or speakers.LineInfo("system")
                out[name].append({
                    "set": name, "file": f, "line": i, "kind": s.kind,
                    "speaker": s.speaker, "speaker_type": s.speaker_type, "gender": s.gender, "age": s.age,
                    "speaker_source": s.source, "guessed": "",
                    "speaker_tag": f"{s.tag:04X}" if s.tag is not None else "",
                    "speaker_model": s.model if s.model is not None else "",
                    "en": render(e), "ja_kanji": render(k), "ja_kana": render(h)})
    return out


def load_guesses(path: Path = GUESSES) -> dict[tuple[int, int], dict]:
    if not path.exists():
        return {}
    with open(path, newline="", encoding="utf-8") as fh:
        return {(int(r["file"]), int(r["line"])): r for r in csv.DictReader(fh, delimiter="\t")}


def apply_guesses(story: list[dict], guesses: dict[tuple[int, int], dict]) -> None:
    """Fill what the scripts left open from the guesses; `guessed` names the fields filled."""
    for row in story:
        guess = guesses.get((row["file"], row["line"]))
        if not guess:
            continue
        filled = []
        if row["kind"] in OPEN_KINDS and guess["kind"]:
            row["kind"] = guess["kind"]
            filled.append("kind")
        if row["kind"] == "dialogue":
            for field in ("speaker", "gender", "age"):
                if not row[field] and guess[field]:
                    row[field] = guess[field]
                    filled.append(field)
        row["guessed"] = ",".join(filled)


def write(out_dir: Path, sets: dict[str, list[dict]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, lines in sets.items():
        with open(out_dir / f"{name}.tsv", "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=FIELDS, delimiter="\t")
            w.writeheader()
            # one row per line: line breaks become \n
            w.writerows({k: v.replace("\n", "\\n") if isinstance(v, str) else v for k, v in r.items()} for r in lines)
        with open(out_dir / f"{name}.jsonl", "w", encoding="utf-8") as fh:
            for r in lines:
                fh.write(json.dumps(r, ensure_ascii=False) + "\n")
