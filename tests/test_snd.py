import struct

import pytest

from jpreader_tools import snd
from jpreader_tools.hooks import HookError
from jpreader_tools.layout import CODE_BASE


def game(words: dict[int, int]) -> bytes:
    code = bytearray(0x600000)
    for addr, word in words.items():
        struct.pack_into("<I", code, addr - CODE_BASE, word)
    return bytes(code)


EXPORTS = {mangled: 0x200000 + 4 * n for n, mangled in enumerate(snd.EXPORTED.values())}


def test_constants_name_every_function_and_check_the_fixed_ones():
    consts = snd.snd_constants(game(snd.EXPECTED), EXPORTS)
    assert consts["JPR_SND_ALLOC_VOICE"] == EXPORTS[snd.EXPORTED["JPR_SND_ALLOC_VOICE"]]
    assert consts["JPR_SND_LOCK"] == 0x25CAC0 and consts["JPR_SND_DEVMEM"] == 0x690514
    assert consts["JPR_SND_LINEAR_OFFSET"] == 0x69CA4C


def test_the_mixer_constants_are_the_findings_addresses():
    consts = snd.snd_constants(game(snd.EXPECTED), EXPORTS)
    assert consts["JPR_SND_SET_VOLUME"] == EXPORTS[snd.EXPORTED["JPR_SND_SET_VOLUME"]]   # Voice::SetVolume
    assert (consts["JPR_MIX_SYS"], consts["JPR_MIX_CS"], consts["JPR_MIX_LABEL_TO_ID"]) == (0x6915A4, 0x6A53A8, 0x3BCB1C)
    assert (consts["JPR_MIX_CS_ENTER"], consts["JPR_MIX_CS_LEAVE"], consts["JPR_MIX_PLAYER_WRITE"]) == \
        (0x1076A8, 0x107730, 0x2AD0E8)
    for addr in (0x3BA324, 0x4B7A90, 0x2AD0E8, 0x2AF364, 0x3BCB1C):
        assert addr in snd.EXPECTED                       # each is checked word by word at build time


def test_another_game_version_is_refused():
    words = dict(snd.EXPECTED)
    words[0x25CAC0] = 0xE1A00000
    with pytest.raises(HookError, match="0x25cac0"):
        snd.snd_constants(game(words), EXPORTS)
    with pytest.raises(HookError, match="AllocVoice"):
        snd.snd_constants(game(snd.EXPECTED), {})
