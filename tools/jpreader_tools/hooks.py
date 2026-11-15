"""Decide where the game is hooked and generate the trampoline source."""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .layout import CODE_BASE
from .symbols import find_exit, find_unique, is_exit, vtable_address, vtable_functions

PUSH_MASK = 0xFFFF0000
PUSH_BITS = 0xE92D0000        # push {...}

SYMBOLS = {
    "msgdata_ctor_ids": "_ZN4gfl23str7MsgDataC1EjjPNS_4heap11CtrHeapBaseENS1_8LoadTypeE",
    "msgdata_ctor_mem": "_ZN4gfl23str7MsgDataC1EPKvPNS_4heap11CtrHeapBaseE",
    "getstring": "_ZN4gfl23str7MsgData9GetStringEjRNS0_6StrBufE",
    "expand": "_ZN5print7WordSet6ExpandEPN4gfl23str6StrBufEPKS3_",
    "talk_set_buf": "_ZN3App4Tool10TalkWindow10SetMessageEPKN4gfl23str6StrBufE",
    "talk_set_id": "_ZN3App4Tool10TalkWindow10SetMessageEjPN4gfl23str6StrBufE",
    "talk_call": "_ZN3App4Tool10TalkWindow10CallMsgWinEv",
    "talk_end": "_ZN3App4Tool10TalkWindow3EndEv",
    # Every talk window is destroyed through its base class; a window may go without End().
    "view_dtor": "_ZN3app2ui6UIViewD2Ev",
    # Choice menus (stage 12; docs/stage12-choices-findings.md): the field's and the apps'
    "listmenu_add": "_ZN3App4Tool8ListMenu7AddItemEjPN4gfl23str6StrBufE",
    "listmenu_start": "_ZN3App4Tool8ListMenu5StartEv",
    "menuwin_add_buf": "_ZN3app4tool10MenuWindow7AddItemERKN4gfl23str6StrBufEbbj",
    "menuwin_start": "_ZN3app4tool10MenuWindow9StartMenuEjNS1_7PutTypeEff",
    "menuwin_start_disp": "_ZN3app4tool10MenuWindow16StartMenuDisplayEjNS1_7PutTypeEii",
    "menuwin_end": "_ZN3app4tool10MenuWindow7EndMenuEb",
}
TEXTBOX_CLASS = "N2nw3lyt7TextBoxE"
PARTS_CLASS = "N2nw3lyt5PartsE"
MSGWIN_SET_SLOT = 6           # print::MessageWindow's "set the text" (G2DUtil::SetMessage calls it too)
# gfl2::lyt's draw of one layout on one display: (LytWk *wk, u32 display, arg) (stage 14). It is not exported, so its
# first words are checked, as snd.py checks its fixed addresses.
LYT_DRAW = 0x359400
LYT_DRAW_WORDS = (0xE92D4010, 0xE1A04000, 0xE5900004, 0xEB0009D1)
# nn::hid's pad reader ends by folding SELECT into START (0x282354) unless a byte is set
# (ldr r0, =flag; ldrb r0, [r0]; cmp r0, #0; moveq r0, r4; bleq 0x282354): the peek sets it, so SELECT stays its own
# button, which the game never reads (stage 13)
HID_SELECT_CALL = 0x179FB8
HID_SELECT_CALL_WORDS = (0xE59F0024, 0xE5D00000, 0xE3500000, 0x01A00004, 0x0B0420E1)
HID_SELECT_MERGE = 0x282354
HID_SELECT_MERGE_WORDS = (0xE2802008, 0xE2801004, 0xE1A00000, 0xE52D4004, 0xE5913000, 0xE3A0C00C)
SCREEN_EXPORTS = {"JPR_WAZA_GET_TYPE": "_ZN3pml8wazadata7GetTypeE6WazaNo"}
MSGDATA_CLASS = "N4gfl23str7MsgDataE"
MSGWIN_CLASS = "N5print13MessageWindowE"
BUTTON_CLASS = "N4gfl22ui10CTR_ButtonE"
CROSS_CLASS = "N4gfl22ui12CTR_CrossKeyE"
STICK_CLASS = "N4gfl22ui9CTR_StickE"
TOUCH_CLASS = "N4gfl22ui14CTR_TouchPanelE"
POLL_SLOT = 3                 # fourth virtual function of each input device: fills in this poll's state
# The engine's display swap: (display, left buffer, right buffer, column height, [sp] GL format).
# It is not exported, so it is found by its first six instructions.
PRESENT_SIGNATURE = bytes.fromhex("f0432de9" "0cd04de2" "010b50e3" "0170a0e1" "28109de5" "08019fe5")
KINDS = ("pre", "prepost", "pre_regs", "exit", "replace")   # replace: the handler is the function now


class HookError(Exception):
    pass


@dataclass(frozen=True)
class HookSite:
    name: str
    kind: str                 # "pre", "prepost" or "tail"
    addr: int
    displaced: int            # the instruction the branch replaces
    handlers: tuple[str, ...]


def _word(code: bytes, addr: int) -> int:
    return struct.unpack_from("<I", code, addr - CODE_BASE)[0]


def arm_b(site: int, target: int) -> int:
    """Encode an unconditional ARM branch placed at `site`."""
    delta = target - (site + 8)
    if delta % 4 or not -0x2000000 <= delta < 0x2000000:
        raise HookError(f"branch from {site:#x} to {target:#x} is out of range")
    return 0xEA000000 | ((delta >> 2) & 0xFFFFFF)


def make_site(name: str, kind: str, addr: int, code: bytes, handlers) -> HookSite:
    if kind not in KINDS:
        raise HookError(f"{name}: unknown hook kind {kind!r}")
    word = _word(code, addr)
    if kind == "replace":
        pass                       # the branch replaces the function: its first instruction never runs
    elif kind == "exit":
        if not is_exit(word):
            raise HookError(f"{name}: instruction {word:08x} at {addr:#x} is not a return; refusing to hook")
    elif word & PUSH_MASK != PUSH_BITS:
        raise HookError(f"{name}: instruction {word:08x} at {addr:#x} is not a push; refusing to hook")
    return HookSite(name, kind, addr, word, tuple(handlers))


def entry_redirect(code: bytes) -> tuple[int, int]:
    """The game's entry instruction is `bl <start>`; return (<start>, return address)."""
    word = _word(code, CODE_BASE)
    if word >> 24 != 0xEB:
        raise HookError(f"entry instruction {word:08x} is not a bl")
    offset = word & 0xFFFFFF
    if offset & 0x800000:
        offset -= 0x1000000
    return CODE_BASE + 8 + offset * 4, CODE_BASE + 4


def plan_hooks(code: bytes, syms: dict[str, int], debug: bool) -> list[HookSite]:
    def address(key: str) -> int:
        try:
            return syms[SYMBOLS[key]]
        except KeyError:
            raise HookError(f"static.crs has no symbol for {key}") from None

    destructors = vtable_functions(code, MSGDATA_CLASS)[:2]
    sites = [
        make_site("msgdata_ctor_ids", "pre", address("msgdata_ctor_ids"), code, ("jpr_h_msgdata_ctor_ids",)),
        make_site("msgdata_ctor_mem", "pre", address("msgdata_ctor_mem"), code, ("jpr_h_msgdata_ctor_mem",)),
        make_site("msgdata_dtor_a", "pre", destructors[0], code, ("jpr_h_msgdata_dtor",)),
        make_site("msgdata_dtor_b", "pre", destructors[1], code, ("jpr_h_msgdata_dtor",)),
        make_site("getstring", "pre", address("getstring"), code, ("jpr_h_getstring",)),
        make_site("expand", "pre", address("expand"), code, ("jpr_h_expand",)),
        make_site("talk_set_buf", "prepost", address("talk_set_buf"), code,
                  ("jpr_h_talk_set_buf", "jpr_h_talk_set_done")),
        make_site("talk_set_id", "prepost", address("talk_set_id"), code,
                  ("jpr_h_talk_set_id", "jpr_h_talk_set_done")),
        make_site("talk_call", "pre", address("talk_call"), code, ("jpr_h_talk_call",)),
        make_site("talk_end", "pre", address("talk_end"), code, ("jpr_h_talk_end",)),
        make_site("view_dtor", "pre", address("view_dtor"), code, ("jpr_h_view_dtor",)),
        make_site("listmenu_add", "pre", address("listmenu_add"), code, ("jpr_h_listmenu_add",)),
        make_site("listmenu_start", "pre", address("listmenu_start"), code, ("jpr_h_listmenu_start",)),
        make_site("menuwin_add_buf", "pre", address("menuwin_add_buf"), code, ("jpr_h_menuwin_add_buf",)),
        make_site("menuwin_start", "pre", address("menuwin_start"), code, ("jpr_h_menuwin_start",)),
        make_site("menuwin_start_disp", "pre", address("menuwin_start_disp"), code, ("jpr_h_menuwin_start",)),
        make_site("menuwin_end", "pre", address("menuwin_end"), code, ("jpr_h_menuwin_end",)),
    ]
    sites += [
        # nn::snd hands the DSP only buffers in device memory; the payload's voice buffers are its own
        # linear memory, so the check is replaced by one that also accepts them (spec: voice 4)
        make_site("dsp_address", "replace", 0x172BE4, code, ("jpr_dsp_address",)),
        # The mixer (stage 11): the game's player-volume setter and getter, so its fades keep its own values and
        # ours multiply in (docs/stage11-sound-findings.md); snd.py checks their words
        make_site("player_vol_set", "replace", 0x3BA324, code, ("jpr_h_player_vol_set_asm",)),
        make_site("player_vol_get", "replace", 0x4B7A90, code, ("jpr_h_player_vol_get_asm",)),
        make_site("present", "pre_regs", find_unique(code, PRESENT_SIGNATURE), code, ("jpr_h_present",)),
        make_site("touch", "exit", _poll_exit(code, TOUCH_CLASS), code, ("jpr_h_touch",)),
        # The input polls (stage 13): SELECT, which the game never reads, opens and closes the peek, and the game
        # sees no buttons while it is open; debug builds also press buttons for the test driver here
        make_site("pad", "exit", _poll_exit(code, BUTTON_CLASS), code, ("jpr_h_pad_exit",)),
        make_site("cross", "exit", _poll_exit(code, CROSS_CLASS), code, ("jpr_h_cross_exit",)),
        make_site("stick", "exit", _poll_exit(code, STICK_CLASS), code, ("jpr_h_stick_exit",)),
        # What the screens show (stages 13, 14): message windows' text, and the layouts drawn, whose text the peek reads
        make_site("msgwin_set", "pre", vtable_functions(code, MSGWIN_CLASS)[MSGWIN_SET_SLOT], code,
                  ("jpr_h_msgwin_set",)),
        make_site("lyt_draw", "pre", _checked(code, LYT_DRAW, LYT_DRAW_WORDS), code, ("jpr_h_lyt_draw",)),
    ]
    return sites


def _checked(code: bytes, addr: int, words) -> int:
    for k, want in enumerate(words):
        if _word(code, addr + 4 * k) != want:
            raise HookError(f"word at {addr + 4 * k:#x} is not {want:08x}; another game version?")
    return addr


def screen_constants(syms: dict[str, int]) -> dict[str, int]:
    """Exported functions the peek calls (stage 13)."""
    missing = [name for name in SCREEN_EXPORTS.values() if name not in syms]
    if missing:
        raise HookError(f"static.crs has no {missing[0]}")
    return {key: syms[name] for key, name in SCREEN_EXPORTS.items()}


def _poll_exit(code: bytes, class_name: str) -> int:
    return find_exit(code, vtable_functions(code, class_name)[POLL_SLOT])


def hid_select_flag(code: bytes) -> int:
    """The byte that keeps nn::hid from folding SELECT into START."""
    _checked(code, HID_SELECT_CALL, HID_SELECT_CALL_WORDS)
    _checked(code, HID_SELECT_MERGE, HID_SELECT_MERGE_WORDS)
    return _word(code, HID_SELECT_CALL + 8 + 0x24)


def game_constants(code: bytes) -> dict[str, int]:
    """Values the payload needs that are read from the executable at build time."""
    return {"JPR_MSGWIN_VTABLE": vtable_address(code, MSGWIN_CLASS),
            "JPR_BUTTON_VTABLE": vtable_address(code, BUTTON_CLASS),
            "JPR_HID_SELECT_FLAG": hid_select_flag(code),
            "JPR_TEXTBOX_VTABLE": vtable_address(code, TEXTBOX_CLASS),
            "JPR_PARTS_VTABLE": vtable_address(code, PARTS_CLASS)}


def render_trampolines(sites) -> str:
    out = [
        "@ generated by jpreader_tools.hooks; do not edit",
        "    .arm",
        '    .section .text.tramp, "ax", %progbits',
        "",
    ]
    for site in sites:
        label = f"jpr_tramp_{site.name}"
        out += [
            f"    .global {label}",
            f"    .type {label}, %function",
            f"{label}:",
        ]
        if site.kind == "replace":
            out.append(f"    b     {site.handlers[0]}")
        elif site.kind in ("pre_regs", "exit"):
            # The handler gets a pointer to the saved registers; the stack arguments follow them.
            out += [
                "    push  {r0-r12, lr}",
                "    mov   r0, sp",
                f"    bl    {site.handlers[0]}",
                "    pop   {r0-r12, lr}",
                f"    .word 0x{site.displaced:08x}",
            ]
            if site.kind == "pre_regs":
                out.append(f"    ldr   pc, =0x{site.addr + 4:08x}")
        else:
            out += [
                "    push  {r0-r3, r12, lr}",
                f"    bl    {site.handlers[0]}",
            ]
            if site.kind == "pre":
                out += [
                    "    pop   {r0-r3, r12, lr}",
                    f"    .word 0x{site.displaced:08x}",
                    f"    ldr   pc, =0x{site.addr + 4:08x}",
                ]
            else:
                out += [
                    "    ldr   r0, [sp, #20]",
                    "    bl    jpr_ret_push",
                    "    pop   {r0-r3, r12, lr}",
                    "    adr   lr, 1f",
                    f"    .word 0x{site.displaced:08x}",
                    f"    ldr   pc, =0x{site.addr + 4:08x}",
                    "1:  push  {r0, r1}",
                    f"    bl    {site.handlers[1]}",
                    "    bl    jpr_ret_pop",
                    "    mov   lr, r0",
                    "    pop   {r0, r1}",
                    "    bx    lr",
                ]
        out += ["    .ltorg", ""]
    return "\n".join(out)
