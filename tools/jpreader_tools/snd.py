"""The game's own sound library (nn::snd::CTR), which the payload borrows one voice of to play voice clips
(spec: voice 4; research: docs/stage10-snd-findings.md). Exported functions are found in static.crs by name;
the few internal ones are fixed addresses in Ultra Moon USA 1.0, checked word by word so another version of
the game is refused at build time instead of misbehaving."""
from __future__ import annotations

import struct

from .hooks import HookError
from .layout import CODE_BASE

EXPORTED = {
    "JPR_SND_ALLOC_VOICE": "_ZN2nn3snd3CTR10AllocVoiceEiPFvPNS1_5VoiceEjEj",
    "JPR_SND_FREE_VOICE": "_ZN2nn3snd3CTR9FreeVoiceEPNS1_5VoiceE",
    "JPR_SND_INIT_WAVE": "_ZN2nn3snd3CTR20InitializeWaveBufferEPNS1_10WaveBufferE",
    "JPR_SND_APPEND": "_ZN2nn3snd3CTR5Voice16AppendWaveBufferEPNS1_10WaveBufferE",
    "JPR_SND_SET_CHANNELS": "_ZN2nn3snd3CTR5Voice15SetChannelCountEi",
    "JPR_SND_SET_FORMAT": "_ZN2nn3snd3CTR5Voice15SetSampleFormatENS1_12SampleFormatE",
    "JPR_SND_SET_RATE": "_ZN2nn3snd3CTR5Voice13SetSampleRateEi",
    "JPR_SND_SET_STATE": "_ZN2nn3snd3CTR5Voice8SetStateENS2_5StateE",
    "JPR_SND_FLUSH": "_ZN2nn3snd3CTR14FlushDataCacheEjj",
    "JPR_SND_SET_VOLUME": "_ZN2nn3snd3CTR5Voice9SetVolumeEf",
    # (frames r0, s0 volume): debug builds only, so the e2e can make the game set and fade its music players
    "JPR_MIX_CHANGE_BGM_PLAYER_VOLUME": "_ZN5Sound21ChangeBGMPlayerVolumeEfj",
}
FIXED = {
    "JPR_SND_LOCK": 0x25CAC0,          # the sound thread's lock: Enter on singleton+0x34
    "JPR_SND_UNLOCK": 0x25D418,
    "JPR_SND_DSP_ADDRESS": 0x172BE4,   # u32 f(u32 va, u32 bytes): device memory only (replaced by the payload)
    "JPR_SND_DEVMEM": 0x690514,        # {u32 address, u32 size} of nn::os device memory
    "JPR_SND_LINEAR_OFFSET": 0x69CA4C,  # linear VA -> physical address offset
    # The mixer (stage 11; docs/stage11-sound-findings.md): the game's 12 nw::snd players
    "JPR_MIX_SYS": 0x6915A4,           # *: the game's sound system; +4 the inner one, +0x53C its archive player
    "JPR_MIX_CS": 0x6A53A8,            # the game's sound critical section (recursive)
    "JPR_MIX_CS_ENTER": 0x1076A8,
    "JPR_MIX_CS_LEAVE": 0x107730,
    "JPR_MIX_LABEL_TO_ID": 0x3BCB1C,   # u32 (u32 label): the game's player label -> nw::snd player id
    "JPR_MIX_PLAYER_WRITE": 0x2AD0E8,  # (inner, id, s0 v): clamps v to >= 0 and stores the player's volume
}
EXPECTED = {                           # the words that show the fixed addresses are what the findings say
    0x25CAC0: 0xE92D4010, 0x25CAC4: 0xEBFFFD45,
    0x25D418: 0xE92D4010, 0x25D41C: 0xEBFFFAEF,
    0x172BE4: 0xE92D4070, 0x172C1C: 0xE59F0010, 0x172C20: 0xE5900004, 0x172C34: 0x0069CA48,
    0x1047B8: 0x00690514,
    0x3BA324: 0xE92D4010, 0x3BA328: 0xE1A04000, 0x3BA32C: 0xED2D8B02, 0x3BA330: 0xEEB08A40,   # set player volume
    0x4B7A90: 0xE92D4010, 0x4B7A94: 0xE1A04000, 0x4B7A98: 0xE1A00001, 0x4B7A9C: 0xEBFC141E,   # get player volume
    0x2AD0E8: 0xE92D4010, 0x2AD0EC: 0xE2800B01, 0x2AD120: 0xED800A0F,       # writes the player's +0x3C
    0x2AF364: 0xE590201C, 0x2AF36C: 0xE0601180,                           # GetSoundPlayer: stride 0x5C
    0x3BCB1C: 0xE1A01400,
    0x3BD038: 0xE59F0000, 0x3BD03C: 0xE12FFF1E, 0x3BD040: 0x006A53A8,     # the sound critical section
}


def snd_constants(code: bytes, syms: dict[str, int]) -> dict[str, int]:
    for addr, want in EXPECTED.items():
        got = struct.unpack_from("<I", code, addr - CODE_BASE)[0]
        if got != want:
            raise HookError(f"nn::snd: word {got:08x} at {addr:#x} is not {want:08x}; another game version?")
    out = dict(FIXED)
    for name, mangled in EXPORTED.items():
        if mangled not in syms:
            short = mangled.split("CTR", 1)[1]
            raise HookError(f"static.crs has no {short} (nn::snd); AllocVoice and friends are needed")
        out[name] = syms[mangled]
    return out
