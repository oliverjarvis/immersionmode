import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST_TESTS = ["capture", "surface", "font", "draw", "layout", "panel", "ipc", "util", "annot", "card", "string", "config", "url", "kokoro", "word", "outbox", "sentence", "json", "http", "grammar", "status", "voice", "reply"]


def test_payload_logic_on_host():
    build = ROOT / "build" / "host"
    result = subprocess.run(
        ["make", "-C", str(ROOT / "payload"), "host-test", f"BUILD={build}"],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    for name in HOST_TESTS:
        assert f"{name}: all checks passed" in result.stdout
