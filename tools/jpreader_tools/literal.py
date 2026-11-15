"""Literal translations for the peek (stage 15): what a game name or label says word for word (なきごえ "cry",
not Growl; キズぐすり "wound medicine", not Potion), shown before the official English. Every move, item, Pokémon,
ability, type and nature name and every short label in the system text is asked of Claude once, in one batch,
and kept in build/cache/literal.json; the build only reads that file (`jpreader literal` fills it)."""
from __future__ import annotations

import hashlib
import json
import re
import time
from dataclasses import dataclass
from pathlib import Path

from . import claude, formats

PROMPT_VERSION = 1
MODEL = "claude-sonnet-5-5"
PRICE_PER_M = {"input": 3.0, "output": 15.0}   # an upper guess for Sonnet; batches cost half
CHUNK = 100             # strings per request
LITERAL_MAX = 40        # units kept of an answer (the peek shows it on one small line)
LABEL_MAX = 8           # key units of a label: longer system text is a message
MAX_TRIES = 2
NAME_FILES = {118: "move", 40: "item", 60: "Pokémon species", 101: "ability", 112: "type", 92: "nature"}
_JAPANESE = re.compile(r"[぀-ヿ㐀-鿿]")

SYSTEM = (
    "A reading aid for Pokémon Ultra Moon shows learners of Japanese what the game's names and labels say. For "
    "each numbered Japanese string give a short literal English translation of the Japanese words themselves, "
    "not the official English name, which is given only so you know what the thing is.\n"
    "- Translate word for word, in plain lower case: なきごえ \"cry\" (not Growl), キズぐすり \"wound medicine\" "
    "(not Potion), でんこうせっか \"quick as lightning\", しっぽをふる \"wag tail\", モンスターボール \"monster "
    "ball\", たたかう \"fight\", にげる \"run away\".\n"
    "- A coined name (most Pokémon, some moves and items): its parts joined by hyphens, as a learner would read "
    "them: ピカチュウ \"sparkle-squeak\", フシギダネ \"strange-seed\".\n"
    "- Keep it under 30 characters. Capitalise only proper nouns.\n"
    "- Answer \"\" for a name with no meaning to translate (a person, a place, a Pokémon such as ディアルガ) and "
    "for a string that is only a sound or a symbol."
)

SCHEMA = {
    "type": "object", "additionalProperties": False, "required": ["items"],
    "properties": {"items": {"type": "array", "items": {
        "type": "object", "additionalProperties": False, "required": ["n", "literal"],
        "properties": {"n": {"type": "integer"}, "literal": {"type": "string"}}}}},
}


@dataclass(frozen=True)
class Ask:
    key: str          # formats.screen_key of the string, as text
    kind: str         # NAME_FILES' kinds, or "label"
    english: str      # the official English line, for context


def _text(units) -> str:
    return "".join(map(chr, units))


def asks_from(kana, kanji, english) -> list[Ask]:
    """Every name in NAME_FILES and every short label, kana and kanji, each key once (the first seen wins)."""
    from .build import _english
    from .text import Tag, parse_segments

    asks: dict[str, Ask] = {}
    for archive in (kana, kanji):
        for file, lines in enumerate(archive):
            for line, units in enumerate(lines):
                key = _text(formats.screen_key(units))
                if not key or key in asks or not _JAPANESE.search(key):
                    continue
                if any(isinstance(s, Tag) for s in parse_segments(units)):
                    continue
                kind = NAME_FILES.get(file)
                if kind is None and len(key) > LABEL_MAX:
                    continue
                en = english[file][line] if file < len(english) and line < len(english[file]) else []
                asks[key] = Ask(key, kind or "label", _text(_english(en)))
    return list(asks.values())


def user_text(asks: list[Ask]) -> str:
    return "\n".join(f"{n}. {a.key} ({a.kind}; official English: {a.english or 'none'})" for n, a in enumerate(asks))


def request_for(asks: list[Ask], model: str) -> dict:
    custom_id = hashlib.sha256("|".join(a.key for a in asks).encode()).hexdigest()[:32]
    return {"custom_id": custom_id,
            "params": claude.params(model, SYSTEM, user_text(asks), SCHEMA, max_tokens=200 + 30 * len(asks))}


def apply_answer(asks: list, answer: dict) -> dict[str, str]:
    """key -> literal for each numbered answer that names an ask ("" when there is nothing to translate)."""
    out: dict[str, str] = {}
    for item in answer.get("items", []):
        n, text = item.get("n"), item.get("literal")
        if isinstance(n, int) and 0 <= n < len(asks) and asks[n] is not None and isinstance(text, str):
            out[asks[n].key] = " ".join(text.split())[:LITERAL_MAX]
    return out


def units_for(literals, key_units, english=()) -> list[int]:
    """The literal of a string, by its key, as UTF-16 units for strings.bin; none ([]) when there is none or it only
    repeats the official English (Trick, "trick")."""
    found = literals.get(_text(key_units)) if literals else None
    if not found or found.casefold() == _text(english).casefold():
        return []
    return [ord(c) for c in found]


class Literals:
    """build/cache/literal.json: key -> literal ("" for none), and a batch in flight."""

    def __init__(self, path: Path, model: str = MODEL):
        self.path, self.model = path, model
        self.items: dict[str, str] = {}
        self.tries: dict[str, int] = {}
        self.pending: dict | None = None
        if path.exists():
            data = json.loads(path.read_text())
            if data.get("version") == PROMPT_VERSION and data.get("model") == model:
                self.items, self.tries, self.pending = data.get("items", {}), data.get("tries", {}), data.get("pending")

    def get(self, key: str) -> str | None:
        return self.items.get(key)

    def put(self, key: str, text: str) -> None:
        self.items[key] = text

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temp = self.path.with_suffix(".tmp")
        temp.write_text(json.dumps({"version": PROMPT_VERSION, "model": self.model, "items": self.items,
                                    "tries": self.tries, "pending": self.pending}, ensure_ascii=False, sort_keys=True))
        temp.replace(self.path)


def estimate_usd(requests: list) -> float:
    tokens_in = tokens_out = 0
    for request in requests:
        text = request["params"]["system"] + request["params"]["messages"][0]["content"]
        tokens_in += sum(1 if ord(ch) > 0x2E80 else 0.25 for ch in text)
        tokens_out += request["params"]["max_tokens"]
    return (tokens_in * PRICE_PER_M["input"] + tokens_out * PRICE_PER_M["output"]) / 1_000_000 / 2


def _collect(batch, batch_id: str, requests: dict[str, list[str]], by_key: dict, cache: Literals, summary: dict,
             log, poll_s: float) -> None:
    while (status := batch.status(batch_id)) != "ended":
        log(f"literal: batch {batch_id} {status}")
        time.sleep(poll_s)
    results = batch.results(batch_id)
    for custom_id, keys in requests.items():
        answer = results.get(custom_id)
        got = apply_answer([by_key.get(k) for k in keys], answer) if answer else {}
        for key in keys:
            if key in got:
                summary["answered"] += cache.get(key) is None
                cache.put(key, got[key])
            elif key in by_key and cache.get(key) is None:
                cache.tries[key] = cache.tries.get(key, 0) + 1
    cache.pending = None
    cache.save()


def run(asks: list[Ask], cache: Literals, key: str | None, budget_usd: float, folder: Path, log,
        model: str = MODEL, poll_s: float = 60) -> dict:
    """Asks every string not in the cache: first collects a batch an earlier run left pending, then sends one batch.
    Failures are logged (never the key) and leave strings without a literal."""
    by_key = {a.key: a for a in asks}
    summary = {"asked": len(by_key), "cached": sum(cache.get(k) is not None for k in by_key), "answered": 0,
               "cost_estimate_usd": 0.0, "batch_id": None}
    if not key:
        return summary
    try:
        batch = claude.Batch(key, folder) if cache.pending else None
        if cache.pending:
            log(f"literal: collecting batch {cache.pending['batch']} from an earlier run")
            _collect(batch, cache.pending["batch"], cache.pending["requests"], by_key, cache, summary, log, poll_s)
        todo = [a for k, a in by_key.items() if cache.get(k) is None and cache.tries.get(k, 0) < MAX_TRIES]
        if todo:
            chunks = [todo[n:n + CHUNK] for n in range(0, len(todo), CHUNK)]
            requests = [request_for(chunk, model) for chunk in chunks]
            summary["cost_estimate_usd"] = estimate = estimate_usd(requests)
            if estimate > budget_usd:
                log(f"literal: estimate ${estimate:.2f} is over the ${budget_usd:.2f} budget; not asking")
                return summary
            batch = batch or claude.Batch(key, folder)
            batch_id = batch.submit(requests)
            request_map = {r["custom_id"]: [a.key for a in chunk] for r, chunk in zip(requests, chunks)}
            cache.pending = {"batch": batch_id, "requests": request_map}
            cache.save()
            summary["batch_id"] = batch_id
            log(f"literal: batch {batch_id} sent, {len(requests)} requests, estimate ${estimate:.2f}")
            _collect(batch, batch_id, request_map, by_key, cache, summary, log, poll_s)
    except (claude.ClaudeError, OSError) as error:
        log(f"literal: Claude could not be asked ({error})")
    return summary


def cache_path(root: Path) -> Path:
    return root / "build" / "cache" / "literal.json"
