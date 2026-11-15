from jpreader_tools import jmdict
from jpreader_tools.analyze import Unit
from jpreader_tools.dictionary import CardEntry, Resolver, is_content

ENTRIES = [
    jmdict.Entry("1", ("秘める",), ("ひめる",), True, False, ("v1", "vt"), ("to hide; to keep to oneself",)),
    jmdict.Entry("2", ("海",), ("うみ",), True, False, ("n",), ("sea; ocean",)),
    jmdict.Entry("3", (), ("ちゃん",), True, False, ("suf",), ("suffix for familiar person",)),
]


def unit(surface, lemma, reading="", kind="word", pos="noun"):
    return Unit(0, len(surface), kind, surface, lemma, reading, pos)


def resolver():
    return Resolver(jmdict.Index(ENTRIES), {"ナマコブシ": "Pyukumuku"}, {"を": "marks the direct object"})


def test_dictionary_word():
    entry = resolver().resolve(unit("秘めている", "秘める", "ひめる", pos="verb"))
    assert entry == CardEntry("dictionary", "秘める", "ひめる", "verb (ichidan)", ("to hide; to keep to oneself",), 1)


def test_kana_word_gets_the_kanji_headword():
    entry = resolver().resolve(unit("うみ", "うみ", "うみ"))
    assert (entry.headword, entry.reading) == ("海", "うみ")


def test_name_and_particle():
    r = resolver()
    assert r.resolve(unit("ナマコブシ", "ナマコブシ", kind="name")) == CardEntry(
        "name", "ナマコブシ", "", "name", ("Pyukumuku",))
    assert r.resolve(unit("を", "を", kind="particle")) == CardEntry(
        "particle", "を", "", "particle", ("marks the direct object",))


def test_unknown_word_has_no_entry():
    assert resolver().resolve(unit("ゼンリョク", "ゼンリョク")).kind == "lemma"


def test_is_content():
    assert is_content(unit("海", "海"))
    assert is_content(unit("ナマコブシ", "ナマコブシ", kind="name"))
    assert not is_content(unit("を", "を", kind="particle"))
    assert not is_content(unit("kg", "kg"))
    assert not is_content(unit("１２", "１２"))


def test_normalized_form_picks_the_right_homonym():
    entries = [jmdict.Entry("1", ("結う",), ("いう",), True, False, ("v5u",), ("to do up (hair)",)),
               jmdict.Entry("2", ("言う",), ("いう",), True, False, ("v5u",), ("to say",)),
               jmdict.Entry("3", ("勝つ",), ("かつ",), True, False, ("v5t",), ("to win",))]
    r = Resolver(jmdict.Index(entries), {}, {"よ": "emphasis"})
    say = Unit(0, 4, "word", "いわない", "いう", "いう", "verb", ("ない",), "いわない", "言う")
    assert r.resolve(say).headword == "言う"
    win = Unit(0, 3, "word", "勝てる", "勝てる", "かてる", "verb", ("える",), "かてる", "勝つ")
    assert (r.resolve(win).headword, r.resolve(win).reading) == ("勝つ", "かつ")
    stretched = Unit(0, 2, "particle", "よー", "よー", "よー", "other", (), "よー", "よ")
    assert r.resolve(stretched).senses == ("emphasis",)


def test_particles_without_a_note_do_not_become_nouns():
    entries = [jmdict.Entry("1", ("胃",), ("い",), True, False, ("n",), ("stomach",)),
               jmdict.Entry("2", (), ("ぞい",), False, False, ("prt",), ("emphasis (dialect)",))]
    r = Resolver(jmdict.Index(entries), {}, {})
    assert r.resolve(unit("い", "い", "い", kind="particle")).kind == "lemma"
    assert r.resolve(unit("ぞい", "ぞい", "ぞい", kind="particle")).senses == ("emphasis (dialect)",)


def test_no_match_still_gives_the_dictionary_form_and_reading():
    entry = resolver().resolve(unit("ゼンリョクで", "ゼンリョク", "ぜんりょく"))
    assert entry == CardEntry("lemma", "ゼンリョク", "ぜんりょく", "", ())


def test_usually_kana_words_keep_their_kana_headword():
    sore = jmdict.Entry("8", ("其れ",), ("それ",), True, True, ("pn",), ("that",))
    boku = jmdict.Entry("9", ("僕",), ("ぼく",), True, False, ("pn",), ("I (male)",))
    r = Resolver(jmdict.Index([sore, boku]), {}, {})
    assert r.resolve(unit("それ", "それ", "それ")).headword == "それ"
    assert r.resolve(unit("ぼく", "ぼく", "ぼく")).headword == "僕"     # not usually kana: the kanji teaches something


def test_card_entry_carries_the_id_and_the_front_rule_wins_over_the_normalized_form():
    sore = jmdict.Entry("1006430", ("其れ",), ("それ",), True, True, ("pn",), ("that",),
                        common_forms=frozenset({"それ"}))
    r = Resolver(jmdict.Index([sore]), {}, {})
    entry = r.resolve(Unit(0, 2, "word", "それ", "それ", "それ", "noun", (), "それ", "其れ"))
    assert (entry.headword, entry.reading, entry.jmdict_id) == ("それ", "それ", 1006430)


def test_a_decision_overrides_ranking_and_zero_means_no_entry():
    you = jmdict.Entry("200", ("君",), ("きみ",), True, False, ("pn",), ("you",), common_forms=frozenset({"君", "きみ"}))
    yolk = jmdict.Entry("100", ("黄身",), ("きみ",), True, False, ("n",), ("yolk",), common_forms=frozenset({"黄身", "きみ"}))
    r = Resolver(jmdict.Index([yolk, you]), {}, {})
    u = unit("きみ", "きみ", "きみ")
    assert r.resolve(u, choice=200).jmdict_id == 200
    assert r.resolve(u, choice=100).jmdict_id == 100
    assert r.resolve(u, choice=0).kind == "lemma"
    assert r.resolve(u).jmdict_id in (100, 200)            # no choice: ranking, as before


def test_the_headword_keeps_the_games_spelling_not_sudachis_normalized_one():
    find = jmdict.Entry("1604570", ("見付ける", "見つける"), ("みつける",), True, False, ("v1",), ("to find",),
                        common_forms=frozenset({"見つける"}))
    r = Resolver(jmdict.Index([find]), {}, {})
    entry = r.resolve(Unit(0, 4, "word", "見つけた", "見つける", "みつける", "verb", (), "みつけた", "見付ける"))
    assert entry.headword == "見つける"
    assert r.resolve(Unit(0, 4, "word", "見つけた", "見つける", "みつける", "verb", (), "みつけた", "見付ける"),
                     choice=1604570).headword == "見つける"
