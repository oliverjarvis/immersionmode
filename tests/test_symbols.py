import struct

import pytest

from jpreader_tools import symbols
from jpreader_tools.layout import CODE_BASE


def make_crs(exports, segments):
    header = bytearray(0x138)
    header[0x80:0x84] = b"CRO0"
    seg_off = len(header)
    seg_blob = b"".join(struct.pack("<III", addr, size, ident) for addr, size, ident in segments)
    exp_off = seg_off + len(seg_blob)
    str_off = exp_off + 8 * len(exports)
    names = b""
    table = b""
    for name, segment, offset in exports:
        table += struct.pack("<II", str_off + len(names), (offset << 4) | segment)
        names += name.encode() + b"\0"
    struct.pack_into("<4I", header, 0xC8, seg_off, len(segments), exp_off, len(exports))
    return bytes(header) + seg_blob + table + names


def test_parse_crs_exports():
    blob = make_crs([("foo", 0, 0x10), ("bar", 1, 4)], [(0x100000, 0x1000, 0), (0x5BA000, 0x100, 1)])
    assert symbols.parse_crs_exports(blob) == {"foo": 0x100010, "bar": 0x5BA004}


def test_parse_crs_rejects_other_files():
    with pytest.raises(ValueError):
        symbols.parse_crs_exports(bytes(0x200))


def make_code_with_vtable():
    code = bytearray(0x400)
    name = b"N4test5ThingE\0"
    code[0x300:0x300 + len(name)] = name
    struct.pack_into("<II", code, 0x200, 0x00500000, CODE_BASE + 0x300)        # typeinfo
    struct.pack_into("<IIIII", code, 0x100, 0, CODE_BASE + 0x200, 0x100100, 0x100200, 0)  # vtable
    return bytes(code)


def test_vtable_functions():
    assert symbols.vtable_functions(make_code_with_vtable(), "N4test5ThingE") == [0x100100, 0x100200]


def test_vtable_functions_unknown_class():
    with pytest.raises(KeyError):
        symbols.vtable_functions(make_code_with_vtable(), "N4test5OtherE")


POP_PC = 0xE8BD8070      # pop {r4, r5, r6, pc}


def test_is_exit():
    assert symbols.is_exit(0xE12FFF1E)
    assert symbols.is_exit(POP_PC)
    assert not symbols.is_exit(0xE8BD0070)          # pop without pc
    assert not symbols.is_exit(0xE1A00000)


def test_find_exit_takes_the_first_return_of_either_kind():
    code = struct.pack("<4I", 0xE3A00000, POP_PC, 0xE12FFF1E, 0)
    assert symbols.find_exit(code, CODE_BASE) == CODE_BASE + 4


def test_find_exit_gives_up():
    with pytest.raises(ValueError):
        symbols.find_exit(bytes(64), CODE_BASE, limit=64)


def test_vtable_address():
    assert symbols.vtable_address(make_code_with_vtable(), "N4test5ThingE") == CODE_BASE + 0x108


def test_find_unique():
    code = bytes(8) + b"\x01\x02\x03\x04" + bytes(4) + b"\x01\x02\x03\x04"
    assert symbols.find_unique(code, b"\x01\x02\x03\x04" + bytes(4) + b"\x01") == CODE_BASE + 8


def test_find_unique_refuses_zero_or_many():
    code = b"\x01\x02\x03\x04" * 2
    with pytest.raises(ValueError, match="2 times"):
        symbols.find_unique(code, b"\x01\x02\x03\x04")
    with pytest.raises(ValueError, match="0 times"):
        symbols.find_unique(code, b"\x09\x09\x09\x09")


def test_find_unique_ignores_unaligned_matches():
    code = b"\x00\x01\x02\x03\x04\x00\x00\x00" + b"\x01\x02\x03\x04"
    assert symbols.find_unique(code, b"\x01\x02\x03\x04") == CODE_BASE + 8


@pytest.mark.rom
def test_new_addresses_from_rom(rom):
    from jpreader_tools.hooks import PRESENT_SIGNATURE

    code = rom.exefs_file(".code")
    assert symbols.find_unique(code, PRESENT_SIGNATURE) == 0x548054
    assert symbols.vtable_address(code, "N5print13MessageWindowE") == 0x64AEDC
    touch = symbols.vtable_functions(code, "N4gfl22ui14CTR_TouchPanelE")[3]
    assert (touch, symbols.find_exit(code, touch)) == (0x4B4AA8, 0x4B4AD4)
    stick = symbols.vtable_functions(code, "N4gfl22ui9CTR_StickE")[3]
    assert (stick, symbols.find_exit(code, stick)) == (0x4B50F0, 0x4B511C)


@pytest.mark.rom
def test_addresses_from_rom(rom):
    exports = symbols.parse_crs_exports(rom.romfs_file("static.crs"))
    assert len(exports) == 4916
    assert exports["_ZN4gfl23str7MsgData9GetStringEjRNS0_6StrBufE"] == 0x35E614
    assert exports["_ZN3App4Tool10TalkWindow10SetMessageEPKN4gfl23str6StrBufE"] == 0x2CF9E0
    assert exports["_ZN5print7WordSet6ExpandEPN4gfl23str6StrBufEPKS3_"] == 0x3D3C58


@pytest.mark.rom
def test_vtables_from_rom(rom):
    code = rom.exefs_file(".code")
    assert symbols.vtable_functions(code, "N4gfl23str7MsgDataE") == [0x35EB48, 0x35EA94]
    poll = symbols.vtable_functions(code, "N4gfl22ui10CTR_ButtonE")[3]
    assert poll == 0x4B3E8C
    assert symbols.find_exit(code, poll) == 0x4B4000
