"""Install, configure, launch and talk to the Azahar emulator."""
from __future__ import annotations

import configparser
import random
import shutil
import socket
import struct
import subprocess
import time
from pathlib import Path

from .layout import TITLE_ID_STR

AZAHAR_VERSION = "2126.1.2"
AZAHAR_URL = (f"https://github.com/azahar-emu/azahar/releases/download/{AZAHAR_VERSION}/"
              f"azahar-macos-arm64-{AZAHAR_VERSION}.zip")
RPC_PORT = 45987
PROCESS_PATTERN = "build/emu/Azahar.app/Contents/MacOS/azahar"   # this project's copy only
READ_MAX = 1024
WRITE_MAX = 1016

# Settings this project needs. Every key also gets a `key\default=false` twin.
SEED = {
    "Debugging": {"enable_rpc_server": "true"},
    "System": {"is_new_3ds": "false", "region_value": "0"},
    "Miscellaneous": {"check_for_update_on_start": "false"},
    "UI": {"confirmClose": "false", "firstStart": "false", "pauseWhenInBackground": "false"},
}


class EmuNotResponding(Exception):
    pass


def install(emu_dir: Path) -> Path:
    app = emu_dir / "Azahar.app"
    if app.exists():
        return app
    emu_dir.mkdir(parents=True, exist_ok=True)
    archive = emu_dir / "azahar.zip"
    subprocess.run(["curl", "-fL", "--retry", "3", "-o", str(archive), AZAHAR_URL], check=True)
    unpack = emu_dir / "unpack"
    subprocess.run(["ditto", "-x", "-k", str(archive), str(unpack)], check=True)
    found = next(unpack.rglob("Azahar.app"), None)
    if found is None:
        raise FileNotFoundError("Azahar.app was not in the downloaded archive")
    found.rename(app)
    shutil.rmtree(unpack)
    return app


def profile_dirs(profile: Path) -> dict[str, Path]:
    return {kind: profile / kind / "azahar-emu" for kind in ("data", "config", "cache")}


def seed_config(path: Path) -> None:
    parser = configparser.RawConfigParser(strict=False)
    parser.optionxform = str
    if path.exists():
        parser.read(path, encoding="utf-8")
    for group, settings in SEED.items():
        if not parser.has_section(group):
            parser.add_section(group)
        for key, value in settings.items():
            parser.set(group, key, value)
            parser.set(group, key + "\\default", "false")
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        parser.write(handle, space_around_delimiters=False)


def prepare_rom(source: Path, target: Path) -> Path:
    """Clone the ROM into the project so the emulator needs no access to ~/Downloads."""
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["cp", "-c", str(source), str(target)], check=True)
    return target


def install_mod(source: Path, profile: Path) -> None:
    shutil.copytree(source, profile_dirs(profile)["data"], dirs_exist_ok=True)


def remove_mod(profile: Path) -> None:
    shutil.rmtree(profile_dirs(profile)["data"] / "load" / "mods" / TITLE_ID_STR, ignore_errors=True)


def launch(app: Path, profile: Path, rom: Path) -> None:
    for directory in profile_dirs(profile).values():
        directory.mkdir(parents=True, exist_ok=True)
    command = ["open", "-n", "-a", str(app)]
    for variable, kind in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache")):
        command += ["--env", f"{variable}={profile / kind}"]
    subprocess.run(command + ["--args", "-w", str(rom)], check=True)


def is_running() -> bool:
    return subprocess.run(["pgrep", "-f", PROCESS_PATTERN], capture_output=True).returncode == 0


def stop() -> None:
    subprocess.run(["pkill", "-f", PROCESS_PATTERN], capture_output=True)
    for _ in range(50):
        if not is_running():
            return
        time.sleep(0.1)
    subprocess.run(["pkill", "-9", "-f", PROCESS_PATTERN], capture_output=True)


class Rpc:
    """Azahar's UDP memory channel."""

    def __init__(self, host: str = "127.0.0.1", port: int = RPC_PORT, timeout: float = 2.0):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)
        self._address = (host, port)

    def _call(self, kind: int, body: bytes) -> bytes:
        request_id = random.getrandbits(32)
        try:
            self._sock.sendto(struct.pack("<4I", 1, request_id, kind, len(body)) + body, self._address)
            while True:
                reply = self._sock.recv(READ_MAX + 64)
                version, reply_id, reply_kind, size = struct.unpack_from("<4I", reply)
                if (version, reply_id, reply_kind) == (1, request_id, kind) and size == len(reply) - 16:
                    return reply[16:]
        except (socket.timeout, ConnectionError) as error:
            raise EmuNotResponding(f"the emulator did not answer on UDP port {self._address[1]}") from error

    def read(self, addr: int, size: int) -> bytes:
        out = bytearray()
        while len(out) < size:
            chunk = self._call(1, struct.pack("<II", addr + len(out), min(READ_MAX, size - len(out))))
            if not chunk:
                raise EmuNotResponding(f"the emulator refused to read memory at {addr + len(out):#x}")
            out += chunk
        return bytes(out)

    def write(self, addr: int, data: bytes) -> None:
        for start in range(0, len(data), WRITE_MAX):
            chunk = data[start:start + WRITE_MAX]
            self._call(2, struct.pack("<II", addr + start, len(chunk)) + chunk)

    def processes(self) -> dict[int, tuple[int, str]]:
        found: dict[int, tuple[int, str]] = {}
        while True:
            reply = self._call(3, struct.pack("<II", len(found), 0x7FFFFFFF))
            count = struct.unpack_from("<I", reply)[0] if len(reply) >= 4 else 0
            if count == 0:
                return found
            for i in range(count):
                pid, title_id, name = struct.unpack_from("<IQ8s", reply, 4 + i * 20)
                found[pid] = (title_id, name.rstrip(b"\0").decode("ascii", "replace"))

    def set_process(self, pid: int) -> None:
        self._call(4, struct.pack("<II", 1, pid))

    def select_title(self, title_id: int) -> int:
        for pid, (process_title, _name) in self.processes().items():
            if process_title == title_id:
                self.set_process(pid)
                return pid
        raise EmuNotResponding(f"title {title_id:016X} is not running in the emulator")


def wait_for_rpc(rpc: Rpc, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while True:
        try:
            rpc.processes()
            return
        except EmuNotResponding:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)
