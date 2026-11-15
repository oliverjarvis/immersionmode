"""Game Freak containers inside GARC entries: LZ11 compression and 'BinLinker' mini packs."""
from __future__ import annotations

import struct


def lz11(src: bytes) -> bytes:
    """Nintendo LZ11 (type 0x11) decompression."""
    if src[:1] != b"\x11":
        raise ValueError("not LZ11 data")
    size = src[1] | src[2] << 8 | src[3] << 16
    i = 4
    if size == 0:
        size = struct.unpack_from("<I", src, 4)[0]
        i = 8
    out = bytearray()
    while len(out) < size:
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(src[i])
                i += 1
                continue
            b = src[i]
            kind = b >> 4
            if kind == 0:
                n = ((b & 0xF) << 4 | src[i + 1] >> 4) + 0x11
                d = ((src[i + 1] & 0xF) << 8 | src[i + 2]) + 1
                i += 3
            elif kind == 1:
                n = ((b & 0xF) << 12 | src[i + 1] << 4 | src[i + 2] >> 4) + 0x111
                d = ((src[i + 2] & 0xF) << 8 | src[i + 3]) + 1
                i += 4
            else:
                n = kind + 1
                d = ((b & 0xF) << 8 | src[i + 1]) + 1
                i += 2
            for _ in range(n):
                out.append(out[-d])
    return bytes(out)


def unpack(blob: bytes) -> bytes:
    """Decompress `blob` when it is LZ11 data, else return it unchanged."""
    if blob[:1] == b"\x11":
        try:
            return lz11(blob)
        except (IndexError, ValueError, struct.error):
            return blob
    return blob


def mini(blob: bytes) -> tuple[str, list[bytes]]:
    """A mini pack: 2-character magic, u16 count, then count + 1 u32 offsets."""
    count = struct.unpack_from("<H", blob, 2)[0]
    offsets = struct.unpack_from(f"<{count + 1}I", blob, 4)
    return blob[:2].decode("latin1"), [blob[offsets[i]:offsets[i + 1]] for i in range(count)]
