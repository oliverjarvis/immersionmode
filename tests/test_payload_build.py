import struct
from pathlib import Path

import pytest

from jpreader_tools.hooks import entry_redirect, plan_hooks
from jpreader_tools.layout import PAYLOAD_BASE, PAYLOAD_END, STAGE_BASE, STAGE_MAX, STUB_MAX
from jpreader_tools.payload import build_payload, parse_nm
from jpreader_tools import tls
from jpreader_tools.symbols import parse_crs_exports

ROOT = Path(__file__).resolve().parent.parent


def test_parse_nm():
    text = "006d4000 T jpr_init\n006e0000 B jpr_cap\n         U missing\n"
    assert parse_nm(text) == {"jpr_init": 0x6D4000, "jpr_cap": 0x6E0000}


def _headers(code, exports):
    from jpreader_tools.build import generated_headers
    from jpreader_tools.font import BUILTIN_CHARS, SMALL_FACE, build_font, fetch_noto, render_face

    builtin = build_font([render_face(fetch_noto(ROOT / "build" / "cache"), *SMALL_FACE, set(BUILTIN_CHARS))])
    headers = generated_headers(code, builtin, exports)
    bearssl = tls.fetch_bearssl(ROOT / "build" / "cache")
    headers["trust_anchors.h"] = tls.trust_anchors_header(tls.build_brssl(bearssl), tls.certificate_files(ROOT))
    return headers


BEARSSL = tls.fetch_bearssl(ROOT / "build" / "cache")


@pytest.mark.rom
@pytest.mark.parametrize("debug", [False, True])
def test_payload_builds_within_budget(rom, debug):
    code = rom.exefs_file(".code")
    sites = plan_hooks(code, parse_crs_exports(rom.romfs_file("static.crs")), debug)
    target, ret = entry_redirect(code)
    built = build_payload(ROOT, sites, target, ret, debug, _headers(code, parse_crs_exports(rom.romfs_file("static.crs"))), BEARSSL)

    assert built.symbols["jpr_init"] == PAYLOAD_BASE
    assert 0 < len(built.payload) <= STAGE_MAX
    assert built.symbols["__jpr_bss_end"] <= PAYLOAD_END
    for site in sites:
        assert PAYLOAD_BASE <= built.symbols[f"jpr_tramp_{site.name}"] < PAYLOAD_END
    assert "jpr_tramp_pad" in built.symbols and "jpr_tramp_stick" in built.symbols   # SELECT opens the peek (stage 13)
    assert "jpr_tramp_present" in built.symbols and "jpr_tramp_touch" in built.symbols
    assert PAYLOAD_BASE < built.symbols["jpr_panel"] < PAYLOAD_END
    assert "jpr_talk" in built.symbols
    assert PAYLOAD_BASE < built.symbols["jpr_cap"] < PAYLOAD_END
    assert PAYLOAD_BASE < built.symbols["jpr_dbg"] < PAYLOAD_END

    assert 0 < len(built.stub) <= STUB_MAX
    assert struct.unpack_from("<I", built.stub, 0)[0] == 0xE92D5FFF      # push {r0-r12, lr}
    for svc in (0xEF000001, 0xEF000070, 0xEF000054, 0xEF00003C):         # alloc, protect, flush, break
        assert struct.pack("<I", svc) in built.stub
    assert struct.pack("<I", PAYLOAD_BASE) in built.stub
    assert struct.pack("<I", STAGE_BASE) in built.stub
    assert struct.pack("<I", target) in built.stub


@pytest.mark.rom
def test_stub_copies_and_flushes_before_making_the_payload_executable(rom):
    code = rom.exefs_file(".code")
    sites = plan_hooks(code, parse_crs_exports(rom.romfs_file("static.crs")), False)
    target, ret = entry_redirect(code)
    stub = build_payload(ROOT, sites, target, ret, False, _headers(code, parse_crs_exports(rom.romfs_file("static.crs"))), BEARSSL).stub
    words = list(struct.unpack(f"<{len(stub) // 4}I", stub))
    assert words.index(0xEF000054) < words.index(0xEF000070) < words.index(0xE12FFF32)   # flush, protect, blx
    # a refused protection call stops with its own break code instead of jumping into the payload
    assert words.count(0xEF00003C) >= 2


@pytest.mark.rom
def test_stub_gives_the_kernel_a_real_process_handle(rom):
    """On a 3DS, svcControlProcessMemory refuses the 0xFFFF8001 pseudo-handle (0xD9001BF7, invalid handle)."""
    code = rom.exefs_file(".code")
    sites = plan_hooks(code, parse_crs_exports(rom.romfs_file("static.crs")), False)
    target, ret = entry_redirect(code)
    stub = build_payload(ROOT, sites, target, ret, False, _headers(code, parse_crs_exports(rom.romfs_file("static.crs"))), BEARSSL).stub
    words = list(struct.unpack(f"<{len(stub) // 4}I", stub))
    assert words.index(0xEF000027) < words.index(0xEF000054)                     # svcDuplicateHandle first
    assert words.count(0xEF000027) == 1



@pytest.mark.rom
def test_payload_never_flushes_the_cache(rom):
    """On a 3DS, svcFlushProcessDataCache on the game's bottom-screen buffer each frame breaks the game's
    Internet connection (Festival Plaza 003-1099; without the flush, 002-0110 from the closed server).
    The panel draws correctly without it, and the payload has no other use for it: only the loader stub
    flushes, once, with its own svc."""
    code = rom.exefs_file(".code")
    sites = plan_hooks(code, parse_crs_exports(rom.romfs_file("static.crs")), False)
    target, ret = entry_redirect(code)
    built = build_payload(ROOT, sites, target, ret, False, _headers(code, parse_crs_exports(rom.romfs_file("static.crs"))), BEARSSL)
    assert "svcFlushProcessDataCache" not in built.symbols
    assert struct.pack("<I", 0xEF000054) not in built.payload          # svc 0x54


def test_the_payload_reads_the_files_the_build_writes():
    import re
    import struct as st
    from jpreader_tools import build, font, formats
    source = (ROOT / "payload" / "src" / "platform" / "screen.c").read_text()
    display = (ROOT / "payload" / "src" / "platform" / "display.c").read_text()
    assert int(re.search(r"#define FONT_BYTES (0x[0-9A-Fa-f]+)", display).group(1), 16) == font.FONT_MAX_BYTES
    magic = lambda name: int(re.search(rf"#define {name} (0x[0-9A-Fa-f]+)u", source).group(1), 16)
    assert magic("STRINGS_MAGIC") == st.unpack("<I", formats.STRINGS_MAGIC)[0]
    assert magic("NAMES_MAGIC") == st.unpack("<I", formats.NAMES_MAGIC)[0]
    assert int(re.search(r"#define STRINGS_HEADER (\d+)", source).group(1)) == formats.STRINGS_HEADER
    assert int(re.search(r"#define STRINGS_ENTRY (\d+)", source).group(1)) == formats.STRINGS_ENTRY
    assert int(re.search(r"#define STRINGS_VERSION (\d+)", source).group(1)) == formats.STRINGS_VERSION
    assert int(re.search(r"#define BUCKET_READ (\d+)", source).group(1)) == formats.STRINGS_BUCKET_MAX
    assert int(re.search(r"#define EN_MAX (\d+)", source).group(1)) == build.ENGLISH_MAX
    assert int(re.search(r"#define TEXT_MAX (\d+)", source).group(1)) == formats.SCREEN_KEY_MAX
