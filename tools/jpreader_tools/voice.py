"""voice.bin: the voice clips for the 3DS (spec: voice 3). Each clip in build/tts/clips.csv becomes
22,050 Hz 16-bit mono PCM twice, at normal speed and at 0.7x with the pitch kept, loudness evened out.
An entry's key is (file, line): a story file as it is, a system file with SYSTEM set, so the two sets
never meet (story and system file numbers overlap; payloads from before system clips never look one up).
The clips stay in build/ and dist/: never committed."""
from __future__ import annotations

import csv
import hashlib
import struct
import subprocess
from pathlib import Path

MAGIC = b"JPRV"
VERSION = 2
RATE = 22050
HEADER = 20
ENTRY = 24
PART_MAX = 1 << 30          # bytes per voice-N.bin: SD cards are FAT32, which holds no file of 4 GB or more
FFMPEG = "/opt/homebrew/bin/ffmpeg"
LOUDNORM = "loudnorm=I=-16:TP=-1.5:LRA=11"
SYSTEM = 0x8000             # added to a system text file's number in an entry's key


def _filters(slow: bool) -> str:
    return ("atempo=0.7," if slow else "") + LOUDNORM


def convert(mp3: Path, slow: bool, cache: Path) -> bytes:
    """The clip as raw PCM16LE mono at RATE; cached by the MP3 and the conversion settings."""
    digest = hashlib.sha1(mp3.read_bytes() + f"|{_filters(slow)}|{RATE}".encode()).hexdigest()
    out = cache / f"{digest}.pcm"
    if not out.exists():
        pcm = subprocess.run([FFMPEG, "-v", "error", "-i", str(mp3), "-af", _filters(slow), "-ac", "1", "-ar", str(RATE),
                              "-f", "s16le", "-"], check=True, capture_output=True).stdout
        cache.mkdir(parents=True, exist_ok=True)
        out.write_bytes(pcm)
    return out.read_bytes()


def _replace(path: Path, data: bytes) -> None:
    """A new file at path, never the old one rewritten: dist/ hardlinks the files, and an older dist must keep its
    own index with its own samples (writing in place once gave an old dist a new index over old samples)."""
    partial = path.with_name(path.name + ".part")
    partial.write_bytes(data)
    partial.replace(path)


def read_clips(path: Path) -> tuple[list[tuple[int, int, Path]], list[str]]:
    clips, skipped = [], []
    with open(path, encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            mp3 = path.parent / row["clip"]
            kind = row.get("set") or "story"             # clips.csv from before system clips has no set
            try:
                file, line = int(row["story_file"]), int(row["line"])
            except ValueError:
                skipped.append(f"{row['clip']}: story_file/line {row['story_file']!r}/{row['line']!r} are not numbers")
                continue
            if kind not in ("story", "system"):
                skipped.append(f"{row['clip']}: set {kind!r} is neither story nor system")
                continue
            if not mp3.exists():
                skipped.append(f"{row['clip']}: no such file")
                continue
            if not (0 <= file < SYSTEM and 0 <= line < 0x10000):
                skipped.append(f"{row['clip']}: story_file/line out of range")
                continue
            clips.append((file | (SYSTEM if kind == "system" else 0), line, mp3))
    return clips, skipped


def _manifest(keys, unique, part_max: int) -> str:
    digest = hashlib.sha1(f"{VERSION}|{RATE}|{part_max}|{_filters(False)}|{_filters(True)}".encode())
    for key in keys:
        mp3 = unique[key]
        stat = mp3.stat()
        digest.update(f"|{key}|{mp3}|{stat.st_size}|{stat.st_mtime_ns}".encode())
    return digest.hexdigest()


def build_voice(root: Path, part_max: int = PART_MAX) -> tuple[dict[str, Path], dict]:
    """build/voice/voice.bin (the index) and voice-N.bin (the samples), remade only when the clips change.
    A clip's two versions stay together in one part; a part grows past part_max only for a single clip. Lines
    that share an mp3 (one take) share its samples."""
    csv_path = root / "build" / "tts" / "clips.csv"
    if not csv_path.exists():
        return {}, {"clips": 0, "skipped": [], "reason": "no build/tts/clips.csv"}
    clips, skipped = read_clips(csv_path)
    unique = {}
    for file, line, mp3 in clips:
        unique[(file, line)] = mp3                     # a later row for the same line wins
    keys = sorted(unique)
    out = root / "build" / "voice"
    manifest = _manifest(keys, unique, part_max)
    stamp = out / "manifest.txt"
    if stamp.exists() and stamp.read_text().split("\n", 1)[0] == manifest:
        parts = int(stamp.read_text().split("\n")[1])
        files = {"voice.bin": out / "voice.bin", **{f"voice-{n}.bin": out / f"voice-{n}.bin" for n in range(parts)}}
        return files, {"clips": len(keys), "skipped": skipped, "parts": parts, "reused": True,
                       "bytes": sum(path.stat().st_size for path in files.values())}
    cache = root / "build" / "cache" / "voice"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("voice-*.bin"):
        old.unlink()
    entries, part, number = [], bytearray(), 0

    def flush() -> None:
        nonlocal part, number
        _replace(out / f"voice-{number}.bin", bytes(part))
        part, number = bytearray(), number + 1

    stored = {}                                       # mp3 -> (part, offsets): a shared take is stored once
    for key in keys:
        if unique[key] in stored:
            entries.append(struct.pack("<HHHHIIII", *key, stored[unique[key]][0], 0, *stored[unique[key]][1]))
            continue
        normal, slow = convert(unique[key], False, cache), convert(unique[key], True, cache)
        need = 32 + len(normal) + 32 + len(slow)
        if part and len(part) + need > part_max:
            flush()
        offsets = []
        for pcm in (normal, slow):
            part += bytes(-len(part) % 32)
            offsets += [len(part), len(pcm) // 2]
            part += pcm
        stored[unique[key]] = (number, offsets)
        entries.append(struct.pack("<HHHHIIII", *key, number, 0, *offsets))
    if part or not keys:
        flush()
    index = struct.pack("<4sIIII", MAGIC, VERSION, len(keys), RATE, number) + b"".join(entries)
    _replace(out / "voice.bin", index)
    stamp.write_text(f"{manifest}\n{number}\n")
    files = {"voice.bin": out / "voice.bin", **{f"voice-{n}.bin": out / f"voice-{n}.bin" for n in range(number)}}
    return files, {"clips": len(keys), "skipped": skipped, "parts": number, "reused": False,
                   "bytes": sum(path.stat().st_size for path in files.values())}
