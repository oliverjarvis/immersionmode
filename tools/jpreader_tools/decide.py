"""Which JMdict entry an ambiguous game word means (spec: word identity 5). Ranking settles most words; a
word with more than one candidate is asked of Claude once per line, with the line in kanji and kana, the
game's own English line and the line before. Answers must name one of the given candidates (or 0, none of
them) and are kept in build/cache/decisions.json, so a line is paid for once."""
from __future__ import annotations

import hashlib
import json
import time
from dataclasses import dataclass
from pathlib import Path

from . import claude

PROMPT_VERSION = 3
MAX_TRIES = 2           # a line asked this often without a usable answer is left to ranking
CHUNK = 20              # lines per request
EFFORT = "medium"       # Haiku thinks first: on the gold set 92.8% against 83.3% without (2026-10-12)
THINKING_TOKENS = 6000  # room for the thinking before the answer
MAX_CANDIDATES = 8

SYSTEM = (
    "A reading aid splits Pokémon game lines into words and shows a dictionary entry for each. You check "
    "the entries. For each line you get the Japanese (kanji and kana versions), the game's own English "
    "translation, the line before it, and some words, each shown inside its line between 【 and 】 with a "
    "numbered list of JMdict candidates.\n\n"
    "Answer with the JMdict id of the candidate that is the word as used in this line.\n"
    "- When the marked text is a meaningful part of a compound or a name, choose that part's entry: 体 in "
    "体当たり, 瓦 in 瓦割り, カード in データカード, 粉 (こ) in ひのこ, 薬 (ぐすり) in キズぐすり, 綿 (わた) in "
    "わたほうし. A learner reading the parts still learns real words.\n"
    "- Answer 0 only when the cut falls inside a word, so the marked text has no meaning of its own here "
    "(ぼう in ぼうぎょ, じ in じばく, こう in とくこう), or when it is a stutter, a cry or sound effect, or "
    "a piece of a fixed expression such as について.\n"
    "- Grammar words count: choose the particle, auxiliary or copula entry when the marked text is one (で "
    "as a particle, not 出; よう in ようだ, not 用).\n"
    "- Use only ids from that word's list. The English line is a translation, not a gloss: use it for "
    "meaning, not word for word."
)

SCHEMA = {
    "type": "object", "additionalProperties": False, "required": ["lines"],
    "properties": {"lines": {"type": "array", "items": {
        "type": "object", "additionalProperties": False, "required": ["line", "units"],
        "properties": {
            "line": {"type": "integer"},
            "units": {"type": "array", "items": {
                "type": "object", "additionalProperties": False, "required": ["unit", "choice"],
                "properties": {"unit": {"type": "integer"}, "choice": {"type": "integer"}}}}}}}},
}


@dataclass(frozen=True)
class AskUnit:
    index: int              # the unit's position in its line
    surface: str
    lemma: str
    candidates: tuple       # jmdict.Entry, best ranked first
    marked: str = ""        # the line with this word between 【 and 】


@dataclass(frozen=True)
class Ask:
    key: str
    kanji: str
    kana: str
    english: str
    before: str
    units: tuple


def _candidates(unit, index) -> list:
    """The entries the resolver chooses among: Sudachi's normalized form first, else the lemma."""
    found = []
    if unit.normalized and unit.normalized != unit.lemma:
        found = index.candidates(unit.normalized, unit.reading, unit.pos, limit=MAX_CANDIDATES)
    return found or index.candidates(unit.lemma, unit.reading, unit.pos, limit=MAX_CANDIDATES)


def line_key(kanji_text: str, asked: list) -> str:
    digest = hashlib.sha256(kanji_text.encode())
    for unit in asked:
        digest.update(f"|{unit.index}:{unit.lemma}:{','.join(e.id for e in unit.candidates)}".encode())
    return digest.hexdigest()[:24]


def marked(runs: list, start: int, end: int) -> str:
    """The line's text (runs from lines.text_runs) with the span [start, end) between 【 and 】."""
    out = []
    for offset, text in runs:
        for k, ch in enumerate(text):
            at = offset + k
            out.append(("【" if at == start else "") + ch + ("】" if at == end - 1 else ""))
    return "".join(out).replace("\n", " ")


def _suspect(unit) -> bool:
    """A short kana word may be a piece of a longer word even with one candidate: ask about it too."""
    return len(unit.surface) <= 2 and all("぀" <= ch <= "ヿ" for ch in unit.surface)


def asks_for_line(units, runs: list, kana_text: str, english: str, before: str, index) -> Ask | None:
    """runs: the kanji line's text runs (lines.text_runs), whose offsets the units' start and end use."""
    kanji_text = "".join(text for _offset, text in runs)
    asked = []
    for i, unit in enumerate(units):
        if unit.kind != "word":
            continue
        found = _candidates(unit, index)
        if len({entry.id for entry in found}) >= 2 or (found and _suspect(unit)):
            asked.append(AskUnit(i, unit.surface, unit.lemma, tuple(found), marked(runs, unit.start, unit.end)))
    if not asked:
        return None
    return Ask(line_key(kanji_text, asked), kanji_text, kana_text, english, before, tuple(asked))


class Decisions:
    """build/cache/decisions.json: line key -> {unit index: JMdict id (0: none)}, and a batch in flight."""

    def __init__(self, path: Path, model: str = claude.MODEL):
        self.path, self.model = path, model
        self.lines: dict[str, dict[str, int]] = {}
        self.tries: dict[str, int] = {}       # line key -> times asked without a usable answer
        self.pending: dict | None = None
        if path.exists():
            data = json.loads(path.read_text())
            if data.get("version") == PROMPT_VERSION and data.get("model") == model:
                self.lines = data.get("lines", {})
                self.tries = data.get("tries", {})
                self.pending = data.get("pending")

    def given_up(self, key: str) -> bool:
        return self.get(key) is None and self.tries.get(key, 0) >= MAX_TRIES

    def get(self, key: str) -> dict[int, int] | None:
        found = self.lines.get(key)
        return None if found is None else {int(k): v for k, v in found.items()}

    def put(self, key: str, choices: dict[int, int]) -> None:
        self.lines[key] = {str(k): v for k, v in choices.items()}

    @property
    def pending_batch(self) -> str | None:
        return self.pending["batch"] if self.pending else None

    @property
    def pending_requests(self) -> dict[str, list[str]]:
        """custom_id -> the line keys of that request, in order: answers are matched to lines by key."""
        return dict(self.pending.get("requests", {})) if self.pending else {}

    def set_pending(self, batch_id: str | None, requests: dict[str, list[str]] | None = None) -> None:
        self.pending = {"batch": batch_id, "requests": requests or {}} if batch_id else None

    def digest(self) -> str:
        return hashlib.sha256(json.dumps(self.lines, sort_keys=True).encode()).hexdigest()[:16]

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temp = self.path.with_suffix(".tmp")
        temp.write_text(json.dumps({"version": PROMPT_VERSION, "model": self.model, "lines": self.lines,
                                    "tries": self.tries, "pending": self.pending}, ensure_ascii=False, sort_keys=True))
        temp.replace(self.path)


def _describe(entry) -> str:
    forms = "・".join(dict.fromkeys(entry.kanji + entry.kana))
    return f"  {entry.id}: {forms} [{', '.join(entry.pos)}] {' / '.join(entry.senses[:2])}"


def user_text(asks: list) -> str:
    parts = []
    for n, ask in enumerate(asks):
        parts.append(f"Line {n}\nBefore: {ask.before}\nKanji: {ask.kanji}\nKana: {ask.kana}\nEnglish: {ask.english}")
        for unit in ask.units:
            parts.append(f"Word {unit.index}: {unit.marked or unit.surface} (dictionary form {unit.lemma}):")
            parts += [_describe(entry) for entry in unit.candidates]
        parts.append("")
    return "\n".join(parts)


def request_for(asks: list, model: str) -> dict:
    custom_id = hashlib.sha256("|".join(a.key for a in asks).encode()).hexdigest()[:32]
    units = sum(len(a.units) for a in asks)
    return {"custom_id": custom_id,
            "params": claude.params(model, SYSTEM, user_text(asks), SCHEMA, max_tokens=THINKING_TOKENS + 25 * units,
                                    effort=EFFORT)}


def apply_answer(asks: list, answer: dict) -> tuple[dict[str, dict[int, int]], int]:
    """Each line's choices, keeping only units that were asked about and ids that were offered (0: none)."""
    choices: dict[str, dict[int, int]] = {}
    ignored = 0
    for line in answer.get("lines", []):
        n = line.get("line")
        if not isinstance(n, int) or not 0 <= n < len(asks) or asks[n] is None:   # None: a line no longer asked
            ignored += len(line.get("units", []))
            continue
        ask = asks[n]
        offered = {unit.index: {int(e.id) for e in unit.candidates} for unit in ask.units}
        for pick in line.get("units", []):
            unit, choice = pick.get("unit"), pick.get("choice")
            if unit in offered and (choice == 0 or choice in offered[unit]):
                choices.setdefault(ask.key, {})[unit] = choice
            else:
                ignored += 1
    return choices, ignored


def _estimate(requests: list) -> float:
    """Input tokens counted generously: about one per Japanese character, one per four Latin ones."""
    tokens_in = tokens_out = 0
    for request in requests:
        text = request["params"]["system"] + request["params"]["messages"][0]["content"]
        tokens_in += sum(1 if ord(ch) > 0x2E80 else 0.25 for ch in text)
        tokens_out += request["params"]["max_tokens"]
    return claude.estimate_usd(int(tokens_in), tokens_out, batch=True)


def _collect(batch, batch_id: str, requests: dict[str, list[str]], by_key: dict, decisions: Decisions,
             summary: dict, log, poll_s: float) -> None:
    """Waits for a batch and keeps every answer whose line is still asked, matched by line key."""
    failures = 0
    while True:
        try:
            status = batch.status(batch_id)
            failures = 0
        except claude.ClaudeError as error:
            if " 404" in str(error):
                raise
            failures += 1
            if failures > 5:
                raise
            status = f"unreachable ({error})"
        except OSError as error:
            failures += 1
            if failures > 5:
                raise
            status = f"unreachable ({error})"
        if status == "ended":
            break
        log(f"decisions: batch {batch_id} {status}")
        time.sleep(poll_s * (1 + failures))
    results = batch.results(batch_id)
    for custom_id, keys in requests.items():
        answer = results.get(custom_id)                   # None: errored or expired
        choices: dict = {}
        if answer is not None:
            choices, ignored = apply_answer([by_key.get(key) for key in keys], answer)
            summary["ignored"] += ignored
        for key, picked in choices.items():
            if decisions.get(key) is None:
                summary["decided"] += 1
            decisions.put(key, picked)
        for key in keys:
            if key in by_key and key not in choices and decisions.get(key) is None:
                decisions.tries[key] = decisions.tries.get(key, 0) + 1
    decisions.set_pending(None)
    decisions.save()


def run(asks: list, decisions: Decisions, key: str | None, budget_usd: float, folder: Path, log,
        model: str = claude.MODEL, poll_s: float = 60) -> dict:
    """Decides every ask not yet in `decisions`: first collects a batch an earlier run left pending, then
    sends one batch for what is still undecided. Identical lines are asked once. Network and API failures
    are logged (never the key) and leave the lines undecided; the build carries on with ranking."""
    by_key = {ask.key: ask for ask in asks}               # identical lines share a key: asked once
    summary = {"asked": len(by_key), "cached": 0, "decided": 0, "undecided": 0, "ignored": 0,
               "cost_estimate_usd": 0.0, "batch_id": None}
    summary["cached"] = sum(decisions.get(k) is not None for k in by_key)
    if key:
        batch = None
        try:
            if decisions.pending_batch:
                batch = claude.Batch(key, folder)
                log(f"decisions: collecting batch {decisions.pending_batch} from an earlier run")
                summary["batch_id"] = decisions.pending_batch
                try:
                    _collect(batch, decisions.pending_batch, decisions.pending_requests, by_key, decisions,
                             summary, log, poll_s)
                except claude.ClaudeError as error:
                    if " 404" not in str(error):
                        raise
                    log(f"decisions: batch {decisions.pending_batch} is gone ({error}); asking again")
                    decisions.set_pending(None)
                    decisions.save()
            todo = [ask for k, ask in by_key.items() if decisions.get(k) is None and not decisions.given_up(k)]
            if todo:
                chunks = [todo[n:n + CHUNK] for n in range(0, len(todo), CHUNK)]
                requests = [request_for(chunk, model) for chunk in chunks]
                summary["cost_estimate_usd"] = estimate = _estimate(requests)
                if estimate > budget_usd:
                    log(f"decisions: estimate ${estimate:.2f} is over the ${budget_usd:.2f} budget; not asking")
                else:
                    batch = batch or claude.Batch(key, folder)
                    batch_id = batch.submit(requests)
                    request_map = {r["custom_id"]: [a.key for a in chunk] for r, chunk in zip(requests, chunks)}
                    decisions.set_pending(batch_id, request_map)
                    decisions.save()
                    summary["batch_id"] = batch_id
                    log(f"decisions: batch {batch_id} sent, {len(requests)} requests, estimate ${estimate:.2f}")
                    _collect(batch, batch_id, request_map, by_key, decisions, summary, log, poll_s)
        except (claude.ClaudeError, OSError) as error:
            log(f"decisions: Claude could not be asked ({error}); undecided words use ranking")
    summary["given_up"] = sum(decisions.given_up(k) for k in by_key)
    summary["undecided"] = sum(decisions.get(k) is None for k in by_key) - summary["given_up"]
    return summary
