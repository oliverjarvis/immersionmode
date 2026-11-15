import socket
import struct
import threading

import pytest

from jpreader_tools import emu


class FakeEmulator(threading.Thread):
    """Answers the UDP memory protocol over a small bytearray based at 0x100000."""

    def __init__(self, silent=False):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.base = 0x100000
        self.memory = bytearray(0x4000)
        self.selected = None
        self.silent = silent
        self.running = True

    def run(self):
        while self.running:
            try:
                packet, sender = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            if self.silent:
                continue
            _version, request_id, kind, _size = struct.unpack_from("<4I", packet)
            body = packet[16:]
            if kind == 1:
                addr, size = struct.unpack_from("<II", body)
                reply = bytes(self.memory[addr - self.base:addr - self.base + size])
            elif kind == 2:
                addr, size = struct.unpack_from("<II", body)
                self.memory[addr - self.base:addr - self.base + size] = body[8:8 + size]
                reply = b""
            elif kind == 3:
                start, _max = struct.unpack_from("<II", body)
                rows = [struct.pack("<IQ8s", 7, 0x0004013000001234, b"sysmod"),
                        struct.pack("<IQ8s", 12, 0x00040000001B5100, b"momiji")][start:]
                reply = struct.pack("<I", len(rows)) + b"".join(rows)
            else:
                self.selected = struct.unpack_from("<II", body)[1]
                reply = b""
            self.sock.sendto(struct.pack("<4I", 1, request_id, kind, len(reply)) + reply, sender)

    def close(self):
        self.running = False
        self.join()
        self.sock.close()


@pytest.fixture
def fake():
    server = FakeEmulator()
    server.start()
    yield server
    server.close()


def test_rpc_reads_and_writes_across_packets(fake):
    rpc = emu.Rpc(port=fake.port)
    data = bytes(i & 0xFF for i in range(3000))
    rpc.write(0x100100, data)
    assert rpc.read(0x100100, 3000) == data
    assert bytes(fake.memory[0x100:0x100 + 3000]) == data


def test_rpc_selects_the_game_process(fake):
    rpc = emu.Rpc(port=fake.port)
    assert rpc.processes() == {7: (0x0004013000001234, "sysmod"), 12: (0x00040000001B5100, "momiji")}
    assert rpc.select_title(0x00040000001B5100) == 12
    assert fake.selected == 12


def test_rpc_reports_a_missing_title(fake):
    with pytest.raises(emu.EmuNotResponding, match="not running"):
        emu.Rpc(port=fake.port).select_title(0x1)


def test_rpc_times_out_when_nothing_answers():
    server = FakeEmulator(silent=True)
    server.start()
    try:
        with pytest.raises(emu.EmuNotResponding, match="did not answer"):
            emu.Rpc(port=server.port, timeout=0.2).read(0x100000, 4)
    finally:
        server.close()


def test_rpc_reports_a_refused_read(fake):
    fake.memory = bytearray(0)
    with pytest.raises(emu.EmuNotResponding, match="refused"):
        emu.Rpc(port=fake.port).read(0x100000, 4)


def test_seed_config_creates_keys_with_default_twins(tmp_path):
    path = tmp_path / "config" / "qt-config.ini"
    emu.seed_config(path)
    text = path.read_text()
    assert "[Debugging]" in text
    assert "enable_rpc_server=true" in text
    assert "enable_rpc_server\\default=false" in text
    assert "is_new_3ds=false" in text
    assert "is_new_3ds\\default=false" in text


def test_seed_config_keeps_existing_settings(tmp_path):
    path = tmp_path / "qt-config.ini"
    path.write_text("[Renderer]\nresolution_factor=2\n[Debugging]\nenable_rpc_server=false\n")
    emu.seed_config(path)
    text = path.read_text()
    assert "resolution_factor=2" in text
    assert "enable_rpc_server=true" in text
    assert "enable_rpc_server=false" not in text


def test_profile_dirs(tmp_path):
    dirs = emu.profile_dirs(tmp_path)
    assert dirs == {
        "data": tmp_path / "data" / "azahar-emu",
        "config": tmp_path / "config" / "azahar-emu",
        "cache": tmp_path / "cache" / "azahar-emu",
    }


def test_install_and_remove_mod(tmp_path):
    source = tmp_path / "dist"
    (source / "load" / "mods" / "00040000001B5100" / "exefs").mkdir(parents=True)
    (source / "load" / "mods" / "00040000001B5100" / "exefs" / "code.ips").write_bytes(b"PATCHEOF")
    profile = tmp_path / "profile"
    emu.install_mod(source, profile)
    installed = emu.profile_dirs(profile)["data"] / "load" / "mods" / "00040000001B5100"
    assert (installed / "exefs" / "code.ips").read_bytes() == b"PATCHEOF"
    emu.remove_mod(profile)
    assert not installed.exists()
    emu.remove_mod(profile)          # removing twice is fine
