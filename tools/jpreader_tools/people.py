"""What each character model looks like: a label, a gender and an age band, for picking voices.

Field models (a/2/0/0, indexed by the model number NPC placement records use) are named
internally: trNNNN people, pmNNNN Pokémon, obNNNN and itNNNN objects. A person model gets
its gender and look from the trainer classes that use it (a/1/0/5: gender byte, overworld
model); townsfolk models no class uses are described in TOWNSFOLK, judged from how their
lines talk in Japanese and from developer labels left in the scripts.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

from .rom import parse_garc
from .text import load_archive, render

FIELD_MODELS = "a/2/0/0"
TRAINER_CLASSES = "a/1/0/5"
SYSTEM_EN = 32
CLASS_NAMES = 111                     # system text file of trainer class names
SPECIES_NAMES = 60                    # system text file of species names, by national dex number
LAST_SPECIES = 807

MALE, FEMALE = "male", "female"
CHILD, TEEN, YOUNG, ADULT, OLDER, ELDERLY = "child", "teen", "young adult", "adult", "older adult", "elderly"


@dataclass(frozen=True)
class Person:
    label: str                        # "Youngster", "old woman", "Pikachu", ...
    kind: str                         # "npc", "pokemon" or "object"
    gender: str = ""                  # "male", "female" or "" when unknown
    age: str = ""


CLASS_AGES = {
    "Preschooler": CHILD, "Twins": CHILD,
    "Youngster": TEEN, "Lass": TEEN, "Youth Athlete": TEEN, "Captain": TEEN, "Pokémon Trainer": TEEN,
    "Ace Trainer": YOUNG, "Rising Star": YOUNG, "Swimmer": YOUNG, "Backpacker": YOUNG, "Punk Guy": YOUNG,
    "Punk Girl": YOUNG, "Team Skull": YOUNG, "Dancer": YOUNG, "Surfer": YOUNG, "Beauty": YOUNG,
    "Veteran": OLDER, "Gentleman": OLDER, "Madame": OLDER, "Principal": OLDER,
}

# Townsfolk models no trainer class uses: (label, gender, age), from their lines' speech and
# the scripts' developer labels (CHR_BOY, CHR_ROUJIN, ...)
TOWNSFOLK = {
    49: ("boy", MALE, CHILD),
    50: ("girl", FEMALE, CHILD),
    51: ("child", "", CHILD),
    52: ("woman", FEMALE, ADULT),
    53: ("young man", MALE, YOUNG),               # ぼく
    54: ("woman", FEMALE, ADULT),                 # CHR_ADULT_F
    55: ("woman", FEMALE, ADULT),                 # あたし, 〜わ; shop and café staff
    56: ("man", MALE, ADULT),                     # a father ("Look, Son!")
    57: ("middle-aged man", MALE, OLDER),         # わし…オヤジ
    58: ("middle-aged woman", FEMALE, OLDER),     # おばさん
    59: ("older woman", FEMALE, OLDER),           # アメちゃん あげるわ
    60: ("old man", MALE, ELDERLY),               # わし…じゃ, CHR_ROUJIN
    61: ("man", MALE, ADULT),                     # shopkeepers, CHR_MAN
    62: ("old woman", FEMALE, ELDERLY),           # "this old woman"
    63: ("young man", MALE, YOUNG),               # オレ
    64: ("food vendor", "", ADULT),
    65: ("receptionist", "", ADULT),
    184: ("delivery person", "", ADULT),
    185: ("woman", FEMALE, ADULT),                # あたし
    197: ("old woman", FEMALE, ELDERLY),          # a widow
    258: ("young man", MALE, YOUNG),              # 〜っす
}

# Named characters (speakers.TAG_NAMES): (gender, age)
CHARACTERS = {
    "Mom": (FEMALE, ADULT), "Professor Kukui": (MALE, ADULT), "Lillie": (FEMALE, TEEN), "Hau": (MALE, TEEN),
    "Hala": (MALE, ELDERLY), "Olivia": (FEMALE, ADULT), "Nanu": (MALE, OLDER), "Hapu": (FEMALE, TEEN),
    "Ilima": (MALE, TEEN), "Lana": (FEMALE, TEEN), "Mallow": (FEMALE, TEEN), "Kiawe": (MALE, TEEN),
    "Sophocles": (MALE, TEEN), "Acerola": (FEMALE, TEEN), "Mina": (FEMALE, YOUNG), "Gladion": (MALE, TEEN),
    "Lusamine": (FEMALE, ADULT), "Faba": (MALE, ADULT), "Wicke": (FEMALE, ADULT), "Guzma": (MALE, YOUNG),
    "Plumeria": (FEMALE, YOUNG), "Kahili": (FEMALE, ADULT), "Ryuki": (MALE, ADULT),
    "Professor Burnet": (FEMALE, ADULT), "Molayne": (MALE, YOUNG), "Red": (MALE, YOUNG), "Blue": (MALE, YOUNG),
    "Looker": (MALE, ADULT), "Giovanni": (MALE, OLDER), "Maxie": (MALE, ADULT), "Archie": (MALE, ADULT),
    "Cyrus": (MALE, ADULT), "Ghetsis": (MALE, OLDER), "Lysandre": (MALE, ADULT), "Sina": (FEMALE, YOUNG),
    "Dexio": (MALE, YOUNG), "Grimsley": (MALE, ADULT), "Anabel": (FEMALE, YOUNG), "Colress": (MALE, ADULT),
    "Masked Royal": (MALE, ADULT), "Mohn": (MALE, ADULT), "Team Skull Grunt": ("", YOUNG),
    "Samson Oak": (MALE, OLDER), "Dulse": (MALE, ADULT), "Zossie": (FEMALE, CHILD), "Soliera": (FEMALE, ADULT),
    "Phyco": (MALE, OLDER), "Reporter": (FEMALE, ADULT), "Madame Meowth": (FEMALE, OLDER),
}
POKEMON = {"Rotom Pokédex", "Togedemaru", "Pikachu", "Bewear", "Pyukumuku"}


def model_names(rom) -> list[str | None]:
    """Internal name of each field model (tr0003_00, pm0025_00, ...), or None."""
    names = []
    for blob in parse_garc(rom.romfs_file(FIELD_MODELS)):
        found = re.findall(rb"(?<![a-z])([a-z]{2}\d{4}_\d\d)", blob)
        names.append(max(set(found), key=found.count).decode() if found else None)
    return names


def _system_lines(rom, file: int) -> list[str]:
    return [render(units) for units in load_archive(rom, SYSTEM_EN)[file]]


def models(rom) -> dict[int, Person]:
    """Model number -> Person, for every model that can be described."""
    class_names = _system_lines(rom, CLASS_NAMES)
    species = _system_lines(rom, SPECIES_NAMES)
    out: dict[int, Person] = {}
    for model, (label, gender, age) in TOWNSFOLK.items():
        out[model] = Person(label, "npc", gender, age)
    for index, record in enumerate(parse_garc(rom.romfs_file(TRAINER_CLASSES))):
        gender = {0: MALE, 1: FEMALE}.get(record[0], "")
        model = struct.unpack_from("<H", record, 8)[0]
        name = class_names[index]
        if model != 0xFFFF and model not in out:
            out[model] = Person(name, "npc", gender, CLASS_AGES.get(name, ADULT))
    for model, name in enumerate(model_names(rom)):
        if model in out or not name:
            continue
        number = int(name[2:6])
        if name.startswith("pm"):
            label = species[number] if 0 < number <= LAST_SPECIES else "Pokémon"
            out[model] = Person(label, "pokemon")
        elif name.startswith(("ob", "it")):
            out[model] = Person("object", "object")
    return out
