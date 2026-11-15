from jpreader_tools import textdump


def row(file, line, kind, speaker="", gender="", age=""):
    return {"file": file, "line": line, "kind": kind, "speaker": speaker, "gender": gender, "age": age,
            "guessed": ""}


def guess(kind, speaker="", gender="", age=""):
    return {"kind": kind, "speaker": speaker, "gender": gender, "age": age}


def test_guesses_fill_only_what_the_scripts_left_open(tmp_path):
    path = tmp_path / "guesses.tsv"
    path.write_text("file\tline\tkind\tspeaker\tgender\tage\n"
                    "1\t0\tdialogue\tshopkeeper\tmale\tadult\n"
                    "1\t1\tnarration\t\t\t\n"
                    "1\t2\tdialogue\tgirl\tfemale\tchild\n"
                    "1\t3\tnarration\t\t\t\n", encoding="utf-8")
    guesses = textdump.load_guesses(path)
    assert guesses[(1, 0)] == {"file": "1", "line": "0", **guess("dialogue", "shopkeeper", "male", "adult")}

    story = [row(1, 0, "unknown"), row(1, 1, "message"),
             row(1, 2, "dialogue", "Lass", "female"),          # the scripts' speaker stays
             row(1, 3, "choice"), row(1, 4, "unknown")]
    textdump.apply_guesses(story, guesses)
    assert story[0] == {**row(1, 0, "dialogue", "shopkeeper", "male", "adult"), "guessed": "kind,speaker,gender,age"}
    assert story[1]["kind"] == "narration" and story[1]["guessed"] == "kind"
    assert story[2] == {**row(1, 2, "dialogue", "Lass", "female", "child"), "guessed": "age"}
    assert story[3] == row(1, 3, "choice")                    # a script-typed line keeps its kind
    assert story[4] == row(1, 4, "unknown")


def test_no_guess_file_means_no_guesses(tmp_path):
    assert textdump.load_guesses(tmp_path / "missing.tsv") == {}
