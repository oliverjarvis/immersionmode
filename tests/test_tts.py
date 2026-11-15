import pytest

from jpreader_tools import tts


def row(file, line, text, speaker="Hau", kind="dialogue", speaker_type="character", gender="male", age="teen"):
    return {"file": file, "line": line, "ja_kanji": text, "speaker": speaker, "kind": kind,
            "speaker_type": speaker_type, "gender": gender, "age": age}


def test_boxes_drop_tags_fill_names_and_split_at_new_boxes():
    text = "そうだ！　{0100:0000}{c}\n町の　奥には\n{0101:0001}が　いるんだ！{r}\nいこう{0114:0005}"
    assert tts.boxes(text) == ["そうだ！ヨウ", "町の奥にはモクローがいるんだ！", "いこう"]
    assert tts.boxes("{0114:0005}") == []


def test_a_line_runs_on_mid_sentence_and_pauses_where_sentences_end():
    text, spans = tts.script(["そうだ！", "町の奥には", "遺跡があるんだ！", "いこう"], ["[ひらめいて]", "[熱く]", "", "[優しく]"])
    assert text == "[ひらめいて] そうだ！\n[熱く] 町の奥には遺跡があるんだ！\n[優しく] いこう"
    assert [text[a:b] for a, b in spans] == ["そうだ！", "町の奥には", "遺跡があるんだ！", "いこう"]
    text, _ = tts.script(["吊り橋の上で", "このコを"], ["", "[感謝して]"])
    assert text == "吊り橋の上でこのコを"            # a direction mid-sentence is dropped: it would make a pause
    assert tts.script(["あ。", "い"], [])[0] == "あ。\nい"


def test_directions_file(tmp_path):
    path = tmp_path / "d.tsv"
    path.write_text("file\tline\tdirections\n31\t2\t[不思議そうに] | [面白がって]\n31\t3\t\n", encoding="utf-8")
    assert tts.load_directions(path) == {(31, 2): ["[不思議そうに]", "[面白がって]"], (31, 3): [""]}


def test_rotoms_names_for_the_player_are_the_default_name():
    assert tts.boxes("{0189:0001}！　いくロ{c}{018A:0002}ポン") == ["ヨウ！いくロ", "ヨウポン"]


def test_lines_with_words_the_game_fills_in_are_not_voiced():
    assert not tts.has_variable("{0100:0000}！　{0101:0000}と{FF00:0001}しまキング{FF00:0000}に{r}あう{BE05:0000}{0114:0005}")
    assert tts.has_variable("{0109:0000}を　あげる")                  # an item
    assert tts.has_variable("${0202:0001}です")                       # a price
    assert tts.has_variable("{0100:0001}さんを")                      # someone else's name, not the player's
    story = [row(80, 0, "{0109:0000}を　あげる"), row(80, 1, "{0100:0000}！")]
    assert [c.line for c in tts.plan(story, {80}, set(), {})] == [1]


def test_cast_by_gender_and_talking_pokemon():
    story = [row(80, 0, "おう", speaker="Team Skull Grunt", gender="male", age="young adult"),
             row(80, 1, "あら", speaker="Team Skull Grunt", gender="female", age="young adult"),
             row(80, 2, "ビビビ", speaker="Rotom Pokédex", speaker_type="pokemon", gender="", age=""),
             row(80, 3, "ピュイ！", speaker="Nebby", speaker_type="", gender="", age=""),
             row(80, 5, "ギャーオ！", speaker="Ultra Beast", speaker_type="", gender="", age=""),
             row(80, 4, "ようこそ", speaker="Ilima")]
    assert [(c.line, c.voice) for c in tts.plan(story, {80}, set(), {})] == [
        (0, "Callum"), (1, "Laura"), (2, "Charlie"), (4, "Eric")]


def test_no_crowd_boy_sounds_like_hau():
    boy = {"speaker": "Youngster", "gender": "male", "age": "teen"}
    system = [{"file": 109, "line": 0, "ja_kanji": "しょうぶだ！", "kind": "system"}]
    [clip] = tts.plan_system(system, {109}, {(109, 0): boy}, {})
    assert clip.voice != tts.CAST["Hau"] and clip.voice == tts.SYSTEM_CROWD[("male", "teen")]
    assert [c.voice for c in tts.plan([row(80, 0, "やあ", speaker="boy")], {80}, set(), {})] == ["Will"]   # nor story ones
    girl = {"speaker": "Lass", "gender": "female", "age": "teen"}
    assert tts.plan_system(system, {109}, {(109, 0): girl}, {})[0].voice not in (tts.CAST["Mallow"], tts.CAST["Zossie"])
    grunt = {"speaker": "Team Skull Grunt", "gender": "male", "age": "young adult"}
    assert tts.plan_system(system, {109}, {(109, 0): grunt}, {})[0].voice == "Callum"


def test_plan_casts_voices_and_leaves_out_cries_and_the_player():
    story = [row(31, 3, "なにか{0114:0007}"),
             row(31, 4, "ピカ！", speaker="Pikachu", speaker_type="pokemon", gender="", age=""),
             row(31, 5, "ぬにゃあ", speaker="Meowth", speaker_type="", gender="", age=""),
             row(31, 6, "はい", speaker="player"),
             row(31, 7, "やあ", speaker="old man", speaker_type="npc", gender="male", age="elderly"),
             row(31, 8, "どうも", speaker="", speaker_type="", gender="female", age=""),
             row(31, 9, "看板", kind="sign"),
             row(32, 0, "ほかの　ファイル")]
    clips = tts.plan(story, {31}, {"Meowth"}, {(31, 3): ["[元気に]"]})
    assert [(c.line, c.voice, c.text) for c in clips] == [
        (3, "Liam", "[元気に] なにか"), (7, "Bill", "やあ"), (8, "Matilda", "どうも")]
    assert clips[0].spans == ((6, 9),) and clips[0].ends == (False,)
    assert clips[0].name == "0031-003.mp3"


def test_pending_skips_clips_already_made_with_the_same_request(tmp_path):
    clip = tts.Clip(31, 3, "Hau", "Liam", "なにか")
    assert tts.pending([clip], tmp_path) == [clip]
    (tmp_path / clip.name).write_bytes(b"mp3")
    (tmp_path / "manifest.json").write_text(
        '{"0031-003.mp3": {"voice": "Liam", "text": "なにか", "model": "%s"}}' % tts.MODEL)
    assert tts.pending([clip], tmp_path) == []
    changed = tts.Clip(31, 3, "Hau", "Liam", "なにかな")
    assert tts.pending([changed], tmp_path) == [changed]
    directed = tts.Clip(31, 3, "Hau", "Liam", "[元気に] なにか")
    assert tts.pending([directed], tmp_path) == [] and tts.pending([directed], tmp_path, redirect=True) == [directed]


def test_a_take_whose_directions_or_pauses_changed_is_kept_unless_asked(tmp_path):
    """Takes in 55-65 were made when every box paused and took a direction: the same words, kept."""
    (tmp_path / "0055-008.mp3").write_bytes(b"mp3")
    (tmp_path / "manifest.json").write_text(
        '{"0055-008.mp3": {"voice": "George", "text": "[真面目に] あの\\n[穏やかに] ね", "model": "%s"}}' % tts.MODEL)
    now = tts.Clip(55, 8, "Professor Kukui", "George", "[真面目に] あのね")
    assert tts.pending([now], tmp_path) == []
    assert tts.pending([now], tmp_path, redirect=True) == [now]
    assert tts.pending([tts.Clip(55, 8, "Professor Kukui", "George", "[真面目に] あのさ")], tmp_path) != []
    assert tts.pending([tts.Clip(55, 8, "Professor Kukui", "Bill", "[真面目に] あのね")], tmp_path) != []


def test_a_take_with_no_timings_is_remade_only_when_asked(tmp_path):
    """Takes made before timings were kept (story files 54-65) cost a whole new take to time."""
    clip = tts.Clip(31, 3, "Hau", "Liam", "なにか")
    (tmp_path / clip.name).write_bytes(b"mp3")
    (tmp_path / "manifest.json").write_text(
        '{"0031-003.mp3": {"voice": "Liam", "text": "なにか", "model": "%s"}}' % tts.MODEL)
    assert tts.pending([clip], tmp_path) == []
    assert tts.pending([clip], tmp_path, retime=True) == [clip]
    (tmp_path / clip.timings).write_text("{}")
    assert tts.pending([clip], tmp_path, retime=True) == []


def test_credits_at_the_list_rate_or_a_measured_one():
    clips = [tts.Clip(31, 3, "Hau", "Liam", "あ" * 100)]
    assert tts.credits(clips) == 50 and tts.credits(clips, rate=0.2) == 20


def test_file_ranges():
    assert tts.file_ranges(["27-29", "31"]) == {27, 28, 29, 31}
    assert tts.parse_specs(["27-28", "sys38", "SYS40-41"]) == ({27, 28}, {38, 40, 41})


def test_system_table_casts_ranges_of_lines(tmp_path):
    path = tmp_path / "s.tsv"
    path.write_text("# who says the opening's lines\nfile\tlines\tspeaker\tgender\tage\tscene\n"
                    "38\t1-3,5\tProfessor Kukui\tmale\tadult\tOpening call\n38\t4\tMom\tfemale\tadult\n",
                    encoding="utf-8")
    cast = tts.load_system(path)
    assert sorted(cast) == [(38, 1), (38, 2), (38, 3), (38, 4), (38, 5)]
    assert cast[(38, 5)] == {"speaker": "Professor Kukui", "gender": "male", "age": "adult", "scene": "Opening call"}
    assert cast[(38, 4)]["speaker"] == "Mom" and cast[(38, 4)]["scene"] == ""


def test_system_lines_are_cleaned_like_story_lines_and_named_apart():
    system = [{"file": 38, "line": 0, "ja_kanji": "{BDFF:0000}", "kind": "system"},
              {"file": 38, "line": 1, "ja_kanji": "{0100:0000}\u3000で\u3000いいかい？", "kind": "system"},
              {"file": 38, "line": 2, "ja_kanji": "はい", "kind": "system"},
              {"file": 39, "line": 1, "ja_kanji": "ほか", "kind": "system"}]
    cast = {(38, 0): {"speaker": "Professor Kukui", "gender": "male", "age": "adult"},
            (38, 1): {"speaker": "Professor Kukui", "gender": "male", "age": "adult"},
            (39, 1): {"speaker": "", "gender": "female", "age": "teen"}}
    [clip] = tts.plan_system(system, {38}, cast, {(38, 1): ["[明るく]"]})
    assert (clip.set, clip.voice, clip.text) == ("system", "George", "[明るく] ヨウでいいかい？")
    assert clip.name == "S0038-001.mp3" and clip.timings == "S0038-001.json"
    assert tts.Clip(38, 1, "Hau", "Liam", "x").name == "0038-001.mp3"      # story file 38 is another clip


def test_lines_with_the_same_voice_and_text_share_one_take(tmp_path):
    """Rotom's affection variants: one take, every line in clips.csv, priced once."""
    rotom = {"speaker": "Rotom Pokédex", "gender": "", "age": ""}
    system = [{"file": 89, "line": k, "ja_kanji": text, "kind": "system"}
              for k, text in enumerate(["いくロ！", "いくロ！", "{0100:0000}！", "{0189:0001}！"])]
    clips = tts.plan_system(system, {89}, {(89, k): rotom for k in range(4)}, {})
    assert [(c.line, c.source) for c in clips] == [(0, ""), (1, "S0089-000.mp3"), (2, ""), (3, "S0089-002.mp3")]
    assert [c.take for c in clips] == ["S0089-000.mp3", "S0089-000.mp3", "S0089-002.mp3", "S0089-002.mp3"]
    assert [c.line for c in tts.pending(clips, tmp_path)] == [0, 2]
    assert tts.credits(tts.pending(clips, tmp_path)) == round((len("いくロ！") + len("ヨウ！")) * tts.CREDITS_PER_CHAR)


def test_index_links_each_clip_to_its_line_and_scene(tmp_path):
    import csv

    story = [{**row(31, 2, "しまキングは{c}\nいない{0114:0005}"), "en": "The kahuna?{c}\nNot here.{0114:0005}"}]
    clips = tts.plan(story, {31}, set(), {(31, 2): ["[不思議そうに]", ""]})
    path = tmp_path / "clips.csv"
    tts.write_index(clips, {("story", 31, 2): story[0]}, {("story", 31): "Iki Town"},
                    {("story", 31, 2): ["[不思議そうに]", ""]}, path)
    [entry] = list(csv.DictReader(open(path, encoding="utf-8-sig")))
    assert entry == {"clip": "clips/0031-002.mp3", "set": "story", "story_file": "31", "line": "2",
                     "scene": "Iki Town", "speaker": "Hau", "voice": "Liam", "english": "The kahuna? Not here.",
                     "japanese": "しまキングは / いない", "directions": "[不思議そうに] | "}


def test_a_run_for_some_files_indexes_every_clip_made(tmp_path, monkeypatch):
    """voice.bin is built from clips.csv: `jpreader tts 54-80` must not drop the clips of 27-53, nor the system
    ones. Nothing is pending here, so no request is made."""
    import csv
    import json
    import subprocess

    from jpreader_tools import cli

    data = tmp_path / "data"
    data.mkdir()
    (data / "system.tsv").write_text("file\tlines\tspeaker\tgender\tage\tscene\n38\t1-2\tProfessor Kukui\tmale\tadult\tOpening\n",
                                     encoding="utf-8")
    for name in ("DIRECTIONS", "SYSTEM_DIRECTIONS", "SCENES"):
        monkeypatch.setattr(tts, name, data / f"{name}.tsv")
    monkeypatch.setattr(tts, "SYSTEM", data / "system.tsv")
    dump = tmp_path / "build" / "script"
    dump.mkdir(parents=True)
    story = [{**row(27, 0, "あ。"), "en": "A."}, {**row(54, 1, "い。"), "en": "I."},
             {**row(60, 0, "ピカ"), "speaker": "Pikachu", "en": "Pika"}]           # a cry: never a clip
    system = [{"file": 38, "line": 1, "ja_kanji": "え。", "en": "E.", "kind": "system"},
              {"file": 38, "line": 2, "ja_kanji": "え。", "en": "E!", "kind": "system"},
              {"file": 60, "line": 0, "ja_kanji": "ピカチュウ", "en": "Pikachu", "kind": "system"}]
    (dump / "story.jsonl").write_text("".join(json.dumps(r) + "\n" for r in story), encoding="utf-8")
    (dump / "system.jsonl").write_text("".join(json.dumps(r) + "\n" for r in system), encoding="utf-8")
    clips_dir = tmp_path / "build" / "tts" / "clips"
    clips_dir.mkdir(parents=True)
    manifest = {}
    for name, text in (("0027-000.mp3", "あ。"), ("0054-001.mp3", "い。"), ("S0038-001.mp3", "え。")):
        subprocess.run([tts.FFMPEG, "-v", "error", "-f", "lavfi", "-i", "sine=f=440:d=0.3", "-b:a", "64k",
                        str(clips_dir / name)], check=True)
        manifest[name] = {"voice": "George" if name[0] == "S" else "Liam", "text": text, "model": tts.MODEL}
    (clips_dir / "S0038-001.json").write_text(json.dumps(
        {"text": "え。", "spans": [[0, 2]], "ends": [True], "alignment": {"characters": ["え", "。"],
         "character_start_times_seconds": [0, 0.1], "character_end_times_seconds": [0.1, 0.3]}}))
    (clips_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False))
    monkeypatch.setattr(tts, "read_key", lambda root: pytest.fail("no clip is pending: no request"))

    assert cli.make_clips(tmp_path, ["54-80"], dry=False) == 0
    entries = list(csv.DictReader(open(tmp_path / "build" / "tts" / "clips.csv", encoding="utf-8-sig")))
    assert [(e["clip"], e["set"], e["story_file"], e["line"], e["voice"], e["scene"]) for e in entries] == [
        ("clips/0027-000.mp3", "story", "27", "0", "Liam", ""), ("clips/0054-001.mp3", "story", "54", "1", "Liam", ""),
        ("clips/S0038-001.mp3", "system", "38", "1", "George", "Opening"),
        ("clips/S0038-001.mp3", "system", "38", "2", "George", "Opening")]          # the same take
    pieces = list(csv.DictReader(open(tmp_path / "build" / "tts" / "pieces.csv", encoding="utf-8-sig")))
    assert [(p["piece"], p["set"], p["story_file"], p["line"]) for p in pieces] == [
        ("pieces/S0038-001-0.mp3", "system", "38", "1"), ("pieces/S0038-001-0.mp3", "system", "38", "2")]
    assert cli.make_clips(tmp_path, ["sys38"], dry=False) == 0                     # nothing pending: line 2 shares


def timing(text, spans, ends, times):
    """times: (start, end) per character."""
    return {"text": text, "spans": spans, "ends": ends,
            "alignment": {"characters": list(text), "character_start_times_seconds": [t[0] for t in times],
                          "character_end_times_seconds": [t[1] for t in times]}}


def test_cuts_in_real_silences_at_sentence_ends_and_breaths_only():
    # boxes: "あ。" | "いう" | "えお" ; a sentence ends after box 0; boxes 1 and 2 run on
    text = "あ。\nいうえお"
    times = [(0.0, 0.2), (0.2, 0.6), (0.6, 0.6), (0.6, 0.8), (0.8, 1.3), (1.3, 1.5), (1.5, 1.7)]
    t = timing(text, [[0, 2], [3, 5], [5, 7]], [True, False, True], times)
    quiet = [(0.3, 0.55), (1.0, 1.3), (1.6, 1.62)]     # the character timings run on; the audio has the pauses
    assert [(b, s, round(g, 2), round(m, 3)) for b, s, g, m in tts.breaks(t, quiet)] == [
        (1, True, 0.25, 0.425), (2, False, 0.3, 1.15)]
    assert [b for b, _ in tts.cuts(t, quiet, pause=0.2)] == [1, 2]
    assert [b for b, _ in tts.cuts(t, quiet, pause=0.5)] == [1]    # too short a breath to cut mid-sentence
    assert tts.cuts(t, [(0.3, 0.35)]) == []                          # 0.05 s isn't a pause, even at a sentence end
    t["alignment"]["characters"][0] = "x"
    assert tts.cuts(t, quiet) == []                                  # timings that don't match the text: no cuts


def test_pieces_are_cut_from_takes_and_stale_ones_removed(tmp_path):
    import json
    import subprocess

    clips_dir, pieces_dir = tmp_path / "clips", tmp_path / "pieces"
    clips_dir.mkdir()
    clip = tts.Clip(31, 3, "Hau", "Liam", "あ。\nいう", ((0, 2), (3, 5)), (True, False))
    # a tone, 0.3 s of silence, a tone: "あ。" then "いう"
    subprocess.run([tts.FFMPEG, "-v", "error", "-f", "lavfi", "-i", "sine=f=440:d=0.3", "-f", "lavfi", "-i",
                    "anullsrc=r=44100:cl=mono:d=0.3", "-f", "lavfi", "-i", "sine=f=440:d=0.4", "-filter_complex",
                    "[0][1][2]concat=n=3:v=0:a=1", "-b:a", "64k", str(clips_dir / clip.name)], check=True)
    times = [(0.0, 0.2), (0.2, 0.6), (0.6, 0.6), (0.6, 0.8), (0.8, 1.0)]
    (clips_dir / clip.timings).write_text(json.dumps(timing(clip.text, [[0, 2], [3, 5]], [True, False], times)))
    (pieces_dir).mkdir()
    (pieces_dir / "0031-003-5.mp3").write_bytes(b"old")
    rows = tts.cut_pieces([clip], clips_dir, pieces_dir)
    assert [(r["piece"], r["box"], r["boxes"], r["start"], r["end"], r["japanese"]) for r in rows] == [
        ("pieces/0031-003-0.mp3", 0, 1, "0.000", rows[0]["end"], "あ。"), ("pieces/0031-003-1.mp3", 1, 1, rows[0]["end"], "", "いう")]
    assert 0.40 <= float(rows[0]["end"]) <= 0.50                     # the middle of the real silence (0.3-0.6 s)
    assert sorted(p.name for p in pieces_dir.glob("*.mp3")) == ["0031-003-0.mp3", "0031-003-1.mp3"]
