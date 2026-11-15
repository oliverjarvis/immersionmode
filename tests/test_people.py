import pytest

from jpreader_tools import people


@pytest.mark.rom
def test_models_take_gender_and_look_from_trainer_classes(rom):
    models = people.models(rom)
    assert models[67] == people.Person("Youngster", "npc", "male", "teen")
    assert models[68] == people.Person("Lass", "npc", "female", "teen")
    assert models[94] == people.Person("Preschooler", "npc", "male", "child")
    assert models[95] == people.Person("Preschooler", "npc", "female", "child")
    assert models[60] == people.Person("old man", "npc", "male", "elderly")      # townsfolk table
    assert models[113] == people.Person("Pikachu", "pokemon")                     # pm0025
    assert models[121].kind == "object"
