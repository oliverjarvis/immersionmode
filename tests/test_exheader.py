import struct

import pytest

from jpreader_tools import exheader


def make_exheader():
    exh = bytearray(0x800)
    struct.pack_into("<III", exh, 0x30, 0x667000, 61, 0x3CD48)
    struct.pack_into("<I", exh, 0x3C, 0x301BC)
    for base in (0x370, 0x770):
        caps = [0xF0000006, 0xF1FFFF00] + [0xFFFFFFFF] * 26
        struct.pack_into("<28I", exh, base, *caps)
    return bytes(exh)


def test_allowed_svcs_reads_masks():
    assert exheader.allowed_svcs(make_exheader()) == {1, 2} | set(range(32, 48))


def test_luma_variant_grows_data_over_bss_without_moving_the_image_end():
    out = exheader.patch_exheader(make_exheader(), "luma")
    assert len(out) == 0x400
    assert struct.unpack_from("<III", out, 0x30) == (0x667000, 0x6D, 0x6D000)
    assert struct.unpack_from("<I", out, 0x3C)[0] == 0
    assert 0x667000 + 0x6D000 == exheader.IMAGE_END


def test_azahar_variant_keeps_sizes():
    out = exheader.patch_exheader(make_exheader(), "azahar")
    assert len(out) == 0x800
    assert struct.unpack_from("<III", out, 0x30) == (0x667000, 61, 0x3CD48)
    assert struct.unpack_from("<I", out, 0x3C)[0] == 0x301BC


@pytest.mark.parametrize("variant", ["luma", "azahar"])
def test_both_variants_allow_the_extra_svcs(variant):
    out = exheader.patch_exheader(make_exheader(), variant)
    assert {0x70, 0x54, 1, 2} <= exheader.allowed_svcs(out)
    if variant == "azahar":
        assert {0x70, 0x54} <= exheader.allowed_svcs(out, 0x770)


def test_existing_svc_table_entry_is_extended():
    exh = bytearray(make_exheader())
    struct.pack_into("<I", exh, 0x370 + 8, 0xF4000001)        # index 4 already present
    out = exheader.patch_exheader(bytes(exh), "azahar")
    assert struct.unpack_from("<I", out, 0x370 + 8)[0] == 0xF4010001


def test_unknown_variant():
    with pytest.raises(exheader.ExheaderError, match="variant"):
        exheader.patch_exheader(make_exheader(), "citra")


def test_data_segment_already_past_the_image_end():
    exh = bytearray(make_exheader())
    struct.pack_into("<III", exh, 0x30, 0x667000, 0x70, 0x70000)
    with pytest.raises(exheader.ExheaderError, match="image end"):
        exheader.patch_exheader(bytes(exh), "luma")


def test_no_free_capability_slot():
    exh = bytearray(make_exheader())
    struct.pack_into("<28I", exh, 0x370, *([0xF0000006] * 28))
    with pytest.raises(exheader.ExheaderError, match="slot"):
        exheader.patch_exheader(bytes(exh), "luma")


@pytest.mark.rom
def test_real_exheader(rom):
    original = rom.exheader()
    before = exheader.allowed_svcs(original)
    assert 0x70 not in before and 0x54 not in before
    assert before <= exheader.allowed_svcs(original, 0x770)
    luma = exheader.patch_exheader(original, "luma")
    assert exheader.allowed_svcs(luma) == before | {0x70, 0x54}
    changed = [i for i in range(0x400) if luma[i] != original[i]]
    assert all(0x30 <= i < 0x40 or 0x370 <= i < 0x3E0 for i in changed)
