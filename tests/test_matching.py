"""Spec word identity 9, "Matching": every Kaishi word the game uses is marked on the 3DS when kokoro lists it,
on every game entry of that word and no other. Before stage 9, 109 of them were missed (spelled differently).
tests/data/kaishi-ids.json is kokoro's kaishi/ids.json (keys, JMdict ids and fronts; no game text)."""
import json
from pathlib import Path

import pytest

from jpreader_tools import formats

ROOT = Path(__file__).resolve().parent.parent


ID_LIMIT = 1 << 24      # core/kokoro.h JPR_KOKORO_ID_LIMIT: the parser skips ids at or above it


def marked(ids: list[int], listed: dict[int, int]) -> dict[int, int]:
    """The 3DS's marking by id (core/kokoro.c: the list parser, then jpr_kokoro_mark_ids), in Python:
    entry -> state."""
    table = sorted((word << 2) | state for word, state in listed.items() if word < ID_LIMIT)
    keys = {value & ~3: value & 3 for value in table}
    return {entry: keys[word << 2] for entry, word in enumerate(ids) if word and (word << 2) in keys}


@pytest.mark.rom
def test_every_kaishi_word_the_game_uses_is_marked_on_its_entries_only(rom_path):
    sd = ROOT / "dist" / "azahar-debug" / "sdmc" / "jpreader"
    if not (sd / "ids.bin").exists():
        pytest.skip("no build yet: jpreader build --debug")
    _stamp, ids, _pairs = formats.read_ids((sd / "ids.bin").read_bytes())
    kaishi = {v["id"] for v in json.loads((ROOT / "tests" / "data" / "kaishi-ids.json").read_text()).values() if v["id"]}
    used = kaishi & set(ids)
    assert len(used) > 600                                   # most of Kaishi's 1.5k words occur in the game
    # kokoro's whole Kaishi list at once, as one sync sends it: every game entry of each word, and nothing else.
    got = marked(ids, {word: 3 for word in kaishi})
    want = {entry for entry, word in enumerate(ids) if word in kaishi}
    assert set(got) == want
    # Every id the game uses is one the 3DS keeps: nine are past 2^22, which a first limit lost (stage 9C).
    assert all(word < ID_LIMIT for word in ids) and any(word >= 1 << 22 for word in ids)
