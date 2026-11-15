import pytest

from jpreader_tools.analyze import Analyzer, ending_key, to_hiragana


@pytest.fixture(scope="module")
def analyzer():
    return Analyzer()


def summary(units):
    return [(u.surface, u.lemma, u.kind, u.endings) for u in units]


def test_te_iru_is_one_unit(analyzer):
    assert summary(analyzer.units("連れている")) == [("連れている", "連れる", "word", ("て", "いる"))]


def test_spec_example(analyzer):
    assert summary(analyzer.units("可能性を　秘めているのではないかと")) == [
        ("可能性", "可能性", "word", ()),
        ("を", "を", "particle", ()),
        ("秘めている", "秘める", "word", ("て", "いる")),
        ("の", "の", "particle", ()),
        ("ではない", "ではない", "word", ()),          # stage 16: one grammar unit
        ("か", "か", "particle", ()),
        ("と", "と", "particle", ()),
    ]


@pytest.mark.parametrize("text, lemma, endings", [
    ("思わず", "思う", ("ず",)),
    ("いられない", "いる", ("られる", "ない")),
    ("かえしたら", "かえす", ("たら",)),
    ("食べなければ", "食べる", ("ない", "ば")),
    ("来ていた", "来る", ("て", "いる", "た")),
    ("行きましょう", "行く", ("ましょう",)),
    ("わかりません", "わかる", ("ます", "ぬ")),
    ("見てください", "見る", ("て", "くださる")),
    ("見なさい", "見る", ("なさる",)),
    ("やってしまった", "やる", ("て", "しまう", "た")),
    ("行きたい", "行く", ("たい",)),
    ("戦わせる", "戦う", ("せる",)),
    ("ちかづいてきた", "ちかづく", ("て", "くる", "た")),
])
def test_conjugations_stay_with_their_verb(analyzer, text, lemma, endings):
    units = analyzer.units(text)
    assert len(units) == 1
    assert (units[0].lemma, units[0].endings, units[0].surface) == (lemma, endings, text)


def test_offsets_readings_and_spaces(analyzer):
    units = analyzer.units("ポケモンを\nあずけている")
    assert [(u.start, u.end) for u in units] == [(0, 4), (4, 5), (6, 12)]
    assert units[2].reading == "あずける"
    assert units[2].surface_reading == "あずけている"
    assert units[0].pos == "noun" and units[2].pos == "verb"


def test_suffix_and_punctuation(analyzer):
    assert summary(analyzer.units("ニャースちゃん！")) == [
        ("ニャース", "ニャース", "word", ()), ("ちゃん", "ちゃん", "word", ())]


def test_helpers():
    assert to_hiragana("ヒメル・ポケモン") == "ひめる・ぽけもん"
    assert ending_key("で", "*") == "て"
    assert ending_key("た", "仮定形-一般") == "たら"
    assert ending_key("ます", "意志推量形") == "ましょう"
    assert ending_key("ない", "連用形-一般") == "ない"


def test_normalized_forms_settle_kana_spellings(analyzer):
    assert [(u.lemma, u.normalized) for u in analyzer.units("いわない")] == [("いう", "言う")]
    assert analyzer.units("きたよ")[0].normalized == "来る"
    assert analyzer.units("みてみる")[0].normalized == "見る"


def test_potential_forms_get_their_base_verb(analyzer):
    unit = analyzer.units("勝てる")[0]
    assert (unit.normalized, unit.endings) == ("勝つ", ("える",))
    assert analyzer.units("来れた")[0].endings == ("える", "た")
    assert analyzer.units("見られる")[0].endings == ("られる",)   # ordinary auxiliary, not a potential verb


def test_stretched_particles(analyzer):
    unit = analyzer.units("よー")[0]
    assert (unit.kind, unit.normalized) == ("particle", "よ")


def test_suffixes_are_marked(analyzer):
    assert analyzer.units("ニャースちゃん")[1].pos == "suffix"


@pytest.mark.parametrize("text", ["する", "しよう", "します", "見る"])
def test_suru_and_ordinary_verbs_are_not_potential(analyzer, text):
    assert "える" not in analyzer.units(text)[0].endings


def test_potential_needs_an_e_row_stem(analyzer):
    assert analyzer.units("いける")[0].endings == ("える",)
    assert analyzer.units("使える")[0].endings == ("える",)


# Stage 16: grammar a learner reads as one unit.
@pytest.mark.parametrize("text, want", [
    ("ポケモンじゃない", [("ポケモン", "ポケモン", "word", ()), ("じゃない", "じゃない", "word", ())]),
    ("ポケモンじゃなかった", [("ポケモン", "ポケモン", "word", ()), ("じゃなかった", "じゃない", "word", ("た",))]),
    ("すごいじゃない", [("すごい", "すごい", "word", ()), ("じゃない", "じゃない", "word", ())]),
    ("ポケモンではない", [("ポケモン", "ポケモン", "word", ()), ("ではない", "ではない", "word", ())]),
    ("そうかもしれない", [("そう", "そう", "word", ()), ("かもしれない", "かもしれない", "word", ())]),
    ("そうかもしれません", [("そう", "そう", "word", ()), ("かもしれません", "かもしれない", "word", ("ます",))]),
    ("違うじゃない！", [("違う", "違う", "word", ()), ("じゃない", "じゃない", "word", ())]),
    ("しないじゃないですか", [("しない", "する", "word", ("ない",)), ("じゃないです", "じゃない", "word", ("です",)),
                             ("か", "か", "particle", ())]),
    ("見なくっちゃ", [("見なくっちゃ", "見る", "word", ("ない", "ちゃ"))]),
    ("いかなくちゃ", [("いかなくちゃ", "いく", "word", ("ない", "ちゃ"))]),
    ("それじゃ　またね", [("それ", "それ", "word", ()), ("じゃ", "じゃ", "particle", ()),
                         ("また", "また", "word", ()), ("ね", "ね", "particle", ())]),
])
def test_grammar_pieces_are_one_unit(analyzer, text, want):
    assert summary(analyzer.units(text)) == want


def test_a_merged_unit_reads_in_kana(analyzer):
    assert analyzer.units("そうかも知れない")[1].surface_reading == "かもしれない"
    split = analyzer.units("違うじゃない")
    assert [(u.start, u.end, u.surface_reading) for u in split] == [(0, 2, "ちがう"), (2, 6, "じゃない")]


def test_a_merged_unit_keeps_its_offsets_and_reading(analyzer):
    units = analyzer.units("ポケモンじゃなかった")
    assert [(u.start, u.end) for u in units] == [(0, 4), (4, 10)]
    assert units[1].reading == "じゃない" and units[1].surface_reading == "じゃなかった" and units[1].pos == "adj"


# Stage 16: a chunk (the game spaces them) never starts with a particle; when one seems to, the chunk is a kana
# word the game writes in kanji elsewhere (でかける, 出かける).
@pytest.fixture(scope="module")
def repairing():
    from jpreader_tools.analyze import Vocabulary
    seen = {("出かける", "でかける"): {"でかけて", "でかける"}, ("野生", "やせい"): {"やせい"},
            ("体力", "たいりょく"): {"たいりょく"}, ("鳴る", "なる"): {"なりすぎた"}, ("暗い", "くらい"): {"くらい"},
            ("中", "なか"): {"なか"}, ("運ぶ", "はこぶ"): {"はこば", "はこぶ"}}
    vocabulary = Vocabulary({("出かける", "でかける"): 4, ("野生", "やせい"): 1972, ("体力", "たいりょく"): 75,
                             ("鳴る", "なる"): 3, ("暗い", "くらい"): 4, ("中", "なか"): 152, ("運ぶ", "はこぶ"): 16,
                             ("担う", "になう"): 3, ("待つ", "まつ"): 40, ("安い", "やすい"): 9, ("出る", "でる"): 60,
                             ("外れる", "はずれる"): 5}, seen | {("出る", "でる"): {"でた"}, ("外れる", "はずれる"): {"はずれです"},
                                                         ("担う", "になう"): {"になった"}, ("待つ", "まつ"): {"またねえ"},
                                                         ("安い", "やすい"): {"やすく"}})
    return Analyzer(vocabulary, grammar=frozenset({"とか", "かも", "として", "でよ", "くらい", "でる"}))


@pytest.mark.parametrize("text, first", [
    ("ほら！　でかける　準備を　して！", ("でかける", "出かける")),
    ("でかける　準備", ("でかける", "出かける")),
    ("やせいの　ポケモン", ("やせい", "野生")),
    ("たいりょくが　へった", ("たいりょく", "体力")),
])
def test_a_chunk_starting_with_a_particle_is_repaired(repairing, text, first):
    units = repairing.units(text)
    found = [(u.surface, u.lemma) for u in units]
    assert first in found, found
    unit = next(u for u in units if u.surface == first[0])
    assert text[unit.start:unit.end] == first[0] and unit.surface_reading == unit.surface


@pytest.mark.parametrize("text", [
    "ペンでかける",            # not at a chunk's start: the particle で
    "で　どうするの",           # the particle is the whole chunk
    "１位　になった",           # に + なる, not 担う
    "またねえ",                # see you, not 待つ
    "なつき　やすく",           # the suffix -やすい
    "でねでね　かわらずのいし",   # で + ね, twice: a filler; no word holds a sentence-ending ね
    "ばか　はずれです",          # 外れ + です: a verb never takes です
    "あつく　なりすぎた",        # なる, not 鳴る
    "こいつ　くらいだ",          # くらい, not 暗い
    "なカなカ　すごい",          # Rotom's なかなか: a doubled word stays as it is
    "はこぼれ　した",            # 刃こぼれ: the game never writes 運ぶ as はこぼ…
])
def test_guards_leave_real_particles_alone(repairing, analyzer, text):
    assert summary(repairing.units(text)) == summary(analyzer.units(text))


def test_a_chunk_ends_at_punctuation(repairing):
    assert [u.lemma for u in repairing.units("やせい！やせい")].count("野生") == 2


def test_only_a_name_or_number_tag_joins_a_chunk(repairing):
    from jpreader_tools.lines import line_units
    word = [ord(c) for c in "でかける"]
    name = [0x10, 2, 0x0100, 0]          # a trainer's name: 「{name}でかける」 continues its chunk
    colour = [0x10, 2, 0xFF00, 1]        # a colour: the line still starts a chunk
    assert [u.lemma for u in line_units(repairing, colour + word)] == ["出かける"]
    assert [u.surface for u in line_units(repairing, name + word)] == ["で", "かける"]


def test_a_run_after_a_tag_is_not_a_chunk_start(repairing):
    assert [u.surface for u in repairing.units("でかける", chunk_start=False)] == ["で", "かける"]


def test_a_repair_must_match_how_the_game_writes_the_word():
    from jpreader_tools.analyze import Vocabulary
    v = Vocabulary({("運ぶ", "はこぶ"): 16}, {("運ぶ", "はこぶ"): {"はこば", "はこんで"}})
    assert v.attested(("運ぶ", "はこぶ"), "はこばれた") and v.attested(("運ぶ", "はこぶ"), "はこ")
    assert not v.attested(("運ぶ", "はこぶ"), "はこぼれ") and not v.attested(("出る", "でる"), "でた")
    assert Vocabulary({("運ぶ", "はこぶ"): 16}).attested(("運ぶ", "はこぶ"), "はこぼれ")   # nothing to check against


def test_the_vocabulary_maps_kana_prefixes_to_kanji():
    from jpreader_tools.analyze import Vocabulary
    v = Vocabulary({("出かける", "でかける"): 4, ("野性", "やせい"): 2, ("野生", "やせい"): 1972, ("ごはん", "ごはん"): 9})
    assert v.spellings("で") == ["出"] and v.spellings("でか") == [] and v.spellings("やせい") == ["野生", "野性"]
    assert v.spellings("ごはん") == [] and ("出かける", "でかける") in v
