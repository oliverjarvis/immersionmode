"""Compile the payload and the loader stub."""
from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

from .hooks import render_trampolines


@dataclass(frozen=True)
class PayloadBuild:
    payload: bytes
    stub: bytes
    symbols: dict[str, int]


def parse_nm(text: str) -> dict[str, int]:
    symbols = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return symbols


def build_payload(root: Path, sites, entry_target: int, entry_return: int, debug: bool,
                  headers: dict[str, str], bearssl: Path) -> PayloadBuild:
    mode = "debug" if debug else "release"
    gen = root / "build" / f"gen-{mode}"
    out = root / "build" / f"payload-{mode}"
    gen.mkdir(parents=True, exist_ok=True)
    (gen / "hooks_gen.s").write_text(render_trampolines(sites) + "\n")
    for name, source in headers.items():
        path = gen / name
        if not path.exists() or path.read_text() != source:   # keep make's timestamps honest
            path.write_text(source)
    subprocess.run(
        ["make", "-C", str(root / "payload"), f"BUILD={out}", f"GEN={gen}", f"DEBUG={int(debug)}",
         f"ENTRY_TARGET=0x{entry_target:08x}", f"ENTRY_RETURN=0x{entry_return:08x}", f"BEARSSL={bearssl}"],
        check=True)
    return PayloadBuild(
        payload=(out / "payload.bin").read_bytes(),
        stub=(out / "stub.bin").read_bytes(),
        symbols=parse_nm((out / "symbols.txt").read_text()))
