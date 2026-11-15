import pytest

from jpreader_tools import ips


def test_literal_record():
    patch = ips.build_ips([(0x10, b"\x01\x02\x03")])
    assert patch == b"PATCH" + b"\x00\x00\x10" + b"\x00\x03" + b"\x01\x02\x03" + b"EOF"


def test_run_of_one_value_uses_run_length_record():
    patch = ips.build_ips([(0x20, bytes(100))])
    assert patch == b"PATCH" + b"\x00\x00\x20" + b"\x00\x00" + b"\x00\x64" + b"\x00" + b"EOF"


def test_long_data_is_split_at_64k():
    data = bytes(range(256)) * 300           # 76,800 bytes, not a single value
    base = bytes(0x20000)
    patched = ips.apply_ips(base, ips.build_ips([(0x100, data)]))
    assert patched[0x100:0x100 + len(data)] == data
    assert patched[:0x100] == bytes(0x100)


def test_apply_grows_the_image():
    patched = ips.apply_ips(b"\xAA" * 4, ips.build_ips([(8, b"\x01\x02"), (16, bytes(0x30000))]))
    assert len(patched) == 16 + 0x30000
    assert patched[:10] == b"\xAA" * 4 + bytes(4) + b"\x01\x02"


def test_empty_records_are_skipped():
    assert ips.build_ips([(5, b"")]) == b"PATCHEOF"


def test_record_at_eof_offset_is_refused():
    with pytest.raises(ips.IpsError, match="EOF"):
        ips.build_ips([(0x454F46, b"\x01")])


def test_record_beyond_16mb_is_refused():
    with pytest.raises(ips.IpsError, match="16 MB"):
        ips.build_ips([(0xFFFFFF, b"\x01\x02")])


def test_apply_rejects_other_files():
    with pytest.raises(ips.IpsError):
        ips.apply_ips(b"", b"NOTIPS")
