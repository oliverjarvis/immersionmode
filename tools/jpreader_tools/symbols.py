"""Find function addresses: named exports in static.crs and C++ vtables in the code."""
from __future__ import annotations

import struct

from .layout import CODE_BASE, TEXT_SIZE

BX_LR = 0xE12FFF1E


def parse_crs_exports(blob: bytes) -> dict[str, int]:
    if blob[0x80:0x84] != b"CRO0":
        raise ValueError("static.crs: missing CRO0 magic")
    seg_off, seg_count, exp_off, exp_count = struct.unpack_from("<4I", blob, 0xC8)
    segments = [struct.unpack_from("<III", blob, seg_off + i * 12) for i in range(seg_count)]
    exports = {}
    for i in range(exp_count):
        name_off, location = struct.unpack_from("<II", blob, exp_off + i * 8)
        name = blob[name_off:blob.index(b"\0", name_off)].decode("ascii")
        exports[name] = segments[location & 0xF][0] + (location >> 4)
    return exports


def _aligned_refs(code: bytes, value: int) -> list[int]:
    needle = struct.pack("<I", value)
    refs = []
    pos = code.find(needle)
    while pos != -1:
        if pos % 4 == 0:
            refs.append(pos)
        pos = code.find(needle, pos + 1)
    return refs


POP_PC_MASK = 0xFFFF8000
POP_PC_BITS = 0xE8BD8000      # pop {..., pc}


def _vtable(code: bytes, class_name: str) -> tuple[int, list[int]]:
    """(address of slot 0, virtual functions) of the class's primary vtable."""
    needle = class_name.encode() + b"\0"
    pos = code.find(needle)
    while pos > 0 and code[pos - 1] != 0:
        pos = code.find(needle, pos + 1)
    if pos == -1:
        raise KeyError(class_name)
    for name_ref in _aligned_refs(code, CODE_BASE + pos):
        typeinfo = CODE_BASE + name_ref - 4
        for ref in _aligned_refs(code, typeinfo):
            if ref < 4 or struct.unpack_from("<I", code, ref - 4)[0] != 0:
                continue
            functions = []
            cursor = ref + 4
            while cursor + 4 <= len(code):
                value = struct.unpack_from("<I", code, cursor)[0]
                if not CODE_BASE <= value < CODE_BASE + TEXT_SIZE:
                    break
                functions.append(value)
                cursor += 4
            if functions:
                return CODE_BASE + ref + 4, functions
    raise KeyError(f"no vtable found for {class_name}")


def vtable_functions(code: bytes, class_name: str) -> list[int]:
    """Virtual functions of the class's primary vtable (the one with offset-to-top 0)."""
    return _vtable(code, class_name)[1]


def vtable_address(code: bytes, class_name: str) -> int:
    """Address of slot 0 of the class's primary vtable: the word an object of the class starts with."""
    return _vtable(code, class_name)[0]


def is_exit(word: int) -> bool:
    return word == BX_LR or word & POP_PC_MASK == POP_PC_BITS


def find_exit(code: bytes, start: int, limit: int = 0x400) -> int:
    """First return instruction (bx lr, or pop that loads pc) at or after `start`."""
    for addr in range(start, start + limit, 4):
        offset = addr - CODE_BASE
        if offset + 4 > len(code):
            break
        if is_exit(struct.unpack_from("<I", code, offset)[0]):
            return addr
    raise ValueError(f"no return instruction within {limit:#x} bytes of {start:#x}")


def find_unique(code: bytes, signature: bytes) -> int:
    """Address of the only word-aligned occurrence of `signature` in the text segment."""
    hits = []
    pos = code.find(signature)
    while pos != -1 and pos < TEXT_SIZE:
        if pos % 4 == 0:
            hits.append(pos)
        pos = code.find(signature, pos + 1)
    if len(hits) != 1:
        raise ValueError(f"signature matched {len(hits)} times; expected exactly 1")
    return CODE_BASE + hits[0]
