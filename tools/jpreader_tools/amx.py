"""Read Game Freak's field scripts: Pawn 3.x AMX files in compact encoding.

The opcode numbering is Pawn 3.3's (with INC_I and DEC_I), checked against every
script in the game: jumps and calls are relative to the instruction and always
land on an instruction, and every call lands on a PROC.
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field

MAGIC = 0x0A0AF1E0

NAMES = """NONE LOAD_PRI LOAD_ALT LOAD_S_PRI LOAD_S_ALT LREF_PRI LREF_ALT LREF_S_PRI LREF_S_ALT
LOAD_I LODB_I CONST_PRI CONST_ALT ADDR_PRI ADDR_ALT STOR_PRI STOR_ALT STOR_S_PRI STOR_S_ALT SREF_PRI SREF_ALT
SREF_S_PRI SREF_S_ALT STOR_I STRB_I LIDX LIDX_B IDXADDR IDXADDR_B ALIGN_PRI ALIGN_ALT LCTRL SCTRL MOVE_PRI MOVE_ALT
XCHG PUSH_PRI PUSH_ALT PUSH_R PUSH_C PUSH PUSH_S POP_PRI POP_ALT STACK HEAP PROC RET RETN CALL CALL_PRI JUMP JREL
JZER JNZ JEQ JNEQ JLESS JLEQ JGRTR JGEQ JSLESS JSLEQ JSGRTR JSGEQ SHL SHR SSHR SHL_C_PRI SHL_C_ALT SHR_C_PRI
SHR_C_ALT SMUL SDIV SDIV_ALT UMUL UDIV UDIV_ALT ADD SUB SUB_ALT AND OR XOR NOT NEG INVERT ADD_C SMUL_C ZERO_PRI
ZERO_ALT ZERO ZERO_S SIGN_PRI SIGN_ALT EQ NEQ LESS LEQ GRTR GEQ SLESS SLEQ SGRTR SGEQ EQ_C_PRI EQ_C_ALT INC_PRI
INC_ALT INC INC_S INC_I DEC_PRI DEC_ALT DEC DEC_S DEC_I MOVS CMPS FILL HALT BOUNDS SYSREQ_PRI SYSREQ_C FILE LINE
SYMBOL SRANGE JUMP_PRI SWITCH CASETBL SWAP_PRI SWAP_ALT PUSH_ADR NOP SYSREQ_N SYMTAG BREAK
PUSH2_C PUSH2 PUSH2_S PUSH2_ADR PUSH3_C PUSH3 PUSH3_S PUSH3_ADR PUSH4_C PUSH4 PUSH4_S PUSH4_ADR
PUSH5_C PUSH5 PUSH5_S PUSH5_ADR LOAD_BOTH LOAD_S_BOTH CONST CONST_S SYSREQ_D SYSREQ_ND OP160 OP161
LOAD_P_PRI LOAD_P_ALT LOAD_P_S_PRI LOAD_P_S_ALT LREF_P_PRI LREF_P_ALT LREF_P_S_PRI LREF_P_S_ALT
LODB_P_I CONST_P_PRI CONST_P_ALT ADDR_P_PRI ADDR_P_ALT STOR_P_PRI STOR_P_ALT STOR_P_S_PRI STOR_P_S_ALT
SREF_P_PRI SREF_P_ALT SREF_P_S_PRI SREF_P_S_ALT STRB_P_I LIDX_P_B IDXADDR_P_B ALIGN_P_PRI ALIGN_P_ALT
PUSH_P_C PUSH_P PUSH_P_S STACK_P HEAP_P SHL_P_C_PRI SHL_P_C_ALT SHR_P_C_PRI SHR_P_C_ALT ADD_P_C SMUL_P_C
ZERO_P ZERO_P_S EQ_P_C_PRI EQ_P_C_ALT INC_P INC_P_S DEC_P DEC_P_S MOVS_P CMPS_P FILL_P HALT_P BOUNDS_P PUSH_P_ADR""".split()
OP = {name: i for i, name in enumerate(NAMES)}
FIRST_PACKED = OP["LOAD_P_PRI"]   # from here on the argument sits in the opcode cell's top 16 bits

_NO_ARGS = set("""NONE LOAD_I MOVE_PRI MOVE_ALT XCHG PUSH_PRI PUSH_ALT POP_PRI POP_ALT PROC RET RETN CALL_PRI SHL SHR
SSHR SMUL SDIV SDIV_ALT UMUL UDIV UDIV_ALT ADD SUB SUB_ALT AND OR XOR NOT NEG INVERT ZERO_PRI ZERO_ALT SIGN_PRI
SIGN_ALT EQ NEQ LESS LEQ GRTR GEQ SLESS SLEQ SGRTR SGEQ INC_PRI INC_ALT INC_I DEC_PRI DEC_ALT DEC_I STOR_I LIDX
IDXADDR SYSREQ_PRI JUMP_PRI SWAP_PRI SWAP_ALT NOP BREAK""".split())
_TWO_ARGS = {"SYSREQ_N", "LINE", "SRANGE", "LOAD_BOTH", "LOAD_S_BOTH", "CONST", "CONST_S", "SYSREQ_ND"}
ARG_COUNT = [0 if name in _NO_ARGS or i >= FIRST_PACKED else 2 if name in _TWO_ARGS else 1
             for i, name in enumerate(NAMES)]
for _k in (2, 3, 4, 5):
    for _suffix in ("_C", "", "_S", "_ADR"):
        ARG_COUNT[OP[f"PUSH{_k}{_suffix}"]] = _k
JUMPS = frozenset(OP[n] for n in "JUMP JZER JNZ JEQ JNEQ JLESS JLEQ JGRTR JGEQ JSLESS JSLEQ JSGRTR JSGEQ".split())

_CONST_PUSH = {OP["PUSH_P_C"], OP["PUSH_C"]}
_FRAME_PUSH = {OP["PUSH_P_S"], OP["PUSH_S"]}
_FIRST_PARAM = 12                 # frame offset of a procedure's first parameter
_MULTI_CONST = {OP[f"PUSH{k}_C"]: k for k in (2, 3, 4, 5)}
_MULTI_OTHER = {OP[f"PUSH{k}{s}"]: k for k in (2, 3, 4, 5) for s in ("", "_S", "_ADR")}


class AmxError(Exception):
    pass


@dataclass(frozen=True)
class Instruction:
    address: int
    op: int
    args: tuple[int, ...]

    @property
    def name(self) -> str:
        return NAMES[self.op]


@dataclass
class Script:
    code: list[int]
    data: list[int]
    natives: list[int]            # name hashes, by native index

    def strings(self, min_len: int = 4) -> list[str]:
        """Printable runs in the data section (unpacked Pawn strings, mostly debug text)."""
        found, run = [], []
        for cell in self.data:
            if 32 <= cell < 127:
                run.append(chr(cell))
                continue
            if len(run) >= min_len:
                found.append("".join(run))
            run = []
        return found


@dataclass
class Call:
    caller: int                   # address of the calling procedure
    address: int
    native: int | None            # name hash for a native call
    target: int | None            # procedure address for a script call
    args: list[int | None]        # constant arguments, first argument first; None when not a constant
    params: list[int | None] = field(default_factory=list)   # the caller's own parameter passed on, by index


@dataclass
class Procedure:
    address: int
    instructions: list[Instruction] = field(default_factory=list)
    signature: str = ""           # same library procedure, same signature, in every script


def _signed(value: int, bits: int = 32) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def expand(packed: bytes) -> list[int]:
    """Pawn's compact encoding: 7-bit groups, high bit set while more follow, bit 6 of the first is the sign."""
    cells = []
    i, n = 0, len(packed)
    while i < n:
        value = -1 if packed[i] & 0x40 else 0
        while True:
            b = packed[i]
            i += 1
            value = (value << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        cells.append(value & 0xFFFFFFFF)
    return cells


def parse(blob: bytes) -> Script:
    (size, magic, _fver, _aver, flags, defsize, cod, dat, hea, _stp, _cip, publics, natives, libraries, *_rest
     ) = struct.unpack_from("<IHBBHHIIIIIIIIIII", blob, 0)
    if (magic | _fver << 16 | _aver << 24) != MAGIC:
        raise AmxError("not an AMX script")
    if not flags & 0x04:
        raise AmxError("expected compact encoding")
    cells = expand(blob[cod:size])
    if len(cells) != (hea - cod) // 4:
        raise AmxError("compact data does not expand to the code and data sections")
    split = (dat - cod) // 4
    hashes = [struct.unpack_from("<I", blob, o + 4)[0] for o in range(natives, libraries, defsize)]
    return Script(cells[:split], cells[split:], hashes)


def disassemble(code: list[int]) -> list[Instruction]:
    out = []
    i = 0
    while i < len(code):
        cell = code[i]
        op = cell & 0xFFFF
        if op >= len(NAMES):
            raise AmxError(f"unknown opcode {op} at {i * 4:#x}")
        if op == OP["CASETBL"]:
            count = code[i + 1]
            out.append(Instruction(i * 4, op, tuple(_signed(a) for a in code[i + 1:i + 3 + 2 * count])))
            i += 3 + 2 * count
        elif op >= FIRST_PACKED:
            out.append(Instruction(i * 4, op, (_signed(cell >> 16, 16),)))
            i += 1
        else:
            if cell >> 16:
                raise AmxError(f"packed argument on {NAMES[op]} at {i * 4:#x}")
            k = ARG_COUNT[op]
            out.append(Instruction(i * 4, op, tuple(_signed(a) for a in code[i + 1:i + 1 + k])))
            i += 1 + k
    return out


def procedures(script: Script) -> dict[int, Procedure]:
    procs: dict[int, Procedure] = {}
    current = None
    for ins in disassemble(script.code):
        if ins.op == OP["PROC"]:
            current = procs[ins.address] = Procedure(ins.address)
        if current is not None:
            current.instructions.append(ins)
    for proc in procs.values():
        tokens = [ins.name + (f":{script.natives[ins.args[0]]:08x}" if ins.op == OP["SYSREQ_N"] else "")
                  for ins in proc.instructions]
        proc.signature = hashlib.sha1(" ".join(tokens).encode()).hexdigest()[:10]
    return procs


def calls(script: Script, procs: dict[int, Procedure]) -> list[Call]:
    """Every native and procedure call, with the constants pushed for it.

    Pawn pushes arguments last to first, and for a procedure call pushes the
    argument byte count last. Only pushes inside the same straight run count.
    """
    def param(offset: int) -> int | None:
        return (offset - _FIRST_PARAM) // 4 if offset >= _FIRST_PARAM else None

    found = []
    for proc in procs.values():
        pushes: list[tuple[int | None, int | None]] = []    # (constant, parameter index)
        for ins in proc.instructions:
            if ins.op in _CONST_PUSH:
                pushes.append((ins.args[0], None))
            elif ins.op in _FRAME_PUSH:
                pushes.append((None, param(ins.args[0])))
            elif ins.op in _MULTI_CONST:
                pushes.extend((a, None) for a in ins.args)
            elif ins.op in _MULTI_OTHER:
                frame = ins.name.endswith("_S")
                pushes.extend((None, param(a) if frame else None) for a in ins.args)
            elif ins.name.startswith("PUSH"):
                pushes.append((None, None))
            if ins.op == OP["SYSREQ_N"]:
                k = ins.args[1] // 4
                got = ([(None, None)] * k + pushes)[-k:] if k else []
                found.append(Call(proc.address, ins.address, script.natives[ins.args[0]], None,
                                  [c for c, _ in got[::-1]], [p for _, p in got[::-1]]))
                pushes = []
            elif ins.op == OP["CALL"]:
                nbytes = pushes[-1][0] if pushes else None
                k = nbytes // 4 if isinstance(nbytes, int) and 0 <= nbytes < 256 else 0
                got = ([(None, None)] * k + pushes[:-1])[-k:] if k else []
                found.append(Call(proc.address, ins.address, None, ins.address + ins.args[0],
                                  [c for c, _ in got[::-1]], [p for _, p in got[::-1]]))
                pushes = []
            elif ins.op in (OP["JUMP"], OP["RETN"]):
                pushes = []
    return found
