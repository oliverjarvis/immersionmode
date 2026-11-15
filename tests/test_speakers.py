import struct

import pytest

from jpreader_tools import speakers
from jpreader_tools.text import TAG_MARK, load_archive


def em_pack(kind: int, records: list[bytes]) -> bytes:
    body = struct.pack("<2I", len(records), kind) + b"".join(records)
    return b"EM" + struct.pack("<H2I", 1, 12, 12 + len(body)) + body


def npc_record(obj: int, model: int, script: int) -> bytes:
    record = bytearray(speakers.NPC_RECORD)
    struct.pack_into("<I", record, 0x28, obj)
    struct.pack_into("<I", record, 0x30, model)
    struct.pack_into("<I", record, 0x34, script)
    return bytes(record)


def test_speaker_tag_is_the_0114_tag():
    assert speakers.speaker_tag([0x48, TAG_MARK, 2, 0x0114, 0x0007, 0x21]) == 7
    assert speakers.speaker_tag([0x48, TAG_MARK, 2, 0x0100, 0x0000]) is None


def test_npcs_come_from_the_npc_list_only():
    files = [em_pack(1, [npc_record(3, 219, 7)]), em_pack(3, [npc_record(9, 9, 9)])]
    assert speakers.npcs(files) == [speakers.Npc(obj=3, model=219, script=7)]


@pytest.fixture(scope="module")
def story_lines(rom):
    return speakers.describe(rom, load_archive(rom, 42), load_archive(rom, 41))


# (story file, line) -> (kind, speaker, gender, source), each checked against what the line says
KNOWN = {
    (31, 0): ("dialogue", "Professor Kukui", "male", "text tag"),
    (27, 0): ("dialogue", "Mom", "female", "text tag"),
    (31, 3): ("dialogue", "Hau", "male", "text tag"),
    (48, 8): ("dialogue", "Lillie", "female", "script"),         # untagged; Lillie's object in the cutscene
    (286, 8): ("dialogue", "Nanu", "male", "script"),
    (283, 22): ("dialogue", "Team Skull Grunt", "male", "script"),
    (1, 0): ("dialogue", "Office Worker", "female", "script"),   # the Battle Tree receptionist's model
    (660, 22): ("dialogue", "", "male", "speech style"),        # a child's tag on Nanu's object; ぼく
    (1060, 813): ("dialogue", "", "male", "script"),            # Festival Plaza guest line, male version
    (1060, 814): ("dialogue", "", "female", "script"),          # ...female version (French: déçu / déçue)
    (442, 18): ("narration", "", "", "script"),
    (489, 29): ("choice", "", "", "script"),
    (23, 0): ("sign", "", "", "file"),
    (736, 0): ("help", "", "", "file"),
    (0, 37): ("empty", "", "", ""),
}


@pytest.mark.rom
def test_known_lines(story_lines):
    got = {k: (story_lines[k].kind, story_lines[k].speaker, story_lines[k].gender, story_lines[k].source)
           for k in KNOWN}
    assert got == KNOWN


@pytest.mark.rom
def test_character_names_stay_on_each_characters_own_models(story_lines):
    models = {}
    for info in story_lines.values():
        if info.source.startswith("script") and info.model is not None and info.speaker_type == "character":
            models.setdefault(info.speaker, set()).add(info.model)
    assert models["Lillie"] <= {2, 3}
    assert models["Team Skull Grunt"] <= {18, 19}
    assert all(len(m) <= 2 for m in models.values())


@pytest.mark.rom
def test_every_story_line_has_a_kind(story_lines):
    assert all(info.kind for info in story_lines.values())
