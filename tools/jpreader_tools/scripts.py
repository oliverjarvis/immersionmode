"""Which text file each of the game's scripts reads its messages from.

Event scripts (a/0/9/2) are started by script ID. a/1/4/3 holds one record per
script file: the range of IDs it handles and the story text file it reads.
Zone scripts read the text file named in their zone's record (a/0/7/7).
Both were confirmed in the emulator by logging script starts and text loads.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .packs import mini, unpack
from .rom import parse_garc

EVENT_SCRIPTS = "a/0/9/2"
EVENT_TABLE = "a/1/4/3"
ZONE_TABLE = "a/0/7/7"
ZONE_RECORD = 84


@dataclass(frozen=True)
class EventScript:
    first_id: int
    last_id: int
    text_file: int          # story text archive (a/0/4/x)
    script_file: int        # index into a/0/9/2


def event_scripts(rom) -> list[EventScript]:
    blob = unpack(parse_garc(rom.romfs_file(EVENT_TABLE))[0])
    records = [struct.unpack_from("<5H", blob, o) for o in range(0, len(blob) - 9, 10)]
    return [EventScript(lo, hi, text, script) for lo, hi, _flag, text, script in records]


def script_for_id(table: list[EventScript], script_id: int) -> EventScript | None:
    return next((e for e in table if e.first_id <= script_id <= e.last_id), None)


def zone_text_files(rom) -> list[int]:
    """Story text file of each zone, by zone index."""
    blob = unpack(parse_garc(rom.romfs_file(ZONE_TABLE))[0])
    return [struct.unpack_from("<H", blob, o + 12)[0] for o in range(0, len(blob), ZONE_RECORD)]


WORLD_FILES = "a/0/9/1"
AREA_PACKS = "a/0/8/3"
AREA_PACK_FILES = 11        # files per area: entity data first, zone scripts at +7
ENTITIES, ZONE_SCRIPTS = 0, 7


def zone_worlds(rom) -> list[int]:
    """World of each zone (the zone table's second file)."""
    blob = unpack(parse_garc(rom.romfs_file(ZONE_TABLE))[1])
    return list(struct.unpack(f"<{len(blob) // 2}H", blob))


def world_zones(rom) -> list[list[tuple[int, int]]]:
    """(zone, area) pairs of each world, in the world file's order."""
    worlds = []
    for blob in parse_garc(rom.romfs_file(WORLD_FILES)):
        part = mini(unpack(blob))[1][0]
        offsets = struct.unpack_from("<4I", part, 0)
        worlds.append([struct.unpack_from("<2H", part, o) for o in range(offsets[2], offsets[3], 4)])
    return worlds


def area_zones(rom) -> dict[int, list[int]]:
    """Zones of each area pack, in the order of the pack's zone scripts."""
    areas: dict[int, list[int]] = {}
    for pairs in world_zones(rom):
        for zone, area in pairs:
            areas.setdefault(area, []).append(zone)
    return areas


def area_packs(rom) -> list[tuple[list[bytes], list[bytes]]]:
    """(entity files, zone scripts) of each area pack in a/0/8/3."""
    files = parse_garc(rom.romfs_file(AREA_PACKS))
    return [([part for part in mini(unpack(files[i + ENTITIES]))[1]],
             [unpack(part) for part in mini(unpack(files[i + ZONE_SCRIPTS]))[1]])
            for i in range(0, len(files), AREA_PACK_FILES)]
