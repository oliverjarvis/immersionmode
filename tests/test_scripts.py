import pytest

from jpreader_tools import scripts

# Seen in Azahar: script ID started -> story text file the message window then loaded
OBSERVED = {0x7558: 27, 0x756C: 29, 0x9100: 28, 0x9178: 36, 0x92F4: 37, 0x9308: 38, 0x9335: 40}


def test_script_for_id_picks_the_covering_range():
    table = [scripts.EventScript(100, 119, 7, 1), scripts.EventScript(120, 139, 9, 2)]
    assert scripts.script_for_id(table, 120).script_file == 2
    assert scripts.script_for_id(table, 119).text_file == 7
    assert scripts.script_for_id(table, 140) is None


@pytest.mark.rom
def test_event_table_matches_what_the_game_loaded(rom):
    table = scripts.event_scripts(rom)
    assert {e.script_file for e in table} == set(range(759))      # file 0 covers two ID ranges
    for script_id, text_file in OBSERVED.items():
        assert scripts.script_for_id(table, script_id).text_file == text_file


@pytest.mark.rom
def test_zone_text_files(rom):
    files = scripts.zone_text_files(rom)
    assert len(files) == 380
    assert files[318] == 1060          # Festival Plaza
    assert all(738 <= f <= 1121 for f in files)


@pytest.mark.rom
def test_every_zone_has_a_zone_script_in_its_area_pack(rom):
    zones = scripts.area_zones(rom)
    packs = scripts.area_packs(rom)
    assert len(packs) == 336
    assert sorted(z for area in zones.values() for z in area) == list(range(380))
    assert all(len(zones.get(a, [])) == len(zone_scripts) for a, (_, zone_scripts) in enumerate(packs))


@pytest.mark.rom
def test_zone_world_area_chain_matches_the_game(rom):
    # Seen in Azahar: in the player's house the game loaded world file 4, then area pack 11
    assert scripts.zone_worlds(rom)[1] == scripts.zone_worlds(rom)[5] == 4
    assert scripts.area_zones(rom)[11] == [1, 5]
