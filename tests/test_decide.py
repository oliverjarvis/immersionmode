import os
from jpreader_tools import decide, jmdict
from jpreader_tools.analyze import Unit


def make_entry(id, kanji=(), kana=(), common_forms=(), odd_forms=(), usually_kana=False, pos=("n",)):
    return jmdict.Entry(id=id, kanji=tuple(kanji), kana=tuple(kana), common=bool(common_forms),
                        usually_kana=usually_kana, pos=tuple(pos), senses=("x",),
                        common_forms=frozenset(common_forms), odd_forms=frozenset(odd_forms))


def U(start, end, surface, lemma, reading, pos, kind="word", normalized=""):
    return Unit(start, end, kind, surface, lemma, reading, pos, (), surface, normalized)


def test_only_units_with_a_choice_are_asked():
    you = make_entry("200", kanji=("君",), kana=("きみ",), common_forms=("君", "きみ"), pos=("pn",))
    yolk = make_entry("100", kanji=("黄身",), kana=("きみ",), common_forms=("黄身", "きみ"), pos=("n",))
    cat = make_entry("300", kanji=("猫",), kana=("ねこ",), common_forms=("猫",))
    index = jmdict.Index([you, yolk, cat])
    units = [U(0, 2, "きみ", "きみ", "きみ", "noun"), U(3, 4, "猫", "猫", "ねこ", "noun")]
    ask = decide.asks_for_line(units, [(0, "きみの 猫")], "きみの ねこ", "Your cat", "", index)
    assert [u.index for u in ask.units] == [0] and {e.id for e in ask.units[0].candidates} == {"100", "200"}
    assert ask.units[0].marked == "【きみ】の 猫"
    assert decide.asks_for_line(units[1:], [(0, "きみの 猫")], "きみの ねこ", "Cat", "", index) is None


def test_short_kana_words_are_asked_even_with_one_candidate():
    defence = make_entry("1", kanji=("棒",), kana=("ぼう",), common_forms=("棒",))
    index = jmdict.Index([defence])
    units = [U(2, 4, "ぼう", "ぼう", "ぼう", "noun")]
    ask = decide.asks_for_line(units, [(0, "x"), (2, "ぼうぎょ")], "ぼうぎょ", "Defense", "", index)
    assert ask.units[0].marked == "x【ぼう】ぎょ" and len(ask.units[0].candidates) == 1


def test_decisions_are_per_line(tmp_path):
    d = decide.Decisions(tmp_path / "decisions.json")
    d.put("line-a", {0: 200})
    d.put("line-b", {0: 100})
    d.save()
    again = decide.Decisions(tmp_path / "decisions.json")
    assert again.get("line-a") == {0: 200} and again.get("line-b") == {0: 100} and again.get("x") is None
    assert again.digest() == d.digest()


def test_answers_outside_the_candidates_are_ignored():
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    yolk = make_entry("100", kanji=("黄身",), kana=("きみ",))
    ask = decide.Ask("k1", "きみ", "きみ", "You", "", (decide.AskUnit(0, "きみ", "きみ", (you, yolk)),))
    choices, ignored = decide.apply_answer([ask], {"lines": [{"line": 0, "units": [
        {"unit": 0, "choice": 999}, {"unit": 5, "choice": 200}]}]})
    assert choices == {} and ignored == 2
    choices, ignored = decide.apply_answer([ask], {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 200}]}]})
    assert choices == {"k1": {0: 200}} and ignored == 0
    choices, _ = decide.apply_answer([ask], {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 0}]}]})
    assert choices == {"k1": {0: 0}}                       # "none of these" is a decision too


def test_the_request_carries_the_english_line_and_the_candidates():
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    ask = decide.Ask("k1", "きみは", "きみは", "You are", "前の行", (decide.AskUnit(0, "きみ", "きみ", (you,)),))
    request = decide.request_for([ask], "claude-haiku-5-5")
    text = request["params"]["messages"][0]["content"]
    assert "You are" in text and "前の行" in text and "200" in text and "君" in text
    assert request["params"]["model"] == "claude-haiku-5-5" and len(request["custom_id"]) == 32


def test_resume_and_expired(tmp_path, monkeypatch):
    """A saved pending batch is collected, not resubmitted; an expired line is asked again next run."""
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    asks = [decide.Ask(f"k{i}", "きみ", "きみ", "You", "", (decide.AskUnit(0, "きみ", "きみ", (you, you)),)) for i in range(2)]
    d = decide.Decisions(tmp_path / "d.json")
    calls = []

    class FakeBatch:
        def __init__(self, key, folder): pass
        def submit(self, requests): calls.append(("submit", len(requests))); return "b1"
        def status(self, batch_id): return "ended"
        def results(self, batch_id):
            rid = decide.request_for(asks, "m")["custom_id"]
            return {rid: None}                                  # expired

    monkeypatch.setattr(decide.claude, "Batch", FakeBatch)
    summary = decide.run(asks, d, "k", 2.0, tmp_path, print, model="m", poll_s=0)
    assert summary["undecided"] == 2 and calls == [("submit", 1)] and d.pending_batch is None


def test_a_pending_batch_is_collected_not_resent(tmp_path, monkeypatch):
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    yolk = make_entry("100", kanji=("黄身",), kana=("きみ",))
    asks = [decide.Ask("k0", "きみ", "きみ", "You", "", (decide.AskUnit(0, "きみ", "きみ", (you, yolk)),))]
    d = decide.Decisions(tmp_path / "d.json")
    d.set_pending("b0", {decide.request_for(asks, "m")["custom_id"]: ["k0"]})
    sent = []

    class FakeBatch:
        def __init__(self, key, folder): pass
        def submit(self, requests): sent.append(requests); return "b9"
        def status(self, batch_id): return "ended"
        def results(self, batch_id):
            assert batch_id == "b0"
            rid = decide.request_for(asks, "m")["custom_id"]
            return {rid: {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 200}]}]}}

    monkeypatch.setattr(decide.claude, "Batch", FakeBatch)
    summary = decide.run(asks, d, "k", 2.0, tmp_path, print, model="m", poll_s=0)
    assert not sent and summary["decided"] == 1 and d.get("k0") == {0: 200} and d.pending_batch is None


def test_over_budget_asks_nothing(tmp_path, monkeypatch):
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    yolk = make_entry("100", kanji=("黄身",), kana=("きみ",))
    asks = [decide.Ask(f"k{i}", "きみ", "きみ", "You", "", (decide.AskUnit(0, "きみ", "きみ", (you, yolk)),)) for i in range(3)]
    monkeypatch.setattr(decide.claude, "Batch", lambda key, folder: (_ for _ in ()).throw(AssertionError("no batch")))
    summary = decide.run(asks, decide.Decisions(tmp_path / "d.json"), "k", 0.0, tmp_path, print, poll_s=0)
    assert summary["undecided"] == 3 and summary["cost_estimate_usd"] > 0


def test_fallback_kana_lines_take_no_decisions():
    from jpreader_tools import annotate
    assert annotate.kana_choices(projected=False, kanji_choices={0: 200}) is None
    assert annotate.kana_choices(projected=True, kanji_choices={0: 200}) == {0: 200}
    assert annotate.kana_choices(projected=True, kanji_choices=None) is None


def _two_ways(key):
    you = make_entry("200", kanji=("君",), kana=("きみ",))
    yolk = make_entry("100", kanji=("黄身",), kana=("きみ",))
    return decide.Ask(key, "きみ", "きみ", "You", "", (decide.AskUnit(0, "きみ", "きみ", (you, yolk)),))


def test_identical_lines_are_asked_once(tmp_path, monkeypatch):
    sent = []

    class FakeBatch:
        def __init__(self, key, folder): pass
        def submit(self, requests): sent.extend(requests); return "b1"
        def status(self, batch_id): return "ended"
        def results(self, batch_id):
            return {r["custom_id"]: {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 200}]}]} for r in sent}

    monkeypatch.setattr(decide.claude, "Batch", FakeBatch)
    asks = [_two_ways("same"), _two_ways("same"), _two_ways("same")]
    d = decide.Decisions(tmp_path / "d.json")
    summary = decide.run(asks, d, "k", 5.0, tmp_path, print, poll_s=0)
    assert len(sent) == 1 and sent[0]["params"]["messages"][0]["content"].count("Line ") == 1
    assert summary["asked"] == 1 and summary["decided"] == 1 and d.get("same") == {0: 200}


def test_a_pending_batch_is_collected_by_line_key_after_the_asks_change(tmp_path, monkeypatch):
    d = decide.Decisions(tmp_path / "d.json")
    old = [_two_ways("a"), _two_ways("b")]
    request = decide.request_for(old, "m")
    d.set_pending("b0", {request["custom_id"]: ["a", "b"]})
    sent = []

    class FakeBatch:
        def __init__(self, key, folder): pass
        def submit(self, requests): sent.extend(requests); return "b1"
        def status(self, batch_id): return "ended"
        def results(self, batch_id):
            if batch_id == "b0":
                return {request["custom_id"]: {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 200}]},
                                                          {"line": 1, "units": [{"unit": 0, "choice": 100}]}]}}
            return {r["custom_id"]: {"lines": [{"line": 0, "units": [{"unit": 0, "choice": 0}]}]} for r in sent}

    monkeypatch.setattr(decide.claude, "Batch", FakeBatch)
    new = [_two_ways("z"), _two_ways("b"), _two_ways("a")]          # a new line first: the chunks moved
    summary = decide.run(new, d, "k", 5.0, tmp_path, print, model="m", poll_s=0)
    assert d.get("a") == {0: 200} and d.get("b") == {0: 100}         # the paid answers are kept
    assert d.get("z") == {0: 0} and len(sent) == 1                    # and the rest asked in the same run
    assert summary["undecided"] == 0 and d.pending_batch is None


def test_offline_or_an_expired_batch_never_fails_the_build(tmp_path, monkeypatch):
    class Offline:
        def __init__(self, key, folder): pass
        def submit(self, requests): raise OSError("network is unreachable")
        def status(self, batch_id): raise decide.claude.ClaudeError("Anthropic returned 404: not found")
        def results(self, batch_id): raise AssertionError

    monkeypatch.setattr(decide.claude, "Batch", Offline)
    d = decide.Decisions(tmp_path / "d.json")
    d.set_pending("gone", {"x": ["a"]})
    lines = []
    summary = decide.run([_two_ways("a")], d, "k", 5.0, tmp_path, lines.append, poll_s=0)
    assert summary["undecided"] == 1 and d.pending_batch is None       # a 404 batch is dropped, not retried forever
    assert any("network is unreachable" in line for line in lines) and not any("k" == line for line in lines)


def test_a_cached_build_with_undecided_words_is_redone_once_claude_can_be_asked(tmp_path, monkeypatch):
    from types import SimpleNamespace

    from jpreader_tools import annotate
    calls = []

    def fake_build_files(root, rom_path):
        calls.append(os.environ.get("JPREADER_LLM"))
        undecided = 3 if os.environ.get("JPREADER_LLM") == "0" else 0
        result = SimpleNamespace(entries=[], decisions={"undecided": undecided}, forms={})
        return b"annot", bytes(28), result

    monkeypatch.setattr(annotate, "build_files", fake_build_files)
    monkeypatch.setattr(annotate.claude, "read_key", lambda root: "k")
    monkeypatch.setenv("JPREADER_LLM", "0")
    annotate.cached_files(tmp_path, tmp_path / "rom", "sha")
    annotate.cached_files(tmp_path, tmp_path / "rom", "sha")
    assert calls == ["0"]                                  # no key in use: the ranking-only build is reused
    monkeypatch.setenv("JPREADER_LLM", "1")
    annotate.cached_files(tmp_path, tmp_path / "rom", "sha")
    annotate.cached_files(tmp_path, tmp_path / "rom", "sha")
    assert calls == ["0", "1"]                             # redone once Claude can answer, then reused


def test_a_line_that_gets_no_answer_twice_is_left_to_ranking(tmp_path, monkeypatch):
    sent = []

    class Unanswered:
        def __init__(self, key, folder): pass
        def submit(self, requests): sent.append(len(requests)); return f"b{len(sent)}"
        def status(self, batch_id): return "ended"
        def results(self, batch_id): return {}                       # every request errored

    monkeypatch.setattr(decide.claude, "Batch", Unanswered)
    d = decide.Decisions(tmp_path / "d.json")
    for _ in range(3):
        summary = decide.run([_two_ways("a")], d, "k", 5.0, tmp_path, print, poll_s=0)
    assert sent == [1, 1] and summary["undecided"] == 0 and summary["given_up"] == 1
    assert decide.Decisions(tmp_path / "d.json").get("a") is None    # still ranked, never decided
