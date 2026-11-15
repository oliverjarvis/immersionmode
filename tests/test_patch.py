import struct

import pytest

from jpreader_tools import patch
from jpreader_tools.hooks import HookSite
from jpreader_tools.ips import apply_ips
from jpreader_tools.layout import CODE_BASE, CODE_FILE_SIZE, PAYLOAD_BASE, STAGE_BASE, STAGE_MAX, STUB_BASE, STUB_MAX

LDR_PC = 0xE51FF004    # ldr pc, [pc, #-4]


def blank_code():
    code = bytearray(CODE_FILE_SIZE)
    struct.pack_into("<I", code, 0, 0xEB000007)
    struct.pack_into("<I", code, 0x1000, 0xE92D4010)
    struct.pack_into("<I", code, 0x2000, 0xE92D4070)
    return bytes(code)


SITES = [HookSite("thing", "pre", CODE_BASE + 0x1000, 0xE92D4010, ("h1",)),
         HookSite("other", "pre", CODE_BASE + 0x2000, 0xE92D4070, ("h2",))]
TRAMPS = {"thing": PAYLOAD_BASE + 0x40, "other": PAYLOAD_BASE + 0x80}


def word(image, addr):
    return struct.unpack_from("<I", image, addr - CODE_BASE)[0]


def branch_target(image, addr):
    instruction = word(image, addr)
    assert instruction >> 24 == 0xEA
    offset = instruction & 0xFFFFFF
    if offset & 0x800000:
        offset -= 0x1000000
    return addr + 8 + offset * 4


def patched(stub=b"\x11" * 40, payload=bytes(range(256)) * 16):
    code = blank_code()
    return code, apply_ips(code, patch.build_patch(code, SITES, TRAMPS, stub, payload))


def test_entry_branches_to_the_stub():
    _, image = patched()
    assert branch_target(image, CODE_BASE) == STUB_BASE


def test_hooks_reach_their_trampolines_through_veneers_after_the_stub():
    _, image = patched(stub=b"\x11" * 40)
    veneers = [branch_target(image, site.addr) for site in SITES]
    assert veneers == [STUB_BASE + 40, STUB_BASE + 48]
    for veneer, site in zip(veneers, SITES):
        assert word(image, veneer) == LDR_PC
        assert word(image, veneer + 4) == TRAMPS[site.name]


def test_payload_is_staged_in_the_old_bss_pages():
    payload = bytes(range(256)) * 16
    _, image = patched(payload=payload)
    start = STAGE_BASE - CODE_BASE
    assert image[start:start + len(payload)] == payload
    assert len(image) == start + len(payload)


def test_nothing_else_changes():
    code, image = patched(stub=b"\x11" * 40)
    untouched = bytearray(image[:CODE_FILE_SIZE])
    for start, size in ((0, 4), (0x1000, 4), (0x2000, 4), (STUB_BASE - CODE_BASE, 56)):
        untouched[start:start + size] = code[start:start + size]
    assert bytes(untouched) == code


def test_stub_and_veneers_must_fit():
    with pytest.raises(patch.PatchError, match="stub"):
        patch.build_patch(blank_code(), SITES, TRAMPS, bytes(STUB_MAX - 8), b"\x01")


def test_payload_must_fit_in_the_stage():
    with pytest.raises(patch.PatchError, match="payload"):
        patch.build_patch(blank_code(), [], {}, b"\x01" * 4, bytes(STAGE_MAX + 4))


def test_stub_area_must_be_unused():
    code = bytearray(blank_code())
    code[STUB_BASE - CODE_BASE + 8] = 0xFF
    with pytest.raises(patch.PatchError, match="not empty"):
        patch.build_patch(bytes(code), [], {}, b"\x01" * 4, b"\x01")


def test_code_must_be_the_expected_size():
    with pytest.raises(patch.PatchError, match="size"):
        patch.build_patch(bytes(0x1000), [], {}, b"\x01" * 4, b"\x01")


def test_missing_trampoline():
    with pytest.raises(patch.PatchError, match="thing"):
        patch.build_patch(blank_code(), SITES, {}, b"\x01" * 4, b"\x01")


def test_edits_replace_single_game_instructions():
    code = blank_code()
    edit = patch.Edit("thing", CODE_BASE + 0x1000, 0xE92D4010, 0xE3E00000)
    image = apply_ips(code, patch.build_patch(code, [], {}, b"\x01" * 4, b"\x01", [edit]))
    assert word(image, CODE_BASE + 0x1000) == 0xE3E00000


def test_an_edit_refuses_an_unexpected_instruction():
    edit = patch.Edit("thing", CODE_BASE + 0x1000, 0xE92D4070, 0xE3E00000)
    with pytest.raises(patch.PatchError, match="thing"):
        patch.build_patch(blank_code(), [], {}, b"\x01" * 4, b"\x01", [edit])


@pytest.mark.rom
def test_local_wireless_never_claims_the_radio(rom):
    """While the game holds ndm in local wireless mode, a 3DS refuses every Internet connection the
    game's process asks for, Grammar's included. The edit makes the game's local wireless start-up
    take its own path for ndm refusing the radio, so local wireless never starts."""
    code = rom.exefs_file(".code")
    edit = patch.NO_LOCAL_WIRELESS
    assert word(code, edit.addr) == edit.original
    assert word(code, edit.addr - 4) == 0xE3A00002                # mov r0, #2: ndm's local wireless mode
    assert branch_target_bl(code, edit.addr) == patch.GAME_NDM_ENTER_EXCLUSIVE_STATE
    assert word(code, edit.addr + 4) == 0xE2100102                # ands r0, r0, #0x80000000
    assert edit.replacement == 0xE3E00000                         # mvn r0, #0: r0 = -1, a failure
    # The failure path the edit takes is the game's own: ndm's library was opened just before, and the
    # branch it now always takes closes it again and returns the game's error, so nothing is left held.
    assert branch_target_bl(code, 0x260E88) == 0x105B68           # nn::ndm library opened (refcounted)
    assert word(code, edit.addr + 8) == 0x5A000002                # bpl: success skips the next two
    assert branch_target_bl(code, edit.addr + 12) == 0x17A25C     # nn::ndm library closed
    assert word(code, edit.addr + 16) == 0xE1A00005               # mov r0, r5: the game's own error


def test_an_edit_may_not_overlap_a_hook():
    edit = patch.Edit("thing", CODE_BASE + 0x1000, 0xE92D4010, 0xE3E00000)
    with pytest.raises(patch.PatchError, match="overlaps"):
        patch.build_patch(blank_code(), SITES, TRAMPS, b"\x01" * 4, b"\x01", [edit])


def test_an_edit_may_not_touch_the_entry_or_the_stub():
    code = blank_code()
    for addr in (CODE_BASE, STUB_BASE + 4):
        edit = patch.Edit("thing", addr, word(code, addr), 0xE3E00000)
        with pytest.raises(patch.PatchError, match="overlaps"):
            patch.build_patch(code, [], {}, b"\x01" * 4, b"\x01", [edit])


def branch_target_bl(image, addr):
    instruction = word(image, addr)
    assert instruction >> 24 == 0xEB
    offset = instruction & 0xFFFFFF
    if offset & 0x800000:
        offset -= 0x1000000
    return addr + 8 + offset * 4
