import struct

import pytest

from jpreader_tools import hooks
from jpreader_tools.layout import CODE_BASE
from jpreader_tools.symbols import parse_crs_exports

PUSH = struct.pack("<I", 0xE92D4010)       # push {r4, lr}
BX_LR = struct.pack("<I", 0xE12FFF1E)
MOV = struct.pack("<I", 0xE1A04000)        # mov r4, r0


def test_arm_b_forward():
    assert hooks.arm_b(0x100000, 0x5B99F8) == 0xEA12E67C


def test_arm_b_backward():
    assert hooks.arm_b(0x100010, 0x100000) == 0xEAFFFFFA


def test_arm_b_out_of_range():
    with pytest.raises(hooks.HookError, match="range"):
        hooks.arm_b(0x100000, 0x100000 + 0x4000000)


def test_make_site_accepts_push():
    site = hooks.make_site("x", "pre", CODE_BASE, PUSH + MOV, ("handler",))
    assert site == hooks.HookSite("x", "pre", CODE_BASE, 0xE92D4010, ("handler",))


def test_entry_redirect():
    code = struct.pack("<I", 0xEB000007)   # bl CODE_BASE + 0x24
    assert hooks.entry_redirect(code) == (CODE_BASE + 0x24, CODE_BASE + 4)


def test_entry_redirect_refuses_other_instructions():
    with pytest.raises(hooks.HookError, match="entry"):
        hooks.entry_redirect(MOV)


def test_render_pre_trampoline():
    source = hooks.render_trampolines([hooks.HookSite("getstring", "pre", 0x35E614, 0xE92D4FF8, ("jpr_h_getstring",))])
    assert '.section .text.tramp, "ax", %progbits' in source
    assert "jpr_tramp_getstring:" in source
    assert "bl    jpr_h_getstring" in source
    assert ".word 0xe92d4ff8" in source
    assert "ldr   pc, =0x0035e618" in source


def test_render_prepost_trampoline():
    site = hooks.HookSite("talk_set_buf", "prepost", 0x2CF9E0, 0xE92D40F8, ("jpr_h_talk_set_buf", "jpr_h_talk_set_done"))
    source = hooks.render_trampolines([site])
    assert "bl    jpr_ret_push" in source
    assert "adr   lr, 1f" in source
    assert "bl    jpr_h_talk_set_done" in source
    assert "bl    jpr_ret_pop" in source
    assert source.index("bl    jpr_h_talk_set_buf") < source.index(".word 0xe92d40f8") < source.index("bl    jpr_h_talk_set_done")


POP_PC = struct.pack("<I", 0xE8BD8070)     # pop {r4, r5, r6, pc}


def test_make_site_refuses_unexpected_instruction():
    with pytest.raises(hooks.HookError, match="not a push"):
        hooks.make_site("x", "pre", CODE_BASE, MOV + PUSH, ("handler",))
    with pytest.raises(hooks.HookError, match="not a push"):
        hooks.make_site("x", "pre_regs", CODE_BASE, MOV + PUSH, ("handler",))
    with pytest.raises(hooks.HookError, match="not a return"):
        hooks.make_site("x", "exit", CODE_BASE, PUSH, ("handler",))
    with pytest.raises(hooks.HookError, match="kind"):
        hooks.make_site("x", "tail", CODE_BASE, BX_LR, ("handler",))


def test_exit_sites_accept_both_kinds_of_return():
    assert hooks.make_site("a", "exit", CODE_BASE, BX_LR, ("h",)).displaced == 0xE12FFF1E
    assert hooks.make_site("b", "exit", CODE_BASE, POP_PC, ("h",)).displaced == 0xE8BD8070


def test_render_exit_trampoline_passes_the_registers_and_then_returns():
    source = hooks.render_trampolines([hooks.HookSite("stick", "exit", 0x4B511C, 0xE8BD8070, ("jpr_h_stick_tail",))])
    body = source[source.index("jpr_tramp_stick:"):]
    lines = [line.strip() for line in body.splitlines()[1:7]]
    assert lines == ["push  {r0-r12, lr}", "mov   r0, sp", "bl    jpr_h_stick_tail",
                     "pop   {r0-r12, lr}", ".word 0xe8bd8070", ".ltorg"]


def test_render_pre_regs_trampoline_continues_into_the_function():
    source = hooks.render_trampolines([hooks.HookSite("present", "pre_regs", 0x548054, 0xE92D43F0, ("jpr_h_present",))])
    body = source[source.index("jpr_tramp_present:"):]
    lines = [line.strip() for line in body.splitlines()[1:7]]
    assert lines == ["push  {r0-r12, lr}", "mov   r0, sp", "bl    jpr_h_present",
                     "pop   {r0-r12, lr}", ".word 0xe92d43f0", "ldr   pc, =0x00548058"]


@pytest.mark.rom
def test_plan_from_rom(rom):
    code = rom.exefs_file(".code")
    syms = parse_crs_exports(rom.romfs_file("static.crs"))
    release = {site.name: site for site in hooks.plan_hooks(code, syms, debug=False)}
    assert {name: (site.kind, site.addr) for name, site in release.items()} == {
        "msgdata_ctor_ids": ("pre", 0x35E9AC),
        "msgdata_ctor_mem": ("pre", 0x35E940),
        "msgdata_dtor_a": ("pre", 0x35EB48),
        "msgdata_dtor_b": ("pre", 0x35EA94),
        "getstring": ("pre", 0x35E614),
        "expand": ("pre", 0x3D3C58),
        "talk_set_buf": ("prepost", 0x2CF9E0),
        "talk_set_id": ("prepost", 0x2CFB20),
        "talk_call": ("pre", 0x2CF9BC),
        "talk_end": ("pre", 0x2D0810),
        "present": ("pre_regs", 0x548054),
        "touch": ("exit", 0x4B4AD4),
        "view_dtor": ("pre", 0x2EEEA4),
        "dsp_address": ("replace", 0x172BE4),
        "player_vol_set": ("replace", 0x3BA324),          # stage 11: the mixer's levels ride on the game's
        "player_vol_get": ("replace", 0x4B7A90),
        "listmenu_add": ("pre", 0x2D8EB0),                # stage 12: choice menus
        "listmenu_start": ("pre", 0x2D8C30),
        "menuwin_add_buf": ("pre", 0x2F1610),
        "menuwin_start": ("pre", 0x2F19AC),
        "menuwin_start_disp": ("pre", 0x2F11A0),
        "menuwin_end": ("pre", 0x2F1694),
        "pad": ("exit", 0x4B4000),                        # stage 13: SELECT opens the peek, in release builds too
        "cross": ("exit", 0x4B4450),
        "stick": ("exit", 0x4B511C),
        "msgwin_set": ("pre", 0x3D047C),                  # stage 13: message windows
        "lyt_draw": ("pre", 0x359400),                    # stage 14: the layouts drawn, read by the peek
    }
    assert hooks.screen_constants(syms)["JPR_WAZA_GET_TYPE"] == 0x3266F8
    assert release["player_vol_set"].handlers == ("jpr_h_player_vol_set_asm",)
    assert release["pad"].handlers == ("jpr_h_pad_exit",) and release["stick"].handlers == ("jpr_h_stick_exit",)
    assert release["getstring"].displaced == 0xE92D4FF8
    debug = {site.name: site for site in hooks.plan_hooks(code, syms, debug=True)}
    assert debug["pad"] == hooks.HookSite("pad", "exit", 0x4B4000, 0xE12FFF1E, ("jpr_h_pad_exit",))
    assert debug["cross"] == hooks.HookSite("cross", "exit", 0x4B4450, 0xE12FFF1E, ("jpr_h_cross_exit",))
    assert debug["stick"] == hooks.HookSite("stick", "exit", 0x4B511C, 0xE8BD8070, ("jpr_h_stick_exit",))
    assert set(debug) == set(release)
    assert hooks.entry_redirect(code) == (0x100024, 0x100004)
    assert hooks.game_constants(code) == {"JPR_MSGWIN_VTABLE": 0x64AEDC, "JPR_BUTTON_VTABLE": 0x648858,
                                          "JPR_HID_SELECT_FLAG": 0x69F4E0, "JPR_TEXTBOX_VTABLE": 0x644D08,
                                          "JPR_PARTS_VTABLE": 0x644B68}


@pytest.mark.rom
def test_plan_reports_missing_symbol(rom):
    with pytest.raises(hooks.HookError, match="msgdata_ctor_ids"):
        hooks.plan_hooks(rom.exefs_file(".code"), {}, debug=False)


def test_a_replace_site_hands_the_whole_call_to_the_handler():
    site = hooks.make_site("dsp_address", "replace", CODE_BASE, MOV + PUSH, ("jpr_dsp_address",))
    assert site.kind == "replace"                          # any first instruction: nothing is displaced
    source = hooks.render_trampolines([site])
    assert "jpr_tramp_dsp_address:" in source and "    b     jpr_dsp_address" in source
    assert f"0x{site.displaced:08x}" not in source         # the original never runs


def _select_code(flag=0x69F4E0, merge_first=0xE2802008):
    code = bytearray(0x200000)
    for k, word in enumerate(hooks.HID_SELECT_CALL_WORDS):
        struct.pack_into("<I", code, hooks.HID_SELECT_CALL - hooks.CODE_BASE + 4 * k, word)
    struct.pack_into("<I", code, hooks.HID_SELECT_CALL - hooks.CODE_BASE + 8 + 0x24, flag)
    words = (merge_first,) + hooks.HID_SELECT_MERGE_WORDS[1:]
    for k, word in enumerate(words):
        struct.pack_into("<I", code, hooks.HID_SELECT_MERGE - hooks.CODE_BASE + 4 * k, word)
    return bytes(code)


def test_the_flag_that_keeps_select_apart_from_start():
    # the SDK's pad reader folds SELECT into START unless this byte is set (docs/stage13-battle-findings.md)
    assert hooks.hid_select_flag(_select_code()) == 0x69F4E0


def test_the_select_flag_refuses_other_code():
    with pytest.raises(hooks.HookError, match="another game version"):
        hooks.hid_select_flag(_select_code(merge_first=0xE92D4010))
