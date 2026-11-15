import pytest

from jpreader_tools import inflect


@pytest.fixture(scope="module")
def labels():
    return inflect.inflection_labels()


@pytest.mark.parametrize("endings, expected", [
    (("て", "いる"), "-te iru: ongoing action or state"),
    (("ず",), "negative (-zu): without …ing"),
    (("られる", "ない"), "cannot …"),
    (("たら",), "conditional (-tara): if, when"),
    (("ない", "ば"), "if not …; must (-nakereba)"),
    (("て", "いる", "た"), "-te ita: was …ing"),
    (("ましょう",), "polite volitional: let's …"),
    (("ます", "ぬ"), "polite negative"),
    (("て", "くださる"), "-te kudasai: please …"),
    (("なさる",), "command (-nasai)"),
    (("て", "しまう", "た"), "-te shimatta: ended up …; went and …"),
    (("たい",), "want to …"),
    (("せる",), "causative: make or let …"),
    (("て", "くる", "た"), "-te kita: has been …ing; came to …"),
])
def test_known_chains(labels, endings, expected):
    assert inflect.label(endings, labels) == expected


def test_unknown_or_empty(labels):
    assert inflect.label((), labels) is None
    assert inflect.label(("ぬ", "ぬ", "ぬ"), labels) is None


def test_particle_notes():
    notes = inflect.particle_notes()
    assert notes["が"] == "marks the subject"
    assert notes["を"] == "marks the direct object"
    assert len(notes) >= 40


def test_tsv_ignores_comments_and_blank_lines(tmp_path):
    path = tmp_path / "t.tsv"
    path.write_text("# comment\n\nて\t-te form\n", encoding="utf-8")
    assert inflect.load_tsv(path) == {"て": "-te form"}


def test_potential_labels(labels):
    assert inflect.label(("える",), labels) == "potential: can …"
    assert inflect.label(("える", "ない"), labels) == "cannot …"
    assert inflect.label(("える", "た"), labels) == "could …; was able to …"
