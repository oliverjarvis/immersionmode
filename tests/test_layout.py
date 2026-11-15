import re
from pathlib import Path

from jpreader_tools.layout import PAYLOAD_BASE, PAYLOAD_END, PAYLOAD_SIZE, SOCK_BUF_ADDR

ROOT = Path(__file__).resolve().parent.parent


def test_payload_block_sits_just_below_the_socket_buffer():
    assert PAYLOAD_SIZE == 0x90000                       # 576 KB: the font with its italic face, and the redesign
    assert PAYLOAD_END == SOCK_BUF_ADDR


def test_makefile_and_linker_script_agree_with_layout():
    makefile = (ROOT / "payload" / "Makefile").read_text()
    linker = (ROOT / "payload" / "link.ld").read_text()
    assert int(re.search(r"PAYLOAD_BASE := (0x[0-9A-Fa-f]+)", makefile).group(1), 16) == PAYLOAD_BASE
    assert int(re.search(r"PAYLOAD_SIZE := (0x[0-9A-Fa-f]+)", makefile).group(1), 16) == PAYLOAD_SIZE
    origin, length = re.search(r"ORIGIN = (0x[0-9A-Fa-f]+), LENGTH = (0x[0-9A-Fa-f]+)", linker).groups()
    assert (int(origin, 16), int(length, 16)) == (PAYLOAD_BASE, PAYLOAD_SIZE)


def test_panel_image_sits_just_below_the_payload():
    from jpreader_tools.layout import PANEL_IMAGE_ADDR, PANEL_IMAGE_SIZE
    assert PANEL_IMAGE_ADDR + PANEL_IMAGE_SIZE == PAYLOAD_BASE
    assert PANEL_IMAGE_SIZE >= 320 * 240 * 4                   # one bottom screen in the widest format, RGBA8
    assert PANEL_IMAGE_ADDR % 0x1000 == 0 and PANEL_IMAGE_SIZE % 0x1000 == 0


def test_game_header_agrees_with_layout():
    from jpreader_tools.layout import PANEL_IMAGE_ADDR, PANEL_IMAGE_SIZE, SOCK_BUF_SIZE
    header = (ROOT / "payload" / "src" / "platform" / "game.h").read_text()
    defined = {name: int(value, 16) for name, value in re.findall(r"#define (JPR_\w+)\s+(0x[0-9A-Fa-f]+)", header)}
    assert defined["JPR_SOCK_BUF_ADDR"] == SOCK_BUF_ADDR and defined["JPR_SOCK_BUF_SIZE"] == SOCK_BUF_SIZE
    assert defined["JPR_PANEL_IMAGE_ADDR"] == PANEL_IMAGE_ADDR and defined["JPR_PANEL_IMAGE_SIZE"] == PANEL_IMAGE_SIZE
    from jpreader_tools.layout import KOKORO_ADDR, KOKORO_SIZE
    assert defined["JPR_KOKORO_ADDR"] == KOKORO_ADDR and defined["JPR_KOKORO_SIZE"] == KOKORO_SIZE


def test_kokoro_block_sits_below_the_panel_image():
    from jpreader_tools.layout import KOKORO_ADDR, KOKORO_SIZE, PANEL_IMAGE_ADDR
    assert KOKORO_ADDR + KOKORO_SIZE == PANEL_IMAGE_ADDR
    assert KOKORO_ADDR % 0x1000 == 0 and KOKORO_SIZE >= 0x8000 + 2 * 0x2000   # the list, then two sets of marks
