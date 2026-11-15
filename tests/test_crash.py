import struct

import pytest

from jpreader_tools import crash

SYMBOLS = {"jpr_init": 0x0FE80000, "jpr_h_present": 0x0FE81000, "jpr_panel_draw": 0x0FE82000}


def dump(pc=0x0FE81010, lr=0x0034E000, kind=3, processor=11, title=0x00040000001B5100, cut=0):
    regs = list(range(13)) + [0x0FFFFF00, lr, pc, 0x10, 0, 0x805, 0]
    body = struct.pack(f"<{len(regs)}I", *regs) + bytes(8) + bytes(16) + b"Ultra\0\0\0" + struct.pack("<Q", title)
    head = struct.pack("<2I2H2H6I", 0xDEADC0DE, 0xDEADCAFE, 2, 1, processor, 1, kind, 40 + len(body),
                       4 * len(regs), 8, 16, 16)
    blob = head + body
    return blob[:len(blob) - cut] if cut else blob


def test_parse_and_describe():
    d = crash.parse(dump())
    assert (d.processor, d.core, d.kind) == (11, 1, "data abort")
    assert d.registers["PC"] == 0x0FE81010 and d.title_id == 0x00040000001B5100
    lines = crash.describe(d, SYMBOLS)
    assert any("PC" in line and "jpr_h_present+0x10" in line for line in lines)
    assert any("LR" in line and "game code 0x0034e000" in line for line in lines)


def test_other_processes_and_bad_files():
    assert any("not Ultra Moon" in line for line in crash.describe(crash.parse(dump(title=0x0004003000008F02)), SYMBOLS))
    with pytest.raises(crash.CrashError, match="ARM9"):
        crash.parse(dump(processor=9))
    with pytest.raises(crash.CrashError, match="not a Luma"):
        crash.parse(b"hello" * 20)
    with pytest.raises(crash.CrashError, match="short"):
        crash.parse(dump(cut=20))


def test_cli(tmp_path, monkeypatch, capsys):
    from jpreader_tools import cli

    (tmp_path / "build" / "payload-release").mkdir(parents=True)
    (tmp_path / "build" / "payload-release" / "symbols.txt").write_text(
        "".join(f"{value:08x} T {name}\n" for name, value in SYMBOLS.items()))
    path = tmp_path / "crash_dump_00000001.dmp"
    path.write_bytes(dump())
    monkeypatch.setattr(cli, "find_root", lambda: tmp_path)
    assert cli.main(["crash", str(path)]) == 0
    assert "jpr_h_present+0x10" in capsys.readouterr().out
    path.write_bytes(b"junk")
    assert cli.main(["crash", str(path)]) == 1


def test_jpreader_stub_and_staging_are_not_game_code():
    from jpreader_tools.layout import STAGE_BASE, STUB_BASE

    assert "jpreader stub" in crash.where(STUB_BASE + 0x18, SYMBOLS)
    assert "game data" in crash.where(STAGE_BASE + 0x100, SYMBOLS)
    assert crash.where(0x0034E000, SYMBOLS) == "game code 0x0034e000"
