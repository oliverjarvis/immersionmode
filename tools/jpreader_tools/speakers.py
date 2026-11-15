"""What each story line is (dialogue, narration, a menu choice, ...) and who says it.

Speakers come from two sources, most trusted first:

1. The line itself. Many lines end in a {0114:XXXX} tag holding the speaking
   character's ID (one tag at most per line). TAG_NAMES names the IDs, worked
   out from what the tagged lines say.
2. The scripts. Every dialog call names a speaker object: an object number,
   -1 for the NPC being talked to, or 0 for a box with no speaker (narration
   and system messages). An area's NPC placement records give each object's
   character model, and a model is named after the tag its lines carry; other
   models are described by people.py (a gender and an age for picking voices).

Which procedures show a message is worked out from the scripts: a procedure parameter
carries a line number when the procedure passes it on into the game's message native
(or into a procedure that does), and likewise for the speaker and for menu choices.
"""
from __future__ import annotations

import re
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass, replace

from . import amx, people, scripts
from .packs import mini
from .rom import parse_garc
from .text import Tag, parse_segments

SPEAKER_TAG = 0x0114

TAG_NAMES = {
    0x04: "Mom", 0x05: "Professor Kukui", 0x06: "Lillie", 0x07: "Hau", 0x08: "Hala", 0x09: "Olivia",
    0x0A: "Nanu", 0x0B: "Hapu", 0x0C: "Ilima", 0x0D: "Lana", 0x0E: "Mallow", 0x0F: "Kiawe",
    0x10: "Sophocles", 0x11: "Acerola", 0x12: "Mina", 0x13: "Gladion", 0x14: "Lusamine", 0x15: "Faba",
    0x16: "Wicke", 0x17: "Guzma", 0x18: "Plumeria", 0x19: "Kahili", 0x1A: "Ryuki",
    0x1B: "Professor Burnet", 0x1C: "Molayne", 0x1D: "Red", 0x1E: "Blue", 0x1F: "Looker",
    0x20: "Giovanni", 0x21: "Maxie", 0x22: "Archie", 0x23: "Cyrus", 0x24: "Ghetsis", 0x25: "Lysandre",
    0x26: "Sina", 0x27: "Dexio", 0x28: "Grimsley", 0x29: "Anabel", 0x2B: "Colress", 0x2E: "Masked Royal",
    0x2F: "Mohn", 0x31: "Team Skull Grunt", 0x32: "Team Skull Grunt", 0x34: "Samson Oak",
    0x35: "Rotom Pokédex", 0x36: "Dulse", 0x37: "Zossie", 0x38: "Soliera", 0x39: "Phyco",
    0x3A: "Togedemaru", 0x43: "Pikachu", 0x47: "Bewear", 0x4A: "Pyukumuku", 0x4D: "Reporter",
    0x4F: "Madame Meowth",
}

# Tags seen on many unrelated speakers' lines: they say nothing about who is speaking
SHARED_TAGS = {0x30, 0x33}

MESSAGE_NATIVE = "n:fd1cf919"         # (0, speaker, line, window, ...)
CHOICE_NATIVE = "n:7903fd3e"          # adds one choice to a menu: (line, ...)
TWO_CHOICES = "f8f69d9b82"            # (first choice, second choice, ...)
GENDERED_PAIR = "f1da5dcb96"          # (line for a male speaker, the same line for a female one)
FILE_KINDS = {23: "sign", 736: "help"}
# Japanese first-person pronouns that give a speaker's gender away (right on 476 of 482 lines
# whose speaker is known)
PRONOUNS = ((people.MALE, re.compile("おれ|オレ|俺|ぼく|ボク|僕")), (people.FEMALE, re.compile("あたし|アタシ")))   # files the game's own code shows: route signs, tutorial tips

NPC_RECORDS = 1                       # EM entity list holding NPCs (0x78-byte records)
NPC_RECORD = 0x78
FIRST_EVENT_ID = 5000                 # IDs below belong to event file 0, used everywhere



@dataclass(frozen=True)
class LineInfo:
    kind: str                         # dialogue, narration, choice, message, sign, help, empty, unknown
    speaker: str = ""                 # a character's name, or what an unnamed speaker looks like
    speaker_type: str = ""            # character, npc, pokemon or object
    gender: str = ""
    age: str = ""
    source: str = ""                  # text tag, script or file
    tag: int | None = None
    model: int | None = None


@dataclass(frozen=True)
class Npc:
    obj: int                          # object number dialog calls use
    model: int                        # character model
    script: int                       # script ID run when talked to (0: none)


@dataclass(frozen=True)
class Usage:
    how: str                          # "message", "choice", "male" or "female"
    speaker: int | None = None        # for a message: object number, -1, 0, or None when computed
    script_ids: frozenset[int] = frozenset()


@dataclass
class _Unit:
    text_file: int
    areas: list[int]
    blob: bytes
    procs: dict
    calls: list


def speaker_tag(units) -> int | None:
    for segment in parse_segments(units):
        if isinstance(segment, Tag) and segment.code == SPEAKER_TAG and segment.args:
            return segment.args[0]
    return None


def npcs(entity_files: list[bytes]) -> list[Npc]:
    found = []
    for blob in entity_files:
        if blob[:2] != b"EM":
            continue
        body = mini(blob)[1][0]
        if len(body) < 8:
            continue
        count, kind = struct.unpack_from("<2I", body, 0)
        if kind != NPC_RECORDS:
            continue
        for i in range(count):
            start = 8 + i * NPC_RECORD
            if start + NPC_RECORD <= len(body):
                obj, model, script = (struct.unpack_from("<I", body, start + o)[0] for o in (0x28, 0x30, 0x34))
                found.append(Npc(obj, model, script))
    return found


def _cases(entry: amx.Procedure) -> dict[int, set[int]]:
    """Script ID -> procedures the entry point's switch calls for it."""
    ins = entry.instructions
    index = {i.address: k for k, i in enumerate(ins)}
    cases: dict[int, set[int]] = {}
    for table in (i for i in ins if i.op == amx.OP["CASETBL"]):
        for k in range(table.args[0]):
            value, offset = table.args[2 + 2 * k], table.args[3 + 2 * k]
            j = index.get(table.address + 4 * (3 + 2 * k) + offset)   # relative to the value cell
            called = set()
            while j is not None and j < len(ins) and ins[j].name not in ("JUMP", "RETN", "CASETBL"):
                if ins[j].name == "CALL":
                    called.add(ins[j].address + ins[j].args[0])
                j += 1
            cases[value] = called
    return cases


def _callee(call: amx.Call, procs) -> str | None:
    if call.native is not None:
        return f"n:{call.native:08x}"
    return procs[call.target].signature if call.target in procs else None


@dataclass
class Roles:
    message: set                      # (callee, argument index) carrying a line to show
    speaker: set                      # ... carrying that message's speaker
    choice: set                       # ... carrying a menu choice
    fixed: dict                       # message procedure without a speaker argument -> the speaker it uses


def roles(units: list[_Unit]) -> Roles:
    flows: dict[tuple[str, int], set] = defaultdict(set)   # (procedure, parameter) -> {(callee, argument)}
    sites = []
    for u in units:
        for c in u.calls:
            callee = _callee(c, u.procs)
            caller = u.procs[c.caller].signature
            sites.append((caller, callee, c))
            for pos, param in enumerate(c.params):
                if param is not None:
                    flows[(caller, param)].add((callee, pos))

    def closure(seed):
        found, changed = set(seed), True
        while changed:
            changed = False
            for key, outs in flows.items():
                if key not in found and outs & found:
                    found.add(key)
                    changed = True
        return found

    message = closure({(MESSAGE_NATIVE, 2)})
    speaker = closure({(MESSAGE_NATIVE, 1)})
    choice = closure({(CHOICE_NATIVE, 0), (TWO_CHOICES, 0), (TWO_CHOICES, 1)})
    fixed: dict[str, int] = {}
    for _ in range(6):
        for caller, callee, c in sites:
            if caller in fixed or not any((caller, p) in message and (callee, pos) in message
                                          for pos, p in enumerate(c.params) if p is not None):
                continue
            spos = [pos for pos in range(len(c.args)) if (callee, pos) in speaker]
            if spos and c.args[spos[0]] is not None:
                fixed[caller] = c.args[spos[0]]
            elif not spos and callee in fixed:
                fixed[caller] = fixed[callee]
    return Roles(message, speaker, choice, fixed)


def _reached_by(u: _Unit) -> dict[int, set[int]]:
    """Procedure -> script IDs whose entry-point case reaches it."""
    edges = defaultdict(set)
    for c in u.calls:
        if c.target is not None:
            edges[c.caller].add(c.target)
    reached: dict[int, set[int]] = defaultdict(set)
    entry = u.procs.get(struct.unpack_from("<I", u.blob, 0x1C)[0])
    for script_id, roots in (_cases(entry) if entry else {}).items():
        todo, seen = list(roots), set()
        while todo:
            p = todo.pop()
            if p not in seen:
                seen.add(p)
                reached[p].add(script_id)
                todo.extend(edges[p])
    return reached


def usages(u: _Unit, r: Roles, line_count: int) -> list[tuple[int, Usage]]:
    """(line, Usage) for every line of the unit's text file one of its calls shows."""
    reached = _reached_by(u)
    found = []
    for c in u.calls:
        callee = _callee(c, u.procs)
        valid = [isinstance(a, int) and 0 <= a < line_count for a in c.args]
        if callee == GENDERED_PAIR:
            for pos, how in ((0, "male"), (1, "female")):
                if pos < len(c.args) and valid[pos]:
                    found.append((c.args[pos], Usage(how)))
            continue
        for pos, value in enumerate(c.args):
            if not valid[pos]:
                continue
            if (callee, pos) in r.message:
                spos = [j for j in range(len(c.args)) if (callee, j) in r.speaker]
                speaker = c.args[spos[0]] if spos else r.fixed.get(callee)
                found.append((value, Usage("message", speaker, frozenset(reached[c.caller]))))
            elif (callee, pos) in r.choice:
                found.append((value, Usage("choice")))
    return found


def _event_areas(table, packs, event_blobs) -> dict[int, set[int]]:
    """Areas each event script file runs in: the areas whose entity data or zone scripts name
    one of its script IDs, then the areas of the event scripts that name it."""
    def file_of(value):
        if FIRST_EVENT_ID <= value <= table_end:
            e = scripts.script_for_id(table, value)
            return e.script_file if e else None
        return None

    table_end = max(e.last_id for e in table)
    areas: dict[int, set[int]] = defaultdict(set)
    for area, (entities, zone_scripts) in enumerate(packs):
        for blob in entities:
            body = mini(blob)[1][0]
            for (value,) in struct.iter_unpack("<I", body[:len(body) // 4 * 4]):
                if (f := file_of(value)) is not None:
                    areas[f].add(area)
        for blob in zone_scripts:
            for f in _named_files(blob, file_of):
                areas[f].add(area)
    callers = {f: _named_files(blob, file_of) for f, blob in enumerate(event_blobs)}
    for _ in range(3):
        for caller, named in callers.items():
            for f in named:
                if not areas[f]:
                    areas[f] |= areas[caller]
    return areas


def _named_files(blob, file_of) -> set[int]:
    try:
        script = amx.parse(blob)
    except amx.AmxError:
        return set()
    return {f for c in amx.calls(script, amx.procedures(script)) for a in c.args
            if isinstance(a, int) and (f := file_of(a)) is not None}


def _models(usage: Usage, area_npcs: list[list[Npc]], areas) -> set[int]:
    """Models the usage's speaker can be. A computed speaker (None) is taken to be the NPC
    whose talk script the call is in, as it usually is; _line marks those as guesses."""
    talked_to = usage.speaker in (-1, None)
    models = set()
    for area in areas:
        for npc in area_npcs[area]:
            if (talked_to and npc.script in usage.script_ids) or npc.obj == usage.speaker:
                models.add(npc.model)
    return models


def _units(rom) -> tuple[list[_Unit], list[list[Npc]]]:
    table = scripts.event_scripts(rom)
    zone_text = scripts.zone_text_files(rom)
    packs = scripts.area_packs(rom)
    area_zone = scripts.area_zones(rom)
    event_blobs = [scripts.unpack(b) for b in parse_garc(rom.romfs_file(scripts.EVENT_SCRIPTS))]
    event_areas = _event_areas(table, packs, event_blobs)
    text_of = {e.script_file: e.text_file for e in table}
    sources = [(text_of[f], sorted(event_areas[f]), blob) for f, blob in enumerate(event_blobs) if f in text_of]
    sources += [(zone_text[zone], [area], blob) for area, (_e, zone_scripts) in enumerate(packs)
                for zone, blob in zip(area_zone.get(area, []), zone_scripts)]
    units = []
    for text_file, areas, blob in sources:
        try:
            script = amx.parse(blob)
        except amx.AmxError:
            continue
        procs = amx.procedures(script)
        units.append(_Unit(text_file, areas, blob, procs, amx.calls(script, procs)))
    return units, [npcs(entities) for entities, _ in packs]


def describe(rom, story: list[list[list[int]]], japanese=None) -> dict[tuple[int, int], LineInfo]:
    """(story file, line) -> LineInfo for every story line.

    `story` is a story text archive's lines (any language: the tags are the same). With the
    Japanese archive too, a line still without a gendered speaker is checked for pronouns."""
    units, area_npcs = _units(rom)
    r = roles(units)
    found: dict[tuple[int, int], list] = defaultdict(list)       # line -> [(Usage, models)]
    for u in units:
        for line, usage in usages(u, r, len(story[u.text_file])):
            models = _models(usage, area_npcs, u.areas) if usage.how == "message" and usage.speaker != 0 else set()
            found[(u.text_file, line)].append((usage, models))

    tags = {(f, i): speaker_tag(units_) for f, lines in enumerate(story) for i, units_ in enumerate(lines)}
    named_models = _model_names(found, tags)
    looks = people.models(rom)
    result = {}
    for f, lines in enumerate(story):
        for i, units_ in enumerate(lines):
            key = (f, i)
            info = _line(units_, f, tags[key], found.get(key, []), named_models, looks)
            if japanese is not None and not info.gender and info.kind in ("dialogue", "message", "unknown"):
                info = _by_pronoun(info, japanese[f][i])
            result[key] = info
    return result


def _by_pronoun(info: LineInfo, units_) -> LineInfo:
    text = "".join(chr(u) for seg in parse_segments(units_) if not isinstance(seg, Tag) for u in seg.units)
    genders = {gender for gender, pattern in PRONOUNS if pattern.search(text)}
    if len(genders) != 1:
        return info
    return replace(info, kind="dialogue", gender=genders.pop(),
                   source=info.source if info.speaker else "speech style")


def _character(name: str, source: str, tag, model=None) -> LineInfo:
    gender, age = people.CHARACTERS.get(name, ("", ""))
    kind = "pokemon" if name in people.POKEMON else "character"
    return LineInfo("dialogue", name, kind, gender, age, source, tag, model)


def _line(units_, file: int, tag, uses, named_models, looks) -> LineInfo:
    text = "".join(chr(u) for seg in parse_segments(units_) if not isinstance(seg, Tag) for u in seg.units)
    if not text.strip():
        return LineInfo("empty", tag=tag)
    if tag in TAG_NAMES:
        return _character(TAG_NAMES[tag], "text tag", tag)
    kinds = Counter()
    guessed = False
    for usage, _models_ in uses:
        if usage.how == "choice":
            kinds["choice"] += 1
        elif usage.how in ("male", "female"):
            kinds["dialogue"] += 1
        elif usage.speaker == 0:
            kinds["narration"] += 1
        elif usage.speaker is not None:
            kinds["dialogue"] += 1
        elif _models_:
            kinds["dialogue"] += 1          # in an NPC's talk script: most likely that NPC
            guessed = True
        else:
            kinds["message"] += 1
    if tag is not None:
        kinds["dialogue"] += len(uses) + 1      # tagged lines are spoken
    if not kinds:
        kind = FILE_KINDS.get(file, "unknown")
        return LineInfo(kind, source="file" if file in FILE_KINDS else "", tag=tag)
    decided = [k for k in ("dialogue", "narration", "choice") if kinds[k]]
    kind = max(decided, key=lambda k: kinds[k]) if decided else "message"
    if kind != "dialogue":
        return LineInfo(kind, source="script", tag=tag)
    source = "script (talk target guessed)" if guessed and not any(
        u.speaker not in (None, 0) for u, _ in uses if u.how == "message") else "script"
    genders = {u.how for u, _ in uses if u.how in ("male", "female")}
    models = Counter(next(iter(m)) for _, m in uses if len(m) == 1)
    if not models:
        gender = genders.pop() if len(genders) == 1 else ""
        return LineInfo("dialogue", gender=gender, source=source, tag=tag)
    model, _ = models.most_common(1)[0]
    shared_or_none = tag is None or tag in SHARED_TAGS     # an unnamed character's tag rules out named ones
    if model in named_models:
        if shared_or_none:
            info = _character(named_models[model], source, tag, model)
            look = looks.get(model)
            return replace(info, gender=look.gender) if look and not info.gender else info
        return LineInfo("dialogue", source=source, tag=tag, model=model)   # someone else, unnamed
    look = looks.get(model)
    if look is None:
        return LineInfo("dialogue", source=source, tag=tag, model=model)
    return LineInfo("dialogue", look.label, look.kind, look.gender, look.age, source, tag, model)


def _model_names(shown, tags) -> dict[int, str]:
    """Name a model after the character whose tagged lines resolve to it.

    A wrongly placed object can tie a character's lines to a generic model, and generic
    models also stand in for minor characters (a reporter), so a model is only named when
    it is one of that character's main models and most of its own lines carry the tag.
    """
    votes: dict[int, Counter] = defaultdict(Counter)       # model -> name -> tagged lines
    untagged: Counter = Counter()                          # model -> lines without a named tag
    for key, uses in shown.items():
        name = TAG_NAMES.get(tags.get(key))
        for _usage, models in uses:
            if len(models) == 1:
                model = next(iter(models))
                if name:
                    votes[model][name] += 1
                else:
                    untagged[model] += 1
    per_name: dict[str, Counter] = defaultdict(Counter)
    for model, counts in votes.items():
        for name, n in counts.items():
            per_name[name][model] += n
    names = {}
    for model, counts in votes.items():
        name, n = counts.most_common(1)[0]
        if (n >= 3 and n >= 0.6 * sum(counts.values()) and n >= untagged[model]
                and n >= 0.25 * sum(per_name[name].values())):
            names[model] = name
    return names
