"""Screenshots of the emulator window, and a check for the panel's background colour."""
from __future__ import annotations

import subprocess
from pathlib import Path

PANEL_RGBS = ((0xF4, 0xF0, 0xE6), (0xFB, 0xF8, 0xF1))   # JPR_COLOR_BG and JPR_COLOR_CARD in panel.h
WINID_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "winid.swift"


class ScreenError(Exception):
    pass


def parse_windows(text: str) -> list[tuple[str, str, int, int]]:
    rows = []
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) == 4 and parts[0].isdigit():
            rows.append((parts[0], parts[1], int(parts[2]), int(parts[3])))
    return rows


def pick_window(rows, fragment: str = "Pokémon") -> str:
    games = [row for row in rows if fragment in row[1] and "Secondary" not in row[1]]
    if not games:
        raise ScreenError("no emulator window showing the game was found")
    return max(games, key=lambda row: row[2] * row[3])[0]


def capture(path: Path) -> Path:
    listing = subprocess.run(["swift", str(WINID_SCRIPT)], capture_output=True, text=True, check=True).stdout
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["screencapture", "-x", "-o", "-l", pick_window(parse_windows(listing)), str(path)],
                   check=True)
    return path


def colour_fraction(path: Path, rgb=None, tolerance: int = 6) -> float:
    """Share of the image's pixels within `tolerance`, on every channel, of `rgb` (one colour), or of
    either panel colour when it is not given."""
    from PIL import Image

    with Image.open(path) as image:
        small = image.convert("RGB").reduce(2)
    data = small.tobytes()
    count = len(data) // 3
    colours = (rgb,) if rgb is not None else PANEL_RGBS
    match = sum(1 for k in range(0, len(data), 3)
                if any(all(abs(data[k + i] - c[i]) <= tolerance for i in range(3)) for c in colours))
    return match / count
