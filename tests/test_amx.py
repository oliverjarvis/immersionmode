import struct

import pytest

from jpreader_tools import amx


def compact(cells: list[int]) -> bytes:
    """Pawn compact encoding, the inverse of amx.expand."""
    out = bytearray()
    for cell in cells:
        value = cell - (1 << 32) if cell & 0x80000000 else cell
        groups = [value & 0x7F]
        value >>= 7
        while not ((value == 0 and not groups[-1] & 0x40) or (value == -1 and groups[-1] & 0x40)):
            groups.append(value & 0x7F)
            value >>= 7
        groups.reverse()
        out += bytes(g | 0x80 for g in groups[:-1]) + bytes([groups[-1]])
    return bytes(out)


def packed(name: str, arg: int) -> int:
    return amx.OP[name] | (arg & 0xFFFF) << 16


def make_script(code: list[int], natives: list[int]) -> bytes:
    header_size = 0x38
    natives_off = header_size
    names_off = natives_off + 8 * len(natives)
    cod = names_off + 4
    body = compact(code)
    size = cod + len(body)
    dat = cod + 4 * len(code)
    header = struct.pack("<IHBBHHIIIIIIIIIII", size, 0xF1E0, 0x0A, 0x0A, 0x1C, 8, cod, dat, dat, dat + 0x100, 0,
                         natives_off, natives_off, names_off, names_off, names_off, names_off)
    table = b"".join(struct.pack("<II", 0, h) for h in natives)
    return header + table + b"\0" * 4 + body


@pytest.mark.parametrize("value", [0, 1, 63, 64, 127, 128, 0x1234, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFC0, 0x80000000])
def test_expand_round_trips(value):
    assert amx.expand(compact([value, 7])) == [value, 7]


def test_dialog_call_arguments_are_recovered_first_to_last():
    # proc 0: talk(speaker=3, message=12) through proc at 0x24, which calls native 0
    code = [
        amx.OP["PROC"],
        packed("PUSH_P_C", 12), packed("PUSH_P_C", 3), packed("PUSH_P_C", 8),
        amx.OP["CALL"], 0x24 - 0x10,
        amx.OP["ZERO_PRI"], amx.OP["RETN"],
        amx.OP["NOP"],
        amx.OP["PROC"],                                   # 0x24
        packed("PUSH_P_S", 16), packed("PUSH_P_S", 12),
        amx.OP["SYSREQ_N"], 0, 8,
        amx.OP["RETN"],
    ]
    script = amx.parse(make_script(code, [0xFD1CF919]))
    procs = amx.procedures(script)
    assert sorted(procs) == [0, 0x24]
    found = amx.calls(script, procs)
    assert [(c.target, c.args) for c in found if c.target is not None] == [(0x24, [3, 12])]
    assert [(c.native, c.args) for c in found if c.native is not None] == [(0xFD1CF919, [None, None])]


def test_parse_rejects_other_files():
    with pytest.raises(amx.AmxError):
        amx.parse(b"\0" * 0x40)


@pytest.mark.rom
def test_every_event_script_disassembles_with_consistent_targets(rom):
    from jpreader_tools.rom import parse_garc

    for blob in parse_garc(rom.romfs_file("a/0/9/2")):
        script = amx.parse(blob)
        instructions = amx.disassemble(script.code)
        starts = {ins.address for ins in instructions}
        procs = {ins.address for ins in instructions if ins.op == amx.OP["PROC"]}
        for ins in instructions:
            if ins.op == amx.OP["CALL"]:
                assert ins.address + ins.args[0] in procs
            elif ins.op in amx.JUMPS:
                assert ins.address + ins.args[0] in starts
