from jpreader_tools.analyze import Unit
from jpreader_tools.glossary import merge_names


def unit(start, surface, kind="word"):
    return Unit(start, start + len(surface), kind, surface, surface, "", "noun")


GLOSSARY = {"ポケモンセンター": "Pokémon Center", "ナマコブシ": "Pyukumuku", "ポケモン": "Pokémon"}


def test_longest_run_becomes_a_name():
    units = [unit(0, "ポケモン"), unit(4, "センター"), unit(8, "へ", "particle")]
    merged = merge_names(units, GLOSSARY)
    assert [(u.surface, u.kind, u.start, u.end) for u in merged] == [
        ("ポケモンセンター", "name", 0, 8), ("へ", "particle", 8, 9)]


def test_single_name_and_non_names():
    units = [unit(0, "ナマコブシ"), unit(5, "を", "particle"), unit(7, "センター")]
    merged = merge_names(units, GLOSSARY)
    assert [(u.surface, u.kind) for u in merged] == [("ナマコブシ", "name"), ("を", "particle"), ("センター", "word")]


def test_runs_do_not_cross_gaps():
    units = [unit(0, "ポケモン"), unit(5, "センター")]           # a space between them
    assert [u.surface for u in merge_names(units, GLOSSARY)] == ["ポケモン", "センター"]
    assert merge_names(units, GLOSSARY)[0].kind == "name"


def lines(*texts):
    return [[ord(c) for c in t] for t in texts]


def test_glossary_comes_only_from_name_lists():
    from jpreader_tools.glossary import glossary_from

    japanese = [lines("ア", "イ", "コ"),                                   # the name-entry keyboard
                lines("タマゴ", "フシギダネ", "フシギソウ", "ヨ", "メス"),       # the species list
                lines("ゲーム", "インターネット")]                              # menu labels
    english = [lines("A", "B", "J"),
               lines("Egg", "Bulbasaur", "Ivysaur", "ヨ", "Female"),
               lines("Playing games", "Surfing the Internet")]
    glossary = glossary_from([japanese], english, anchors=("フシギダネ",), min_lines=1)
    assert glossary == {"タマゴ": "Egg", "フシギダネ": "Bulbasaur", "フシギソウ": "Ivysaur", "メス": "Female"}
