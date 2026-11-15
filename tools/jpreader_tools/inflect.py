"""Short English labels for conjugation endings, and notes for particles (hand-written data)."""
from __future__ import annotations

from pathlib import Path

DATA_DIR = Path(__file__).resolve().parents[1] / "data"


def load_tsv(path: Path) -> dict[str, str]:
    table = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        key, value = line.split("\t", 1)
        table[key] = value.strip()
    return table


def inflection_labels() -> dict[str, str]:
    return load_tsv(DATA_DIR / "inflections.tsv")


def particle_notes() -> dict[str, str]:
    return load_tsv(DATA_DIR / "particles.tsv")


def label(endings, labels: dict[str, str]) -> str | None:
    return labels.get("+".join(endings)) if endings else None
