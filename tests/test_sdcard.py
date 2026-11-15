import pytest

from jpreader_tools import sdcard

TID = "00040000001B5100"


def make_dist(tmp_path):
    dist = tmp_path / "dist"
    titles = dist / "3ds" / "luma" / "titles" / TID
    titles.mkdir(parents=True)
    (titles / "code.ips").write_bytes(b"PATCHEOF")
    (titles / "exheader.bin").write_bytes(bytes(0x400))
    data = dist / "3ds" / "jpreader"
    data.mkdir()
    for name in ("annot.bin", "dict.bin", "font.bin", "NOTICE.txt"):
        (data / name).write_bytes(name.encode())
    (data / "config.txt").write_text("debug=0\nkokoro_token=kkr_x\n")
    return dist


def test_install_copies_both_folders(tmp_path):
    dist = make_dist(tmp_path)
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    (card / "jpreader").mkdir()
    (card / "jpreader" / "annot.bin").write_bytes(b"old 3A file")
    written = sdcard.install(dist, card)
    assert (card / "luma" / "titles" / TID / "code.ips").read_bytes() == b"PATCHEOF"
    assert (card / "jpreader" / "annot.bin").read_bytes() == b"annot.bin"      # the old file is replaced
    assert len(written) == 7
    assert "debug=0" in (card / "jpreader" / "config.txt").read_text()


def test_debug_line(tmp_path):
    dist = make_dist(tmp_path)
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    sdcard.install(dist, card, debug_line=True)
    text = (card / "jpreader" / "config.txt").read_text()
    assert "debug=1" in text and "debug=0" not in text and "kokoro_token=kkr_x" in text


def test_refuses_a_folder_that_is_not_a_luma_card(tmp_path):
    dist = make_dist(tmp_path)
    with pytest.raises(sdcard.CardError, match="luma"):
        sdcard.install(dist, tmp_path / "somewhere")
    assert not (tmp_path / "somewhere").exists()


def test_refuses_without_a_release_build(tmp_path):
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    with pytest.raises(sdcard.CardError, match="jpreader build"):
        sdcard.install(tmp_path / "empty", card)


def test_cli_never_prints_the_key(tmp_path, monkeypatch, capsys):
    from jpreader_tools import cli

    make_dist(tmp_path)
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    monkeypatch.setattr(cli, "find_root", lambda: tmp_path)
    assert cli.main(["sd", str(card)]) == 0
    out = capsys.readouterr().out
    assert "config.txt: kokoro linked" in out and "kkr_x" not in out
    assert "game patching" in out
    assert cli.main(["sd", str(tmp_path / "nowhere")]) == 1


def test_a_file_already_on_the_card_is_not_copied_again(tmp_path):
    dist = make_dist(tmp_path)
    (dist / "3ds" / "jpreader" / "voice-0.bin").write_bytes(b"x" * 1000)
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    first = sdcard.install(dist, card)
    second = sdcard.install(dist, card)
    assert card / "jpreader" / "voice-0.bin" in first and card / "jpreader" / "voice-0.bin" not in second
    (dist / "3ds" / "jpreader" / "voice-0.bin").write_bytes(b"y" * 1001)           # a new build changes it
    assert card / "jpreader" / "voice-0.bin" in sdcard.install(dist, card)


def test_a_file_the_build_no_longer_makes_is_removed(tmp_path):
    dist = make_dist(tmp_path)
    card = tmp_path / "card"
    (card / "luma").mkdir(parents=True)
    (card / "jpreader").mkdir()
    (card / "jpreader" / "heads.bin").write_bytes(b"8B headword hashes")      # ids.bin v2 replaced it (9C)
    (card / "jpreader" / "kokoro.bin").write_bytes(b"the 3DS's own")           # the 3DS's files stay
    sdcard.install(dist, card)
    assert not (card / "jpreader" / "heads.bin").exists()
    assert (card / "jpreader" / "kokoro.bin").exists()
