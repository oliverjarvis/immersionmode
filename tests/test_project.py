import pytest

from jpreader_tools.analyze import Analyzer, Unit
from jpreader_tools.project import project


@pytest.fixture(scope="module")
def analyzer():
    return Analyzer()


def spans_text(text, spans):
    return [text[a:b] for a, b in spans]


def test_project_whole_phrase(analyzer):
    kanji = "可能性を　秘めている"
    kana = "かのうせいを　ひめている"
    spans = project(kanji, analyzer.units(kanji), kana)
    assert spans_text(kana, spans) == ["かのうせい", "を", "ひめている"]


def test_project_keeps_katakana_and_line_breaks(analyzer):
    kanji = "ナマコブシを\n海に　返したら"
    kana = "ナマコブシを\nうみに　かえしたら"
    spans = project(kanji, analyzer.units(kanji), kana)
    assert spans_text(kana, spans) == ["ナマコブシ", "を", "うみ", "に", "かえしたら"]


def test_project_uses_readings_where_kanji_touch():
    units = [Unit(0, 2, "word", "学生", "学生", "がくせい", "noun", (), "がくせい"),
             Unit(2, 3, "word", "証", "証", "しょう", "noun", (), "しょう")]
    assert project("学生証", units, "がくせいしょう") == [(0, 4), (4, 7)]


def test_project_gives_up_on_mismatch(analyzer):
    kanji = "可能性を　秘めている"
    assert project(kanji, analyzer.units(kanji), "かのうせいを　ひめて　いる") is None
    assert project(kanji, analyzer.units(kanji), "かのうせいが　ひめている") is None


def test_project_nothing():
    assert project("！", [], "！") == []
