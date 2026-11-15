import json
import zipfile

import pytest

from jpreader_tools import jmdict


def word(id, kanji=(), kana=(), common=False, pos=("n",), glosses=("thing",), misc=(), senses=1):
    return {
        "id": id,
        "kanji": [{"common": common, "text": k, "tags": []} for k in kanji],
        "kana": [{"common": common, "text": k, "tags": [], "appliesToKanji": ["*"]} for k in kana],
        "sense": [{"partOfSpeech": list(pos), "misc": list(misc),
                   "gloss": [{"lang": "eng", "text": g} for g in glosses]} for _ in range(senses)],
    }


DATA = {"words": [
    word("1", kanji=["返す"], kana=["かえす"], common=True, pos=("v5s", "vt"), glosses=("to return", "to give back")),
    word("2", kanji=["帰す"], kana=["かえす"], pos=("v5s", "vt"), glosses=("to send home",)),
    word("3", kanji=["孵す"], kana=["かえす"], common=True, pos=("n",), glosses=("hatching (noun sense)",)),
    word("4", kana=["ちゃん"], pos=("suf",), glosses=("suffix for familiar person",)),
    word("5", kanji=["抽選"], kana=["ちゅうせん"], common=True, glosses=("lottery", "raffle", "drawing (of lots)"), senses=4),
    word("6", kana=["とても"], common=True, pos=("adv",), glosses=("very",), misc=("uk",)),
    word("7", kanji=["迚も"], kana=["とても"], pos=("adv",), glosses=("very (kanji form)",)),
    word("8", kanji=["長"], kana=["なが"], glosses=("x" * 100,)),
    {"id": "9", "kanji": [], "kana": [{"common": False, "text": "からっぽ", "tags": []}], "sense": []},
]}


def test_parse_keeps_three_short_senses():
    entries = {e.id: e for e in jmdict.parse(DATA)}
    assert "9" not in entries                       # no senses: dropped
    lottery = entries["5"]
    assert lottery.kanji == ("抽選",) and lottery.kana == ("ちゅうせん",)
    assert lottery.senses == ("lottery; raffle; drawing (of lots)",) * 3
    assert lottery.common and lottery.pos == ("n",)
    assert len(entries["8"].senses[0]) == 80 and entries["8"].senses[0].endswith("…")
    assert entries["6"].usually_kana and not entries["7"].usually_kana


def test_lookup_prefers_reading_then_pos_then_common():
    index = jmdict.Index(jmdict.parse(DATA))
    assert index.lookup("返す", "かえす", "verb").id == "1"
    assert index.lookup("帰す", "かえす", "verb").id == "2"
    assert index.lookup("かえす", "かえす", "verb").id == "1"      # verb beats the common noun
    assert index.lookup("かえす", "かえす", "noun").id == "3"
    assert index.lookup("とても", "とても", "adv").id == "6"        # usually kana
    assert index.lookup("ちゅうせん", "ちゅうせん").id == "5"        # found through its reading
    assert index.lookup("ないもの", "ないもの") is None


def test_pos_label():
    assert jmdict.pos_label(("v5s", "vt")) == "verb (godan)"
    assert jmdict.pos_label(("v1",)) == "verb (ichidan)"
    assert jmdict.pos_label(("adj-i",)) == "i-adjective"
    assert jmdict.pos_label(("n",)) == "noun"
    assert jmdict.pos_label(("zzz",)) == "zzz"
    assert jmdict.pos_label(()) == ""


def test_load_reads_the_zip(tmp_path):
    path = tmp_path / "jmdict.json.zip"
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("jmdict-eng.json", json.dumps(DATA))
    assert len(jmdict.load(path)) == 8


def test_fetch_checks_the_digest(tmp_path):
    (tmp_path / jmdict.JMDICT_FILE).write_bytes(b"not the release")
    with pytest.raises(jmdict.JmdictError, match="SHA-256"):
        jmdict.fetch(tmp_path)
    assert not (tmp_path / jmdict.JMDICT_FILE).exists()


def test_no_lookup_by_sound_for_numbers_and_latin():
    index = jmdict.Index(jmdict.parse({"words": [word("10", kanji=["酸"], kana=["さん"], common=True)]}))
    assert index.lookup("3", "さん") is None
    assert index.lookup("さん", "さん").id == "10"


def make_entry(id, kanji=(), kana=(), common_forms=(), odd_forms=(), usually_kana=False, pos=("n",)):
    return jmdict.Entry(id=id, kanji=tuple(kanji), kana=tuple(kana), common=bool(common_forms),
                        usually_kana=usually_kana, pos=tuple(pos), senses=("x",),
                        common_forms=frozenset(common_forms), odd_forms=frozenset(odd_forms))


def test_parse_keeps_common_and_odd_forms():
    entry = jmdict.parse({"words": [{
        "id": "1", "kanji": [{"text": "其れ", "common": False, "tags": []}, {"text": "夫れ", "common": False, "tags": ["rK"]}],
        "kana": [{"text": "それ", "common": True, "tags": [], "appliesToKanji": ["*"]}],
        "sense": [{"partOfSpeech": ["pn"], "misc": ["uk"], "gloss": [{"lang": "eng", "text": "that"}]}]}]})[0]
    assert entry.common_forms == frozenset({"それ"})
    assert entry.odd_forms == frozenset({"夫れ"})


def test_the_matched_form_being_common_outranks_order():
    rare = make_entry("100", kanji=("黄身",), kana=("きみ",), common_forms=(), pos=("n",))
    you = make_entry("200", kanji=("君",), kana=("きみ",), common_forms=("君", "きみ"), pos=("n",))
    index = jmdict.Index([rare, you])
    assert [e.id for e in index.candidates("きみ", "きみ", "noun")] == ["200", "100"]
    assert index.lookup("きみ", "きみ", "noun").id == "200"


def test_front_rule():
    sore = make_entry("1", kanji=("其れ", "夫れ"), kana=("それ",), usually_kana=True, odd_forms=("夫れ",))
    assert jmdict.front(sore, "其れ", "それ") == "それ"                       # usually kana
    study = make_entry("2", kanji=("勉強",), kana=("べんきょう",))
    assert jmdict.front(study, "べんきょう", "べんきょう") == "勉強"          # first proper kanji form
    see = make_entry("3", kanji=("見付ける", "見つける"), kana=("みつける",), common_forms=("見つける",))
    assert jmdict.front(see, "見付ける", "みつける") == "見付ける"            # the game's own spelling
    assert jmdict.front(see, "みつける", "みつける") == "見つける"            # else the common kanji form
    odd = make_entry("4", kanji=("乍ら",), kana=("ながら",), odd_forms=("乍ら",))
    assert jmdict.front(odd, "ながら", "ながら") == "ながら"                  # never an odd form
    float_ = make_entry("5", kanji=("浮き浮き",), kana=("うきうき", "ウキウキ"), usually_kana=True,
                        common_forms=("ウキウキ",))
    assert jmdict.front(float_, "浮き浮き", "うきーうき") == "ウキウキ"        # no such reading: the common kana form
