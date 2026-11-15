import pytest

from jpreader_tools import annotate, jmdict
from jpreader_tools.analyze import Analyzer
from jpreader_tools.dictionary import Resolver

ENTRIES = [
    jmdict.Entry("1", ("海",), ("うみ",), True, False, ("n",), ("sea",)),
    jmdict.Entry("2", ("返す",), ("かえす",), True, False, ("v5s",), ("to return",)),
]


def units(s):
    return [ord(c) for c in s]


@pytest.fixture(scope="module")
def annotator():
    resolver = Resolver(jmdict.Index(ENTRIES), {"ナマコブシ": "Pyukumuku"}, {"を": "object", "に": "target"})
    return annotate.Annotator(Analyzer(), resolver, {"ナマコブシ": "Pyukumuku"}, {"たら": "conditional"})


KANJI = units("ナマコブシを\n海に　返したら")
KANA = units("ナマコブシを\nうみに　かえしたら")


def test_kanji_line_record(annotator):
    record, resolved = annotator.record(KANJI, annotator.units(KANJI))
    assert [(u.start, u.length, u.kind) for u in record.units] == [
        (0, 5, "name"), (5, 1, "particle"), (7, 1, "word"), (8, 1, "particle"), (10, 4, "word")]
    assert annotator.labels[record.units[4].label] == "conditional"
    assert annotator.entries[record.units[2].entry].headword == "海"
    assert len(record.segments) == 1 and record.segments[0].length == len(KANJI)


def test_kana_line_shares_entries(annotator):
    kanji_units = annotator.units(KANJI)
    before = annotator.projected
    kana_units = annotator.kana_units(KANA, KANJI, kanji_units)
    assert annotator.projected == before + 1
    assert [u.surface for u in kana_units] == ["ナマコブシ", "を", "うみ", "に", "かえしたら"]
    kanji_record, _ = annotator.record(KANJI, kanji_units)
    kana_record, _ = annotator.record(KANA, kana_units)
    assert [u.entry for u in kana_record.units] == [u.entry for u in kanji_record.units]


def test_kana_line_that_differs_falls_back(annotator):
    before = annotator.fallbacks
    kana = units("ナマコブシを　うみに\nかえしたら")          # a space and a line break swapped
    kana_units = annotator.kana_units(kana, KANJI, annotator.units(KANJI))
    assert annotator.fallbacks == before + 1
    assert "うみ" in [u.surface for u in kana_units]


def test_lines_without_words_have_no_record(annotator):
    assert annotator.record(units("！？"), annotator.units(units("！？")))[0] is None


def test_coverage_counts_content_only(annotator):
    _record, resolved = annotator.record(KANJI, annotator.units(KANJI))
    coverage = annotate.Coverage()
    coverage.add(resolved)
    assert (coverage.content, coverage.covered) == (3, 3)      # ナマコブシ, 海, 返したら


@pytest.mark.rom
def test_whole_game(rom_path):
    from pathlib import Path

    from jpreader_tools import formats

    root = Path(__file__).resolve().parent.parent
    annot, dictionary, result = annotate.build_files(root, rom_path)
    for arc, coverage in result.coverage.items():
        assert coverage.share >= 0.95, (arc, coverage.share)
    assert result.projected > 10 * result.fallbacks
    archives = formats.read_annot(annot)
    entries, _labels = formats.read_dict(dictionary)
    kanji = archives[41][22].lines[0]
    kana = archives[40][22].lines[0]
    lottery = [u for u in kanji.units if u.length == 2 and u.entry is not None
               and entries[u.entry].senses and "lottery" in entries[u.entry].senses[0]]
    assert lottery, "抽選 should have its JMdict entry"
    assert lottery[0].entry in [u.entry for u in kana.units]
    assert len(annot) < 20_000_000 and len(dictionary) < 5_000_000


def test_lemma_only_entries_count_as_uncovered(annotator):
    stored = units("ゼンリョク")
    _record, resolved = annotator.record(stored, annotator.units(stored))
    coverage = annotate.Coverage()
    coverage.add(resolved)
    assert (coverage.content, coverage.covered) == (1, 0)


def test_a_decided_none_is_an_answer_not_a_gap(annotator):
    stored = units("ゼンリョク")
    unit_list = annotator.units(stored)
    _record, resolved = annotator.record(stored, unit_list, {0: 0})
    coverage = annotate.Coverage()
    coverage.add(resolved, {0: 0})
    assert (coverage.content, coverage.covered, coverage.decided_none, coverage.share) == (1, 0, 1, 1.0)
