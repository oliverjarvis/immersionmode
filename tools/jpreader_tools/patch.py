"""Assemble code.ips: entry and hook branches, the stub with its veneers, and the staged payload."""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .hooks import arm_b
from .ips import build_ips
from .layout import CODE_BASE, CODE_FILE_SIZE, STAGE_BASE, STAGE_MAX, STUB_BASE, STUB_MAX

LDR_PC = 0xE51FF004        # ldr pc, [pc, #-4]: jump to the word that follows
VENEER_SIZE = 8


class PatchError(Exception):
    pass


@dataclass(frozen=True)
class Edit:
    """One game instruction replaced in place; the build refuses a ROM where it is not the original."""
    name: str
    addr: int
    original: int
    replacement: int


GAME_NDM_ENTER_EXCLUSIVE_STATE = 0x0017A198
# While the game holds ndm in local wireless mode, a 3DS refuses every Internet connection the game's
# process asks for, Grammar's included. The game's local wireless start-up (0x260E48) asks ndm for the
# radio here; a failure result instead takes the game's own path for ndm refusing it, so local wireless
# never starts and Festival Plaza works as when nobody is nearby.
NO_LOCAL_WIRELESS = Edit("no local wireless", 0x00260EA0, 0xEBFC64BC,   # bl nn::ndm::EnterExclusiveState
                         0xE3E00000)                                     # mvn r0, #0
EDITS = (NO_LOCAL_WIRELESS,)


def build_patch(code: bytes, sites, trampolines: dict[str, int], stub: bytes, payload: bytes,
                edits=()) -> bytes:
    if len(code) != CODE_FILE_SIZE:
        raise PatchError(f"executable has size {len(code):#x}, expected {CODE_FILE_SIZE:#x}")
    stub = stub + bytes(-len(stub) % 4)
    if len(stub) + VENEER_SIZE * len(sites) > STUB_MAX:
        raise PatchError(f"stub ({len(stub)} bytes) and {len(sites)} veneers do not fit in {STUB_MAX} bytes")
    if len(payload) > STAGE_MAX:
        raise PatchError(f"payload image is {len(payload)} bytes; the stage holds {STAGE_MAX}")
    stub_off = STUB_BASE - CODE_BASE
    if any(code[stub_off:stub_off + STUB_MAX]):
        raise PatchError("the space reserved for the stub is not empty")

    # The payload runs far above the text segment, out of reach of a branch, so each hook
    # branches to a veneer next to the stub that loads the trampoline address into pc.
    veneers = bytearray()
    records = [(0, struct.pack("<I", arm_b(CODE_BASE, STUB_BASE)))]
    for site in sites:
        if site.name not in trampolines:
            raise PatchError(f"no trampoline address for hook {site.name}")
        veneer = STUB_BASE + len(stub) + len(veneers)
        veneers += struct.pack("<II", LDR_PC, trampolines[site.name])
        records.append((site.addr - CODE_BASE, struct.pack("<I", arm_b(site.addr, veneer))))
    taken = [(0, 4), (stub_off, stub_off + STUB_MAX)] + [(s.addr - CODE_BASE, s.addr - CODE_BASE + 4) for s in sites]
    for edit in edits:
        at = edit.addr - CODE_BASE
        if any(start < at + 4 and at < end for start, end in taken):
            raise PatchError(f"{edit.name}: {edit.addr:#x} overlaps the entry, the stub or a hook")
        found = struct.unpack_from("<I", code, at)[0]
        if found != edit.original:
            raise PatchError(f"{edit.name}: expected {edit.original:08x} at {edit.addr:#x}, found {found:08x}")
        records.append((at, struct.pack("<I", edit.replacement)))
    records.append((stub_off, stub + bytes(veneers)))
    records.append((STAGE_BASE - CODE_BASE, payload))
    return build_ips(records)
