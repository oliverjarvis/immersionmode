"""Build the exheader.bin variants that make room for the payload."""
from __future__ import annotations

import struct

from .layout import IMAGE_END

PAGE = 0x1000
DATA_INFO = 0x30            # u32 address, u32 page count, u32 byte size
BSS_SIZE = 0x3C
KERNEL_CAPS = 0x370         # 28 descriptors in the access control block
KERNEL_CAPS_COPY = 0x770    # the same list inside the access descriptor
SVC_CONTROL_PROCESS_MEMORY = 0x70
SVC_FLUSH_PROCESS_DATA_CACHE = 0x54
EXTRA_SVCS = (SVC_CONTROL_PROCESS_MEMORY, SVC_FLUSH_PROCESS_DATA_CACHE)
_SVC_PREFIX = 0x1E          # top five bits of an SVC table descriptor
_UNUSED = 0xFFFFFFFF


class ExheaderError(Exception):
    pass


def allowed_svcs(exh: bytes, base: int = KERNEL_CAPS) -> set[int]:
    allowed: set[int] = set()
    for descriptor in struct.unpack_from("<28I", exh, base):
        if descriptor != _UNUSED and descriptor >> 27 == _SVC_PREFIX:
            index = (descriptor >> 24) & 7
            allowed |= {index * 24 + bit for bit in range(24) if descriptor >> bit & 1}
    return allowed


def _allow_svc(buf: bytearray, base: int, svc: int) -> None:
    index, bit = divmod(svc, 24)
    slots = list(struct.unpack_from("<28I", buf, base))
    for i, descriptor in enumerate(slots):
        if descriptor != _UNUSED and descriptor >> 27 == _SVC_PREFIX and (descriptor >> 24) & 7 == index:
            slots[i] = descriptor | (1 << bit)
            break
    else:
        for i, descriptor in enumerate(slots):
            if descriptor == _UNUSED:
                slots[i] = (_SVC_PREFIX << 27) | (index << 24) | (1 << bit)
                break
        else:
            raise ExheaderError("no free kernel capability slot for the extra SVC")
    struct.pack_into("<28I", buf, base, *slots)


def patch_exheader(exh: bytes, variant: str) -> bytes:
    """Allow the loader stub's extra SVCs, and on Luma make the code file cover the staged payload.

    The image must still end at IMAGE_END: the game maps its CRO modules from there upward.
    """
    if len(exh) != 0x800:
        raise ExheaderError("expected the full 0x800-byte exheader")
    if variant not in ("luma", "azahar"):
        raise ExheaderError(f"unknown variant {variant!r}")
    buf = bytearray(exh)
    if variant == "luma":
        # Luma loads only text, rodata and data from code.bin, so data must reach the image end.
        address, pages, _size = struct.unpack_from("<III", buf, DATA_INFO)
        if address + pages * PAGE > IMAGE_END:
            raise ExheaderError("the data segment already reaches past the image end")
        new_size = IMAGE_END - address
        struct.pack_into("<III", buf, DATA_INFO, address, new_size // PAGE, new_size)
        struct.pack_into("<I", buf, BSS_SIZE, 0)
    # Azahar sizes its buffer as code.bin plus page-rounded bss, which already covers the stage.
    for svc in EXTRA_SVCS:
        _allow_svc(buf, KERNEL_CAPS, svc)
        _allow_svc(buf, KERNEL_CAPS_COPY, svc)
    return bytes(buf[:0x400]) if variant == "luma" else bytes(buf)
