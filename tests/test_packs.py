import struct

from jpreader_tools import packs


def test_lz11_literals_and_back_references():
    # "abc" as literals, then a 6-byte copy from 3 back: "abcabcabc"
    data = bytes([0x11, 9, 0, 0, 0b00010000]) + b"abc" + bytes([0x50, 0x02])
    assert packs.lz11(data) == b"abcabcabc"


def test_unpack_leaves_plain_data_alone():
    assert packs.unpack(b"ZS\x00\x00") == b"ZS\x00\x00"


def test_mini_pack():
    parts = [b"one", b"three"]
    head = 4 + 4 * (len(parts) + 1)
    offsets = [head, head + 3, head + 8]
    blob = b"ZS" + struct.pack("<H", 2) + struct.pack("<3I", *offsets) + b"".join(parts)
    assert packs.mini(blob) == ("ZS", parts)
