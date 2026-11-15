import struct

import pytest

from jpreader_tools import rom as romlib


def make_garc(files):
    data = b""
    spans = []
    for content in files:
        start = len(data)
        data += content
        spans.append((start, len(data), len(content)))
        data += b"\xff" * (-len(data) % 4)
    fato = b"OTAF" + struct.pack("<IHH", 12 + 4 * len(files), len(files), 0xFFFF)
    fato += b"".join(struct.pack("<I", i * 16) for i in range(len(files)))
    entries = b"".join(struct.pack("<IIII", 1, s, e, n) for s, e, n in spans)
    fatb = b"BTAF" + struct.pack("<II", 12 + len(entries), len(files)) + entries
    fimb = b"BMIF" + struct.pack("<II", 12, len(data))
    data_off = 0x24 + len(fato) + len(fatb) + len(fimb)
    header = b"CRAG" + struct.pack("<IHHIII", 0x24, 0xFEFF, 0x0600, 4, data_off, data_off + len(data))
    header += struct.pack("<III", 0, 0, 4)
    return header + fato + fatb + fimb + data


def test_parse_garc_returns_each_file():
    assert romlib.parse_garc(make_garc([b"alpha", b"be", b""])) == [b"alpha", b"be", b""]


def test_parse_garc_rejects_other_data():
    with pytest.raises(ValueError):
        romlib.parse_garc(b"NOPE" + bytes(64))


def test_identity_accepts_target():
    romlib.check_rom_identity(romlib.TITLE_ID, romlib.EXPECTED_CODE_SHA256, True)


@pytest.mark.parametrize(
    "title_id, sha, no_crypto, fragment",
    [
        (romlib.TITLE_ID, romlib.EXPECTED_CODE_SHA256, False, "encrypted"),
        (0x00040000001B5000, romlib.EXPECTED_CODE_SHA256, True, "title ID"),
        (romlib.TITLE_ID, "00" * 32, True, "v1.0"),
    ],
)
def test_identity_rejects(title_id, sha, no_crypto, fragment):
    with pytest.raises(romlib.WrongRomError, match=fragment):
        romlib.check_rom_identity(title_id, sha, no_crypto)


def test_open_rejects_non_cartridge_file(tmp_path):
    bogus = tmp_path / "bogus.cci"
    bogus.write_bytes(bytes(0x4000))
    with pytest.raises(romlib.WrongRomError, match="NCSD"):
        romlib.Rom(bogus)


@pytest.mark.rom
def test_rom_is_the_target(rom):
    rom.verify()
    assert rom.title_id == 0x00040000001B5100
    assert rom.no_crypto


@pytest.mark.rom
def test_exefs_code_size(rom):
    assert len(rom.exefs_file(".code")) == 0x5A4000


@pytest.mark.rom
def test_exheader_text_segment(rom):
    exh = rom.exheader()
    assert len(exh) == 0x800
    assert struct.unpack_from("<III", exh, 0x10) == (0x100000, 1210, 0x4B99F8)


@pytest.mark.rom
def test_romfs_lookup(rom):
    assert rom.romfs_file("static.crs")[0x80:0x84] == b"CRO0"
    assert len(romlib.parse_garc(rom.romfs_file("a/0/4/1"))) == 1124


@pytest.mark.rom
def test_romfs_missing_file(rom):
    with pytest.raises(FileNotFoundError):
        rom.romfs_file("a/9/9/9")


def test_verify_reports_encryption_before_reading_the_exefs(tmp_path):
    image = bytearray(0x8000)
    image[0x100:0x104] = b"NCSD"
    struct.pack_into("<II", image, 0x120, 0x4000 // 0x200, 0x20)
    ncch = 0x4000
    image[ncch + 0x100:ncch + 0x104] = b"NCCH"
    struct.pack_into("<Q", image, ncch + 0x118, romlib.TITLE_ID)
    image[ncch + 0x18F] = 0x00                                  # encrypted
    struct.pack_into("<I", image, ncch + 0x1A0, 2)              # ExeFS at +0x400
    image[ncch + 0x400:ncch + 0x600] = bytes(range(0x80, 0x100)) * 4   # ciphertext, not ASCII
    path = tmp_path / "encrypted.cci"
    path.write_bytes(bytes(image))
    with romlib.Rom(path) as rom, pytest.raises(romlib.WrongRomError, match="encrypted"):
        rom.verify()
