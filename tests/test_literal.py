from jpreader_tools import literal


def u(s):
    return [ord(c) for c in s]


def archives():
    """Tiny kana, kanji and English archives: file 118 holds moves; file 5 menu text."""
    kana = [[] for _ in range(119)]
    kanji = [[] for _ in range(119)]
    english = [[] for _ in range(119)]
    kana[118] = [u("はたく"), u("でんこうせっか")]
    kanji[118] = [u("はたく"), u("電光石火")]
    english[118] = [u("Pound"), u("Quick Attack")]
    kana[5] = [u("もどる"), u("これは　とても　ながい　せつめいの　ぶんしょうです"), u("123"), [0x10, 1, 0xBE01] + u("やめる")]
    kanji[5] = [u("戻る"), u("これは"), u("123"), u("止める")]
    english[5] = [u("Back"), u("This"), u("123"), u("Quit")]
    return kana, kanji, english


def test_names_and_short_labels_are_asked_once_each():
    asks = literal.asks_from(*archives())
    by_key = {a.key: a for a in asks}
    assert by_key["はたく"] == literal.Ask("はたく", "move", "Pound")       # kana and kanji agree: asked once
    assert by_key["電光石火"].kind == "move" and by_key["でんこうせっか"].english == "Quick Attack"
    assert by_key["もどる"].kind == "label" and by_key["戻る"].english == "Back"
    assert "これはとてもながいせつめいのぶんしょうです" not in by_key     # long: a message, not a label
    assert "123" not in by_key                                         # nothing Japanese to translate
    assert "やめる" not in by_key                                       # tagged lines are templates
    assert len(asks) == len(by_key)


def test_the_request_carries_kind_and_english():
    asks = [literal.Ask("はたく", "move", "Pound"), literal.Ask("もどる", "label", "Back")]
    request = literal.request_for(asks, "claude-sonnet-5-5")
    text = request["params"]["messages"][0]["content"]
    assert "0. はたく (move; official English: Pound)" in text and "1. もどる (label; official English: Back)" in text
    assert request["params"]["output_config"]["format"]["schema"] == literal.SCHEMA
    assert len(request["custom_id"]) == 32


def test_answers_are_matched_by_number_and_cleaned():
    asks = [literal.Ask("はたく", "move", "Pound"), literal.Ask("ロビン", "label", "Robin"),
            literal.Ask("でんこうせっか", "move", "Quick Attack")]
    got = literal.apply_answer(asks, {"items": [
        {"n": 0, "literal": "  slap  "}, {"n": 1, "literal": ""}, {"n": 2, "literal": "x" * 80}, {"n": 9, "literal": "?"}]})
    assert got == {"はたく": "slap", "ロビン": "", "でんこうせっか": "x" * literal.LITERAL_MAX}


def test_the_cache_keeps_answers_for_its_prompt_and_model(tmp_path):
    path = tmp_path / "literal.json"
    cache = literal.Literals(path, "m1")
    cache.put("はたく", "slap")
    cache.save()
    assert literal.Literals(path, "m1").get("はたく") == "slap"
    assert literal.Literals(path, "m2").get("はたく") is None          # another model: asked again


def test_run_sends_one_batch_and_keeps_the_answers(tmp_path, monkeypatch):
    sent = []

    class FakeBatch:
        def __init__(self, key, folder):
            pass

        def submit(self, requests):
            sent.extend(requests)
            return "b1"

        def status(self, batch_id):
            return "ended"

        def results(self, batch_id):
            return {r["custom_id"]: {"items": [{"n": 0, "literal": "slap"}]} for r in sent}

    monkeypatch.setattr(literal.claude, "Batch", FakeBatch)
    cache = literal.Literals(tmp_path / "literal.json", "m")
    asks = [literal.Ask("はたく", "move", "Pound")]
    summary = literal.run(asks, cache, "key", 5.0, tmp_path, lambda m: None, model="m", poll_s=0)
    assert cache.get("はたく") == "slap" and summary["answered"] == 1 and len(sent) == 1
    summary = literal.run(asks, cache, "key", 5.0, tmp_path, lambda m: None, model="m", poll_s=0)
    assert summary["cached"] == 1 and len(sent) == 1                    # nothing asked twice
    assert literal.Literals(tmp_path / "literal.json", "m").pending is None


def test_over_budget_or_offline_asks_nothing(tmp_path, monkeypatch):
    monkeypatch.setattr(literal.claude, "Batch", lambda key, folder: (_ for _ in ()).throw(AssertionError("no batch")))
    cache = literal.Literals(tmp_path / "literal.json", "m")
    asks = [literal.Ask("はたく", "move", "Pound")]
    assert literal.run(asks, cache, "key", 0.0, tmp_path, lambda m: None, model="m")["answered"] == 0
    assert literal.run(asks, cache, None, 5.0, tmp_path, lambda m: None, model="m")["answered"] == 0


def test_units_for_the_strings_file():
    cache = {"はたく": "slap", "ロビン": ""}
    assert literal.units_for(cache, u("はたく")) == u("slap")
    assert literal.units_for(cache, u("ロビン")) == []
    assert literal.units_for(cache, u("ない")) == []
    assert literal.units_for({"トリック": "trick"}, u("トリック"), u("Trick")) == []    # only the official name again
