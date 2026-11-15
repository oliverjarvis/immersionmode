import struct
import subprocess

import pytest

from jpreader_tools import voice

FFMPEG = "/opt/homebrew/bin/ffmpeg"


def tone(path, seconds=1.0, hz=440):
    subprocess.run([FFMPEG, "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=f={hz}:d={seconds}",
                    "-ac", "1", "-ar", "44100", str(path)], check=True)


@pytest.fixture
def clips(tmp_path):
    tts = tmp_path / "build" / "tts"
    (tts / "clips").mkdir(parents=True)
    tone(tts / "clips" / "0027-001.mp3")
    tone(tts / "clips" / "0027-000.mp3", hz=660)
    (tts / "clips.csv").write_text(
        "﻿clip,story_file,line,scene,speaker,voice,english,japanese,directions\n"
        "clips/0027-001.mp3,27,1,s,Mom,Alice,e,j,d\n"
        "clips/0027-000.mp3,27,0,s,Mom,Alice,e,j,d\n"
        "clips/missing.mp3,27,2,s,Mom,Alice,e,j,d\n"
        "clips/0027-000.mp3,x,3,s,Mom,Alice,e,j,d\n", encoding="utf-8")
    return tmp_path


def entries(index: bytes) -> list[tuple]:
    count = struct.unpack_from("<I", index, 8)[0]
    return [struct.unpack_from("<HHHHIIII", index, 20 + 24 * n) for n in range(count)]


def test_voice_bin_indexes_clips_held_in_parts(clips):
    files, report = voice.build_voice(clips)
    index = files["voice.bin"].read_bytes()
    magic, version, count, rate, parts = struct.unpack_from("<4sIIII", index)
    assert (magic, version, count, rate, parts) == (b"JPRV", 2, 2, 22050, 1)
    rows = entries(index)
    assert [(f, l) for f, l, *_ in rows] == [(27, 0), (27, 1)]                     # sorted by (file, line)
    part = files["voice-0.bin"].read_bytes()
    for _f, _l, number, _z, normal_at, normal_n, slow_at, slow_n in rows:
        assert number == 0 and normal_at % 32 == 0 and slow_at % 32 == 0
        assert abs(normal_n - 22050) <= 22050 * 0.02
        assert abs(slow_n - 22050 / 0.7) <= 22050 / 0.7 * 0.02                      # 0.7x: longer, same pitch
        assert normal_at + 2 * normal_n <= len(part) and slow_at + 2 * slow_n <= len(part)
    assert report["clips"] == 2 and report["parts"] == 1 and len(report["skipped"]) == 2


def test_parts_stay_under_the_limit_and_no_clip_is_split(clips):
    files, report = voice.build_voice(clips, part_max=120_000)       # one clip (normal + slow) is ~107 KB
    rows = entries(files["voice.bin"].read_bytes())
    assert report["parts"] == 2 and [r[2] for r in rows] == [0, 1]
    for number, (_f, _l, part, _z, normal_at, normal_n, slow_at, slow_n) in enumerate(rows):
        size = files[f"voice-{part}.bin"].stat().st_size
        assert size <= 120_000 and slow_at + 2 * slow_n <= size


def test_a_rebuild_with_the_same_clips_reuses_the_files(clips):
    files, _ = voice.build_voice(clips)
    stamp = files["voice-0.bin"].stat().st_mtime_ns
    again, report = voice.build_voice(clips)
    assert again["voice-0.bin"].stat().st_mtime_ns == stamp and report["reused"]


def test_system_clips_are_filed_apart_from_story_ones(tmp_path):
    """System file 38 (the opening call) and story file 38 are different text: system keys carry 0x8000."""
    tts = tmp_path / "build" / "tts"
    (tts / "clips").mkdir(parents=True)
    tone(tts / "clips" / "0038-001.mp3")
    tone(tts / "clips" / "S0038-001.mp3", hz=660)
    (tts / "clips.csv").write_text(
        "\ufeffclip,set,story_file,line,scene,speaker,voice,english,japanese,directions\n"
        "clips/S0038-001.mp3,system,38,1,s,Professor Kukui,George,e,j,d\n"
        "clips/0038-001.mp3,story,38,1,s,Hau,Liam,e,j,d\n"
        "clips/S0038-001.mp3,other,38,2,s,Hau,Liam,e,j,d\n"
        "clips/S0038-001.mp3,system,32768,3,s,Hau,Liam,e,j,d\n", encoding="utf-8")
    files, report = voice.build_voice(tmp_path)
    assert [(f, l) for f, l, *_ in entries(files["voice.bin"].read_bytes())] == [(38, 1), (0x8000 | 38, 1)]
    assert report["clips"] == 2 and len(report["skipped"]) == 2


def test_lines_sharing_a_take_share_its_samples(clips):
    csv_path = clips / "build" / "tts" / "clips.csv"
    csv_path.write_text(csv_path.read_text(encoding="utf-8") + "clips/0027-000.mp3,30,5,s,Mom,Alice,e,j,d\n",
                        encoding="utf-8")
    files, report = voice.build_voice(clips)
    rows = {(f, l): rest for f, l, *rest in entries(files["voice.bin"].read_bytes())}
    assert report["clips"] == 3 and rows[(30, 5)] == rows[(27, 0)]
    one = 2 * (rows[(27, 0)][3] + rows[(27, 0)][5])                                   # its normal + slow samples
    assert files["voice-0.bin"].stat().st_size < 2.5 * one                             # two clips stored, not three


def test_a_rebuild_leaves_a_hardlinked_dist_with_its_own_files(clips, tmp_path):
    """dist/ hardlinks voice.bin and its parts: a rebuild must make new files, so an older dist keeps an index that
    matches its samples."""
    import os

    files, _ = voice.build_voice(clips)
    dist = tmp_path / "dist"
    dist.mkdir()
    for name in ("voice.bin", "voice-0.bin"):
        os.link(files[name], dist / name)
    old_index, old_part = (dist / "voice.bin").read_bytes(), (dist / "voice-0.bin").read_bytes()
    tts = clips / "build" / "tts"
    tone(tts / "clips" / "0027-005.mp3", hz=880)
    with open(tts / "clips.csv", "a", encoding="utf-8") as fh:
        fh.write("clips/0027-005.mp3,27,5,s,Mom,Alice,e,j,d\n")
    files, report = voice.build_voice(clips)
    assert report["clips"] == 3 and files["voice.bin"].read_bytes() != old_index
    assert (dist / "voice.bin").read_bytes() == old_index and (dist / "voice-0.bin").read_bytes() == old_part


def test_no_csv_no_voice(tmp_path):
    assert voice.build_voice(tmp_path) == ({}, {"clips": 0, "skipped": [], "reason": "no build/tts/clips.csv"})
