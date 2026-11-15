"""Voice clips for story and system lines, from ElevenLabs text to speech.

Reads the text dump (`jpreader script`), casts a voice for each speaker, adds the acting
directions in tools/data/tts-directions.tsv, and writes one mp3 per line to build/tts/clips,
named FFFF-LLL.mp3 after the story text file and line the payload already identifies, with the
time of every character beside it (FFFF-LLL.json). System text has no speakers in the dump:
tools/data/tts-system.tsv chooses which system lines are spoken and by whom, with their directions
in tools/data/tts-system-directions.tsv; their clips are SFFFF-LLL.mp3, since system and story
file numbers overlap.

A line is spoken in one take so its sentences flow across text boxes: a box that ends a sentence
is followed by a line break (a pause), one that ends mid-sentence runs straight on. Each take is
then cut into pieces at box breaks (build/tts/pieces/FFFF-LLL-B.mp3, B = the first box it covers),
in the middle of a real silence there: where a box ends a sentence, and mid-sentence only where the
speaker took a breath anyway. The character timings say where each box starts; they can't say
whether the speaker paused (a pause is folded into the characters around it), so the silences come
from the audio itself. Cutting is local: it can be redone with another pause length for free.
"""
from __future__ import annotations

import csv
import dataclasses
import json
import base64
import os
import re
import subprocess
import urllib.request
from dataclasses import dataclass
from pathlib import Path

API = "https://api.elevenlabs.io/v1/"
MODEL = "eleven_v4_turbo"
CREDITS_PER_CHAR = 0.5                # eleven_v4_turbo's character cost multiplier (GET /v1/models); a plan may
                                      # bill less: `jpreader tts --rate` uses the rate measured on the account
OUTPUT = "mp3_44100_128"
DATA = Path(__file__).resolve().parents[1] / "data"
DIRECTIONS = DATA / "tts-directions.tsv"
SCENES = DATA / "tts-scenes.tsv"          # story file -> where in the game its lines play
SYSTEM = DATA / "tts-system.tsv"          # the system lines that are spoken: file, lines, speaker, gender, age, scene
SYSTEM_DIRECTIONS = DATA / "tts-system-directions.tsv"
# story_file is the text file in either set (the name predates system clips)
INDEX_FIELDS = ("clip", "set", "story_file", "line", "scene", "speaker", "voice", "english", "japanese", "directions")
PIECE_FIELDS = ("piece", "set", "story_file", "line", "box", "boxes", "start", "end", "japanese")
SENTENCE_ENDS = set("。！？!?…」』）♪")
MIN_SILENCE = 0.15                    # seconds of silence a cut at a sentence end needs
PAUSE = 0.2                           # seconds the speaker must pause for a cut mid-sentence: in the opening,
                                      # a third of mid-sentence breaks have a 0.2-0.7 s breath, the rest < 0.15 s
QUIET = "silencedetect=noise=-35dB:d=0.05"
FFMPEG = "/opt/homebrew/bin/ffmpeg"
SPECIES_NAMES = 60                    # system text file of species names: their lines are cries

# Text the scripts fill in while playing: spoken as the defaults until the game can say them
PLAYER = "ヨウ"                        # the default Japanese name of the male player; also Rotom's name for
                                      # the player ({0189:...}, the player's name until a nickname is taken) and
                                      # the first two characters of the name ({018A:...}, the nickname's stem)
PARTNER = "モクロー"                    # a Pokémon's name ({0101:...})

# Speaker -> ElevenLabs premade voice (by first name). Free accounts can only use premade voices.
# There are 21 premade voices, so some characters share one, but never two who speak in the same text file (checked
# over the whole story), or a character and the crowd (Kiawe, Sophocles, Nanu).
CAST = {
    "Mom": "Alice", "Professor Kukui": "George", "Hau": "Liam", "Lillie": "Sarah", "Hala": "Bill",
    "Dulse": "Daniel", "Zossie": "Jessica", "Ilima": "Eric", "Soliera": "Bella", "Phyco": "Adam",
    "Rotom Pokédex": "Charlie", "Masked Royal": "George",       # the Masked Royal is Kukui
    "Gladion": "Harry", "Guzma": "Roger", "Kiawe": "Will", "Sophocles": "Chris", "Molayne": "Daniel", "Nanu": "Brian",
    "Mallow": "Jessica", "Lana": "Bella", "Acerola": "Laura", "Mina": "Laura", "Plumeria": "Matilda", "Olivia": "Matilda",
    "Hapu": "Lily", "Kahili": "Bella", "Colress": "Eric",
    "Lusamine": "Alice", "Wicke": "Lily", "Professor Burnet": "Laura", "Faba": "Adam", "Looker": "Daniel",
    "Grimsley": "Eric",
    "Giovanni": "Brian", "Maxie": "Eric", "Archie": "Harry", "Cyrus": "Daniel", "Lysandre": "Adam", "Ghetsis": "Bill",
    "Team Rainbow Rocket Executive": "Charlie", "Ryuki": "Callum",
}
# Speakers whose label covers both genders: only one of them is cast
CAST_BY_GENDER = {("Team Skull Grunt", "male"): "Callum", ("Team Rainbow Rocket", "male"): "Callum",
                  ("Team Rainbow Rocket Grunt Leader", "male"): "Roger",      # not the grunts' voices beside him
                  ("Team Rainbow Rocket Grunt Leader", "female"): "Lily"}
CRIES = ("player", "Pokémon", "Nebby", "Ultra Beast")    # speakers never voiced, besides species names and Pokémon
# Everyone else, by gender and age band. Boys are never Liam: that is Hau.
CROWD = {
    ("male", "child"): "Will", ("male", "teen"): "Will", ("male", "young adult"): "Will",
    ("male", "adult"): "Chris", ("male", "older adult"): "Brian", ("male", "elderly"): "Bill",
    ("female", "child"): "Jessica", ("female", "teen"): "Jessica", ("female", "young adult"): "Laura",
    ("female", "adult"): "Matilda", ("female", "older adult"): "Lily", ("female", "elderly"): "Lily",
}
CROWD_BY_GENDER = {"male": "Chris", "female": "Matilda"}
# System lines (trainers above all) without a CAST voice: girls must not sound like Mallow and Zossie (Jessica).
# Story lines keep CROWD, so no clip already made changes voice.
SYSTEM_CROWD = {**CROWD, ("female", "child"): "Laura", ("female", "teen"): "Laura"}
NEUTRAL = "River"


@dataclass(frozen=True)
class Clip:
    file: int
    line: int
    speaker: str
    voice: str
    text: str                         # what is sent: directions and the cleaned Japanese
    spans: tuple = ()                 # each box's (start, end) in text
    ends: tuple = ()                  # whether each box ends a sentence
    set: str = "story"                # or "system": the text archive the line is in
    source: str = ""                  # another line's mp3 this line plays: the same voice and text (one take)

    @property
    def take(self) -> str:
        """The mp3 this line plays."""
        return self.source or self.name

    @property
    def stem(self) -> str:
        return f"{'S' if self.set == 'system' else ''}{self.file:04d}-{self.line:03d}"

    @property
    def name(self) -> str:
        return self.stem + ".mp3"

    @property
    def timings(self) -> str:
        return self.stem + ".json"


def boxes(text: str, player: str = PLAYER, partner: str = PARTNER) -> list[str]:
    """A line's text boxes, as plain Japanese: tags dropped, names filled in, no wrapping or spacing."""
    text = text.replace("{0100:0000}", player)
    text = re.sub(r"\{01(89|8A):[^}]*\}", player, text)
    text = re.sub(r"\{0101:[^}]*\}", partner, text)
    text = re.sub(r"\{[rc]\}", "\0", text)
    text = re.sub(r"\{[^}]*\}", "", text)
    out = [b.replace("　", "").replace("\n", "").strip() for b in text.split("\0")]
    return [b for b in out if b]


# Tags that are not words: box breaks, the speaker id, colour, waits and sounds, empty slots, English-only grammar
# forms; and the names spoken as defaults (the player {0100:0000}, a Pokémon {0101}, Rotom's names {0189} {018A}).
SAID = re.compile(r"\{(?:[rc]|0100:0000|01(?:01|89|8A):[^}]*|0114:[^}]*|FF00:[^}]*|B[DE][0-9A-F]{2}(?::[^}]*)?"
                  r"|1[0-9A-F]{3}(?::[^}]*)?)\}")


def has_variable(text: str) -> bool:
    """Whether the game fills in words here as it plays (an item, a number, a move, someone else's name): a take
    would leave them out."""
    return bool(re.search(r"\{[^}]*\}", SAID.sub("", text)))


def ends_sentence(box: str) -> bool:
    return box[-1:] in SENTENCE_ENDS or box.rstrip("ー〜")[-1:] in SENTENCE_ENDS


def script(parts: list[str], directions: list[str]) -> tuple[str, list[tuple[int, int]]]:
    """What to send for a line's boxes, and where each box is in it.

    After a box that ends a sentence comes a line break (a pause); after one that doesn't, the next
    box runs straight on. A direction is kept only where a sentence starts: mid-sentence it would
    make the speaker pause."""
    padded = directions + [""] * (len(parts) - len(directions))
    text, spans = "", []
    for k, (box, direction) in enumerate(zip(parts, padded)):
        starts_sentence = k == 0 or ends_sentence(parts[k - 1])
        if k and starts_sentence:
            text += "\n"
        if direction and starts_sentence:
            text += direction + " "
        spans.append((len(text), len(text) + len(box)))
        text += box
    return text, spans


def load_directions(path: Path = DIRECTIONS) -> dict[tuple[int, int], list[str]]:
    if not path.exists():
        return {}
    with open(path, newline="", encoding="utf-8") as fh:
        return {(int(r["file"]), int(r["line"])): [d.strip() for d in (r["directions"] or "").split("|")]
                for r in csv.DictReader(fh, delimiter="\t")}


def voice_for(row: dict, crowd: dict = CROWD) -> str:
    if row["speaker"] in CAST:
        return CAST[row["speaker"]]
    if (row["speaker"], row["gender"]) in CAST_BY_GENDER:
        return CAST_BY_GENDER[(row["speaker"], row["gender"])]
    return crowd.get((row["gender"], row["age"])) or CROWD_BY_GENDER.get(row["gender"], NEUTRAL)


def _clip(row: dict, cast: dict, directions: dict[tuple[int, int], list[str]], kind: str) -> Clip | None:
    """row's text, spoken by cast's speaker (speaker, gender and age); None when it has nothing to say, or words
    the game fills in."""
    parts = boxes(row["ja_kanji"])
    if not parts or has_variable(row["ja_kanji"]):
        return None
    text, spans = script(parts, directions.get((row["file"], row["line"]), []))
    voice = voice_for(cast, SYSTEM_CROWD if kind == "system" else CROWD)
    return Clip(row["file"], row["line"], cast["speaker"], voice, text, tuple(spans),
                tuple(ends_sentence(p) for p in parts), kind)


def plan(story: list[dict], files: set[int], species: set[str],
         directions: dict[tuple[int, int], list[str]]) -> list[Clip]:
    """A clip for every spoken dialogue line in `files`; cries and the player's own lines are left out. A Pokémon
    that talks (the Rotom Pokédex) is voiced when it is in CAST."""
    clips = []
    for row in story:
        if row["file"] not in files or row["kind"] != "dialogue":
            continue
        cry = row["speaker_type"] == "pokemon" and row["speaker"] not in CAST
        if cry or row["speaker"] in species or row["speaker"] in CRIES:
            continue
        clip = _clip(row, row, directions, "story")
        if clip:
            clips.append(clip)
    return clips


def line_ranges(spec: str) -> set[int]:
    """'1-13,17' -> {1, ..., 13, 17}"""
    out = set()
    for part in spec.split(","):
        low, _, high = part.strip().partition("-")
        out.update(range(int(low), int(high or low) + 1))
    return out


def load_system(path: Path = SYSTEM) -> dict[tuple[int, int], dict]:
    """tts-system.tsv: (system file, line) -> who says it (speaker, gender, age) and the scene. Lines starting
    with # are comments."""
    if not path.exists():
        return {}
    fields = ("file", "lines", "speaker", "gender", "age", "scene")
    cast = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        row = dict(zip(fields, [v.strip() for v in raw.split("\t")]))
        if row["file"] == "file":                              # the header
            continue
        for line in line_ranges(row["lines"]):
            cast[(int(row["file"]), line)] = {"speaker": row["speaker"], "gender": row.get("gender", ""),
                                              "age": row.get("age", ""), "scene": row.get("scene", "")}
    return cast


def plan_system(system: list[dict], files: set[int], cast: dict[tuple[int, int], dict],
                directions: dict[tuple[int, int], list[str]]) -> list[Clip]:
    """A clip for every system line in `files` that tts-system.tsv casts. A line with the same voice and text as an
    earlier one (Rotom's affection variants) shares its take."""
    clips, first = [], {}
    for row in system:
        who = cast.get((row["file"], row["line"]))
        if row["file"] not in files or not who:
            continue
        clip = _clip(row, who, directions, "system")
        if not clip:
            continue
        owner = first.setdefault((clip.voice, clip.text), clip)
        clips.append(clip if owner is clip else dataclasses.replace(clip, source=owner.name))
    return clips


def read_key(root: Path) -> str | None:
    """ELEVENLABS_API_KEY from the environment or .env; never printed."""
    if os.environ.get("ELEVENLABS_API_KEY"):
        return os.environ["ELEVENLABS_API_KEY"]
    env = root / ".env"
    if env.exists():
        for line in env.read_text().splitlines():
            if line.startswith("ELEVENLABS_API_KEY="):
                return line.split("=", 1)[1].strip() or None
    return None


class ElevenLabs:
    def __init__(self, key: str):
        self.key = key

    def _call(self, path: str, body: dict | None = None):
        request = urllib.request.Request(API + path, data=json.dumps(body).encode() if body else None,
                                         headers={"xi-api-key": self.key, "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()

    def credits_left(self) -> int:
        sub = json.loads(self._call("user/subscription"))
        return sub["character_limit"] - sub["character_count"]

    def voices(self) -> dict[str, str]:
        """Voice first name -> voice id."""
        return {v["name"].split(" - ")[0].strip(): v["voice_id"] for v in json.loads(self._call("voices"))["voices"]}

    def speak(self, voice_id: str, text: str) -> tuple[bytes, dict]:
        """The take, and when each character of text starts and ends in it."""
        reply = json.loads(self._call(f"text-to-speech/{voice_id}/with-timestamps?output_format={OUTPUT}",
                                      {"text": text, "model_id": MODEL, "language_code": "ja"}))
        return base64.b64decode(reply["audio_base64"]), reply["alignment"]


def credits(clips: list[Clip], rate: float = CREDITS_PER_CHAR) -> int:
    return round(sum(len(c.text) for c in clips) * rate)


def words(text: str) -> str:
    """What is spoken of a request: its directions and pauses taken out."""
    return re.sub(r"\[[^\]]*\]|\s", "", text)


def pending(clips: list[Clip], out_dir: Path, retime: bool = False, redirect: bool = False) -> list[Clip]:
    """Clips with no mp3 yet, or whose voice or words changed since it was made. A take whose request differs only
    in directions or pauses (older takes paused and took a direction at every box) is kept unless redirect is
    asked for; takes made before the timings were kept have none, and are made again only with retime. Either
    costs a whole new take. A line that shares another's take is never made."""
    manifest = _manifest(out_dir)

    def stale(c: Clip) -> bool:
        made = manifest.get(c.name)
        if not made or made["voice"] != c.voice or made["model"] != MODEL:
            return True
        return made["text"] != c.text if redirect else words(made["text"]) != words(c.text)

    return [c for c in clips if not c.source and (
            not (out_dir / c.name).exists() or (retime and not (out_dir / c.timings).exists()) or stale(c))]


def generate(api: ElevenLabs, clips: list[Clip], out_dir: Path, report=print) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    ids = api.voices()
    manifest = _manifest(out_dir)
    for clip in clips:
        audio, alignment = api.speak(ids[clip.voice], clip.text)
        _write(out_dir / clip.timings, json.dumps({"text": clip.text, "spans": clip.spans, "ends": clip.ends,
                                                   "alignment": alignment}, ensure_ascii=False).encode())
        _write(out_dir / clip.name, audio)
        manifest[clip.name] = {"voice": clip.voice, "text": clip.text, "model": MODEL}
        (out_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1))
        report(f"{clip.stem} {clip.speaker} ({clip.voice})")


def _write(path: Path, data: bytes) -> None:
    """Whole or not at all: a full disk once left a clip cut short."""
    partial = path.with_name(path.name + ".part")
    partial.write_bytes(data)
    partial.replace(path)


def silences(take: Path) -> list[tuple[float, float]]:
    """The quiet stretches in a take, (start, end) in seconds."""
    log = subprocess.run([FFMPEG, "-hide_banner", "-i", str(take), "-af", QUIET, "-f", "null", "-"],
                         capture_output=True, text=True, check=True).stderr
    starts = [float(t) for t in re.findall(r"silence_start: (-?[\d.]+)", log)]
    ends = [float(t) for t in re.findall(r"silence_end: ([\d.]+)", log)]
    return list(zip(starts, ends))


def breaks(timing: dict, quiet: list[tuple[float, float]]) -> list[tuple[int, bool, float, float]]:
    """Each box break in a take: (the box after it, whether a sentence ended there, the longest
    silence around it in seconds, the middle of that silence). Around it means from the start of the
    box's last character to the end of the next box's first. Empty when the timings don't match the
    text sent."""
    alignment = timing["alignment"]
    if "".join(alignment["characters"]) != timing["text"]:
        return []
    starts, ends = alignment["character_start_times_seconds"], alignment["character_end_times_seconds"]
    out = []
    for k in range(1, len(timing["spans"])):
        low, high = starts[timing["spans"][k - 1][1] - 1], ends[timing["spans"][k][0]]
        overlaps = [(min(end, high) - max(start, low), (max(start, low) + min(end, high)) / 2)
                    for start, end in quiet if end > low and start < high]
        length, middle = max(overlaps, default=(0.0, 0.0))
        out.append((k, timing["ends"][k - 1], length, middle))
    return out


def cuts(timing: dict, quiet: list[tuple[float, float]], pause: float = PAUSE) -> list[tuple[int, float]]:
    """Where a take is cut: (the first box of the next piece, the time), in the middle of a silence.
    At a sentence end with any real silence; mid-sentence only where the speaker paused anyway."""
    return [(box, middle) for box, sentence, length, middle in breaks(timing, quiet)
            if length >= (MIN_SILENCE if sentence else pause)]


def cut_pieces(clips: list[Clip], clips_dir: Path, pieces_dir: Path, pause: float = PAUSE) -> list[dict]:
    """Cuts each take with timings into pieces; returns pieces.csv's rows. Pieces whose cut changed
    are remade; pieces a line no longer has are removed. A line sharing another's take lists that take's pieces."""
    pieces_dir.mkdir(parents=True, exist_ok=True)
    made = _manifest(pieces_dir)
    rows = []
    for clip in clips:
        if clip.source:
            continue
        take, timing_path = clips_dir / clip.name, clips_dir / clip.timings
        if not take.exists() or not timing_path.exists():
            continue
        timing = json.loads(timing_path.read_text())
        edges = [(0, 0.0)] + cuts(timing, silences(take), pause)
        stem = clip.stem
        keep = set()
        for i, (box, start) in enumerate(edges):
            end = edges[i + 1][1] if i + 1 < len(edges) else None
            last_box = edges[i + 1][0] if i + 1 < len(edges) else len(timing["spans"])
            name = f"{stem}-{box}.mp3"
            keep.add(name)
            source = {"take": clip.text, "start": round(start, 3), "end": None if end is None else round(end, 3)}
            if made.get(name) != source or not (pieces_dir / name).exists():
                command = [FFMPEG, "-v", "error", "-y", "-i", str(take), "-ss", f"{start:.3f}"]
                command += ["-to", f"{end:.3f}"] if end is not None else []
                # through a filter: cutting straight into libmp3lame failed on some takes ("inadequate AVFrame
                # plane padding", 402:7); a filter hands the encoder freshly allocated frames
                subprocess.run(command + ["-af", "aresample=44100", "-b:a", "128k", str(pieces_dir / (name + ".part.mp3"))],
                               check=True)
                (pieces_dir / (name + ".part.mp3")).replace(pieces_dir / name)
                made[name] = source
            spoken = "".join(timing["text"][a:b] for a, b in timing["spans"][box:last_box])
            rows.append({"piece": f"pieces/{name}", "set": clip.set, "story_file": clip.file, "line": clip.line, "box": box,
                         "boxes": last_box - box, "start": f"{start:.3f}", "end": "" if end is None else f"{end:.3f}",
                         "japanese": spoken})
        for old in pieces_dir.glob(f"{stem}-*.mp3"):
            if old.name not in keep:
                old.unlink()
                made.pop(old.name, None)
    (pieces_dir / "manifest.json").write_text(json.dumps(made, ensure_ascii=False, indent=1))
    by_take = {}
    for row in rows:
        by_take.setdefault(row["piece"].split("/")[1].rsplit("-", 1)[0] + ".mp3", []).append(row)
    for clip in clips:
        if clip.source:
            rows += [{**row, "set": clip.set, "story_file": clip.file, "line": clip.line}
                     for row in by_take.get(clip.source, [])]
    return rows


def write_pieces(rows: list[dict], path: Path) -> None:
    with open(path, "w", newline="", encoding="utf-8-sig") as fh:
        w = csv.DictWriter(fh, fieldnames=PIECE_FIELDS)
        w.writeheader()
        w.writerows(rows)


def _manifest(out_dir: Path) -> dict:
    path = out_dir / "manifest.json"
    return json.loads(path.read_text()) if path.exists() else {}


def load_scenes(path: Path = SCENES) -> dict[int, str]:
    if not path.exists():
        return {}
    with open(path, newline="", encoding="utf-8") as fh:
        return {int(r["file"]): r["scene"] for r in csv.DictReader(fh, delimiter="\t")}


def write_index(clips: list[Clip], lines: dict[tuple[str, int, int], dict], scenes: dict[tuple[str, int], str],
                directions: dict[tuple[str, int, int], list[str]], path: Path) -> None:
    """clips.csv: each clip beside the line it voices, where that plays, and what was sent. lines, scenes and
    directions are keyed by set ("story" or "system") first."""
    with open(path, "w", newline="", encoding="utf-8-sig") as fh:          # the BOM lets Excel read the Japanese
        w = csv.DictWriter(fh, fieldnames=INDEX_FIELDS)
        w.writeheader()
        for c in clips:
            row = lines[(c.set, c.file, c.line)]
            w.writerow({"clip": f"clips/{c.take}", "set": c.set, "story_file": c.file, "line": c.line,
                        "scene": scenes.get((c.set, c.file), ""), "speaker": c.speaker, "voice": c.voice,
                        "english": " ".join(re.sub(r"\{[^}]*\}", " ", row["en"]).split()),
                        "japanese": " / ".join(boxes(row["ja_kanji"])),
                        "directions": " | ".join(directions.get((c.set, c.file, c.line), []))})


def file_ranges(specs: list[str]) -> set[int]:
    """'27-65' and '70' -> story file numbers."""
    files = set()
    for spec in specs:
        files |= line_ranges(spec)
    return files


def parse_specs(specs: list[str]) -> tuple[set[int], set[int]]:
    """'27-65', '70' and 'sys38' -> (story files, system files)."""
    story = [s for s in specs if not s.lower().startswith("sys")]
    system = [s[3:] for s in specs if s.lower().startswith("sys")]
    return file_ranges(story), file_ranges(system)
