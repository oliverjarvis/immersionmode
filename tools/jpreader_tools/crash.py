"""Read a Luma3DS ARM11 crash dump (/luma/dumps/arm11/crash_dump_*.dmp) and name the code it stopped in.

Format from Luma3DS (arm9/source/types.h ExceptionDumpHeader, arm9/source/exceptions.c): a 40-byte
header, the registers, a code dump, a stack dump, and for ARM11 the process name and title ID.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .layout import CODE_BASE, IMAGE_END, PAYLOAD_BASE, PAYLOAD_END, STAGE_BASE, STUB_BASE, STUB_MAX, TITLE_ID

MAGIC = (0xDEADC0DE, 0xDEADCAFE)
HEADER = "<2I2H2H6I"
HEADER_SIZE = struct.calcsize(HEADER)
REGISTER_NAMES = ("R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12",
                  "SP", "LR", "PC", "CPSR")
KINDS = ("FIQ", "undefined instruction", "prefetch abort", "data abort")


class CrashError(Exception):
    pass


@dataclass(frozen=True)
class Dump:
    processor: int
    core: int
    kind: str
    registers: dict[str, int]
    process: str
    title_id: int


def parse(blob: bytes) -> Dump:
    if len(blob) < HEADER_SIZE or struct.unpack_from("<2I", blob) != MAGIC:
        raise CrashError("not a Luma3DS crash dump")
    (_m0, _m1, _minor, _major, processor, core, kind, total, reg_size, code_size, stack_size,
     extra_size) = struct.unpack_from(HEADER, blob)
    if processor != 11:
        raise CrashError("an ARM9 crash: not the game (jpreader runs on the ARM11)")
    if len(blob) < total or len(blob) < HEADER_SIZE + reg_size + code_size + stack_size + extra_size:
        raise CrashError(f"the dump is short: {len(blob)} bytes, the header says {total}")
    count = min(reg_size // 4, len(REGISTER_NAMES))
    values = struct.unpack_from(f"<{count}I", blob, HEADER_SIZE)
    registers = dict(zip(REGISTER_NAMES, values))
    process, title_id = "", 0
    extra = HEADER_SIZE + reg_size + code_size + stack_size
    if extra_size >= 16:
        process = blob[extra:extra + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        title_id = struct.unpack_from("<Q", blob, extra + 8)[0]
    return Dump(processor, core, KINDS[kind] if kind < len(KINDS) else f"type {kind}", registers, process, title_id)


def where(address: int, symbols: dict[str, int]) -> str:
    """The payload function an address is in, or which part of memory it is."""
    if PAYLOAD_BASE <= address < PAYLOAD_END:
        below = [(value, name) for name, value in symbols.items() if PAYLOAD_BASE <= value <= address]
        if below:
            value, name = max(below)
            return f"jpreader {name}+0x{address - value:x}"
        return f"jpreader 0x{address:08x}"
    if STUB_BASE <= address < STUB_BASE + STUB_MAX:
        return f"jpreader stub/veneer 0x{address:08x}"     # the loader and the hooks' jumps
    if STAGE_BASE <= address < IMAGE_END:
        return f"game data 0x{address:08x}"                # the old bss pages the payload travelled in
    if CODE_BASE <= address < STUB_BASE:
        return f"game code 0x{address:08x}"
    if CODE_BASE <= address < IMAGE_END:
        return f"game data 0x{address:08x}"
    return f"0x{address:08x}"


def describe(dump: Dump, symbols: dict[str, int]) -> list[str]:
    lines = [f"{dump.kind} on ARM11 core {dump.core}"]
    process = f"process {dump.process or '?'} ({dump.title_id:016X})"
    if dump.title_id != TITLE_ID:
        process += " (not Ultra Moon: unrelated to jpreader)"
    lines.append(process)
    for name in ("PC", "LR"):
        if name in dump.registers:
            lines.append(f"{name}   {dump.registers[name]:08x}  {where(dump.registers[name], symbols)}")
    for name in ("SP", "CPSR"):
        if name in dump.registers:
            lines.append(f"{name:4s} {dump.registers[name]:08x}")
    return lines
