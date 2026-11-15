"""Write (and, for tests, apply) IPS patches."""
from __future__ import annotations

EOF_OFFSET = 0x454F46          # these three bytes spell "EOF"
MAX_CHUNK = 0xFFFF


class IpsError(Exception):
    pass


def build_ips(records: list[tuple[int, bytes]]) -> bytes:
    out = bytearray(b"PATCH")
    for offset, data in records:
        position = 0
        while position < len(data):
            chunk = data[position:position + MAX_CHUNK]
            at = offset + position
            if at == EOF_OFFSET:
                raise IpsError("a record may not start at 0x454F46: it would read as the EOF marker")
            if at + len(chunk) > 0x1000000:
                raise IpsError("IPS cannot address data beyond 16 MB")
            out += at.to_bytes(3, "big")
            if len(chunk) > 3 and chunk.count(chunk[0]) == len(chunk):
                out += b"\x00\x00" + len(chunk).to_bytes(2, "big") + chunk[:1]
            else:
                out += len(chunk).to_bytes(2, "big") + chunk
            position += len(chunk)
    return bytes(out + b"EOF")


def apply_ips(base: bytes, patch: bytes) -> bytes:
    if patch[:5] != b"PATCH":
        raise IpsError("not an IPS patch")
    image = bytearray(base)
    position = 5
    while patch[position:position + 3] != b"EOF":
        if position + 5 > len(patch):
            raise IpsError("IPS patch ends without an EOF marker")
        offset = int.from_bytes(patch[position:position + 3], "big")
        size = int.from_bytes(patch[position + 3:position + 5], "big")
        position += 5
        if size == 0:
            run = int.from_bytes(patch[position:position + 2], "big")
            data = patch[position + 2:position + 3] * run
            position += 3
        else:
            data = patch[position:position + size]
            position += size
        if offset + len(data) > len(image):
            image.extend(bytes(offset + len(data) - len(image)))
        image[offset:offset + len(data)] = data
    return bytes(image)
