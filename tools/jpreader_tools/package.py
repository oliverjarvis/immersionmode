"""Write the output folders."""
from __future__ import annotations

import os
import shutil
from pathlib import Path

from .layout import TITLE_ID_STR



def config_text(kokoro_url: str | None, kokoro_token: str | None) -> str:
    """config.txt for the SD card. Grammar and words go through kokoro, so it holds kokoro's site URL and
    the device token from `jpreader kokoro-link`, and no LLM key. dist/ is never committed."""
    text = "debug=0\n"
    if kokoro_url and kokoro_token:
        text += f"kokoro_url={kokoro_url}\nkokoro_token={kokoro_token}\n"
    return text


def _write(path: Path, data: bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def _link(target: Path, source: Path) -> Path:
    """A hard link to a large build file (the voice clips): no copy of gigabytes on every build."""
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        if target.stat().st_ino == source.stat().st_ino:
            return target
        target.unlink()
    try:
        os.link(source, target)
    except OSError:
        shutil.copyfile(source, target)
    return target


def _place_links(folder: Path, sd_links: dict[str, Path]) -> list[Path]:
    (folder / "heads.bin").unlink(missing_ok=True)      # retired: ids.bin v2 has the spellings (9C)
    for old in folder.glob("voice-*.bin") if folder.exists() else ():
        if old.name not in sd_links:
            old.unlink()                     # a part an older build had and this one does not
    return [_link(folder / name, source) for name, source in sd_links.items()]


def write_dist(dist: Path, ips: bytes, exheader_luma: bytes, exheader_azahar: bytes, debug: bool,
               sd_files: dict[str, bytes], sd_links: dict[str, Path] | None = None) -> list[Path]:
    """Debug builds contain test-only hooks and are never written for the 3DS. sd_links are large files
    (the voice clips) placed as hard links to where the build made them."""
    sd_links = sd_links or {}
    if debug:
        root = dist / "azahar-debug"
        mods = root / "load" / "mods" / TITLE_ID_STR
        written = [_write(mods / "exefs" / "code.ips", ips), _write(mods / "exheader.bin", exheader_azahar)]
        written += [_write(root / "sdmc" / "jpreader" / name, data) for name, data in sd_files.items()]
        return written + _place_links(root / "sdmc" / "jpreader", sd_links)
    luma = dist / "3ds" / "luma" / "titles" / TITLE_ID_STR
    mods = dist / "azahar" / "load" / "mods" / TITLE_ID_STR
    written = [
        _write(luma / "code.ips", ips),
        _write(luma / "exheader.bin", exheader_luma),
        _write(mods / "exefs" / "code.ips", ips),
        _write(mods / "exheader.bin", exheader_azahar),
    ]
    for name, data in sd_files.items():
        written.append(_write(dist / "3ds" / "jpreader" / name, data))
        written.append(_write(dist / "azahar" / "sdmc" / "jpreader" / name, data))
    written += _place_links(dist / "3ds" / "jpreader", sd_links)
    written += _place_links(dist / "azahar" / "sdmc" / "jpreader", sd_links)
    return written
