"""Read-only access to a decrypted Nintendo 3DS cartridge image (.cci or .3ds)."""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from .layout import TITLE_ID

MEDIA_UNIT = 0x200
EXPECTED_CODE_SHA256 = "0aa12056d403a7d747d9c299e1dfbd3c0d5e4ce147675bc8300a8d61e999919b"
_NO_ENTRY = 0xFFFFFFFF


class WrongRomError(Exception):
    """The file is not the ROM this project targets."""


def check_rom_identity(title_id: int, code_sha256: str, no_crypto: bool) -> None:
    if not no_crypto:
        raise WrongRomError("ROM is encrypted; a decrypted image is required")
    if title_id != TITLE_ID:
        raise WrongRomError(f"title ID {title_id:016X} is not Pokémon Ultra Moon ({TITLE_ID:016X})")
    if code_sha256 != EXPECTED_CODE_SHA256:
        raise WrongRomError(
            "executable does not match Ultra Moon v1.0; updated or modified ROMs are not supported")


def parse_garc(blob: bytes) -> list[bytes]:
    """Return the first sub-file of every entry in a GARC archive."""
    if blob[:4] != b"CRAG":
        raise ValueError("not a GARC archive")
    header_size, _bom, _version, _chunks, data_off, _length = struct.unpack_from("<IHHIII", blob, 4)
    fato = header_size
    if blob[fato:fato + 4] != b"OTAF":
        raise ValueError("GARC: missing FATO section")
    fato_size, count = struct.unpack_from("<IH", blob, fato + 4)
    entry_offsets = struct.unpack_from(f"<{count}I", blob, fato + 12)
    fatb = fato + fato_size
    if blob[fatb:fatb + 4] != b"BTAF":
        raise ValueError("GARC: missing FATB section")
    files = []
    for entry in entry_offsets:
        pos = fatb + 12 + entry
        bits = struct.unpack_from("<I", blob, pos)[0]
        if not bits & 1:
            files.append(b"")
            continue
        start, _end, length = struct.unpack_from("<III", blob, pos + 4)
        files.append(blob[data_off + start:data_off + start + length])
    return files


class Rom:
    def __init__(self, path: str | Path):
        self._f = open(path, "rb")
        try:
            if self._read(0x100, 4) != b"NCSD":
                raise WrongRomError("not a 3DS cartridge image (no NCSD header)")
            partition, _ = struct.unpack("<II", self._read(0x120, 8))
            self._ncch = partition * MEDIA_UNIT
            header = self._read(self._ncch, 0x200)
            if header[0x100:0x104] != b"NCCH":
                raise WrongRomError("first partition is not an NCCH container")
        except Exception:
            self._f.close()
            raise
        self.title_id = struct.unpack_from("<Q", header, 0x118)[0]
        self.no_crypto = bool(header[0x18F] & 0x04)
        self._exefs = self._ncch + struct.unpack_from("<I", header, 0x1A0)[0] * MEDIA_UNIT
        self._romfs = self._ncch + struct.unpack_from("<I", header, 0x1B0)[0] * MEDIA_UNIT
        self._level3: int | None = None

    def __enter__(self) -> "Rom":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        self._f.close()

    def _read(self, offset: int, size: int) -> bytes:
        self._f.seek(offset)
        return self._f.read(size)

    def exheader(self) -> bytes:
        return self._read(self._ncch + 0x200, 0x800)

    def exefs_file(self, name: str) -> bytes:
        header = self._read(self._exefs, 0x200)
        for i in range(10):
            entry_name, offset, size = struct.unpack_from("<8sII", header, i * 16)
            if entry_name.rstrip(b"\0").decode("ascii", "replace") == name:
                return self._read(self._exefs + 0x200 + offset, size)
        raise FileNotFoundError(f"ExeFS has no file named {name}")

    def code_sha256(self) -> str:
        return hashlib.sha256(self.exefs_file(".code")).hexdigest()

    def verify(self) -> None:
        # Encryption and title ID first: an encrypted ExeFS header cannot even be listed.
        check_rom_identity(self.title_id, EXPECTED_CODE_SHA256, self.no_crypto)
        check_rom_identity(self.title_id, self.code_sha256(), self.no_crypto)

    def _init_romfs(self) -> None:
        ivfc = self._read(self._romfs, 0x60)
        if ivfc[:4] != b"IVFC":
            raise WrongRomError("RomFS is not readable (no IVFC header)")
        master_hash_size = struct.unpack_from("<I", ivfc, 0x08)[0]
        block = 1 << struct.unpack_from("<I", ivfc, 0x4C)[0]
        self._level3 = self._romfs + -(-(0x60 + master_hash_size) // block) * block
        fields = struct.unpack("<10I", self._read(self._level3, 0x28))
        dir_off, dir_len, file_off, file_len, self._data_off = fields[3], fields[4], fields[7], fields[8], fields[9]
        self._dirs = self._read(self._level3 + dir_off, dir_len)
        self._files = self._read(self._level3 + file_off, file_len)

    def _children(self, dir_offset: int) -> tuple[dict[str, int], dict[str, tuple[int, int]]]:
        _parent, _sibling, child, first_file = struct.unpack_from("<4I", self._dirs, dir_offset)
        dirs: dict[str, int] = {}
        files: dict[str, tuple[int, int]] = {}
        while child != _NO_ENTRY:
            _p, sibling, _c, _f, _h, name_len = struct.unpack_from("<6I", self._dirs, child)
            dirs[self._dirs[child + 24:child + 24 + name_len].decode("utf-16le")] = child
            child = sibling
        while first_file != _NO_ENTRY:
            _p, sibling, data_off, size, _h, name_len = struct.unpack_from("<IIQQII", self._files, first_file)
            name = self._files[first_file + 32:first_file + 32 + name_len].decode("utf-16le")
            files[name] = (data_off, size)
            first_file = sibling
        return dirs, files

    def romfs_file(self, path: str) -> bytes:
        if self._level3 is None:
            self._init_romfs()
        parts = path.strip("/").split("/")
        directory = 0
        for part in parts[:-1]:
            dirs, _ = self._children(directory)
            if part not in dirs:
                raise FileNotFoundError(path)
            directory = dirs[part]
        _, files = self._children(directory)
        if parts[-1] not in files:
            raise FileNotFoundError(path)
        data_off, size = files[parts[-1]]
        return self._read(self._level3 + self._data_off + data_off, size)
