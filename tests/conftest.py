import os
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def _load_env() -> None:
    env = ROOT / ".env"
    if not env.exists():
        return
    for line in env.read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip())


_load_env()
os.environ.setdefault("JPREADER_LLM", "0")   # the suite never calls Claude; cached decisions still apply


@pytest.fixture(scope="session")
def rom_path() -> Path:
    path = os.environ.get("JPREADER_ROM")
    if not path or not Path(path).exists():
        pytest.skip("JPREADER_ROM is not set or the file is missing")
    return Path(path)


@pytest.fixture(scope="session")
def rom(rom_path):
    from jpreader_tools.rom import Rom

    with Rom(rom_path) as opened:
        yield opened
