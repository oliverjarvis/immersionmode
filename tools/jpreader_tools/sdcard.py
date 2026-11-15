"""Copy the release build onto a mounted 3DS SD card (spec 5.3)."""
from __future__ import annotations

import shutil
from pathlib import Path

from .layout import TITLE_ID_STR


class CardError(Exception):
    pass


def _same(source: Path, target: Path) -> bool:
    """The card already holds this build's copy: same size, and copied after the build made it."""
    if not target.exists():
        return False
    a, b = source.stat(), target.stat()
    return a.st_size == b.st_size and b.st_mtime >= a.st_mtime and source.name.startswith("voice")


RETIRED = ("heads.bin",)      # files older builds put on the card; ids.bin v2 replaced heads.bin (9C)


def install(dist: Path, card: Path, debug_line: bool = False) -> list[Path]:
    source = dist / "3ds"
    if not (source / "luma" / "titles" / TITLE_ID_STR / "code.ips").exists():
        raise CardError(f"no release build in {source}: run `jpreader build` first")
    if not (card / "luma").is_dir():
        raise CardError(f"{card} has no luma folder: give the root of the 3DS SD card")
    written = []
    for name in RETIRED:
        (card / "jpreader" / name).unlink(missing_ok=True)
    for folder in (Path("luma") / "titles" / TITLE_ID_STR, Path("jpreader")):
        (card / folder).mkdir(parents=True, exist_ok=True)
        for item in sorted((source / folder).iterdir()):
            target = card / folder / item.name
            if _same(item, target):
                continue                     # a large voice part already copied: hundreds of MB saved
            shutil.copyfile(item, target)
            written.append(target)
    if debug_line:
        config = card / "jpreader" / "config.txt"
        config.write_text(config.read_text().replace("debug=0", "debug=1"))
    return written
