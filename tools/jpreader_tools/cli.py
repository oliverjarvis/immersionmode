"""Command line: jpreader build, jpreader e2e."""
from __future__ import annotations

import argparse
import os
from pathlib import Path


def find_root() -> Path:
    for candidate in (Path.cwd(), *Path.cwd().parents):
        if (candidate / "pyproject.toml").exists() and (candidate / "payload").is_dir():
            return candidate
    raise SystemExit("run jpreader from inside the project directory")


def resolve_rom(arg: str | None, root: Path) -> Path:
    if arg:
        return Path(arg)
    value = os.environ.get("JPREADER_ROM")
    env_file = root / ".env"
    if not value and env_file.exists():
        for line in env_file.read_text().splitlines():
            if line.startswith("JPREADER_ROM="):
                value = line.split("=", 1)[1].strip()
    if not value:
        raise SystemExit("no ROM given: pass a path or set JPREADER_ROM in .env")
    return Path(value)


def make_clips(root: Path, specs: list[str], dry: bool, pause: float | None = None, retime: bool = False,
               redirect: bool = False, rate: float | None = None) -> int:
    import json

    from . import tts

    rate = tts.CREDITS_PER_CHAR if rate is None else rate
    dump = root / "build" / "script"
    if not (dump / "story.jsonl").exists():
        raise SystemExit("no text dump: run `jpreader script` first")
    story = [json.loads(line) for line in open(dump / "story.jsonl", encoding="utf-8")]
    system = [json.loads(line) for line in open(dump / "system.jsonl", encoding="utf-8")]
    species = {row["en"] for row in system if row["file"] == tts.SPECIES_NAMES}
    directions = tts.load_directions(tts.DIRECTIONS)
    system_directions = tts.load_directions(tts.SYSTEM_DIRECTIONS)
    cast = tts.load_system(tts.SYSTEM)
    # every clip the scripts and tables plan, so the index covers clips made by earlier runs too
    planned = (tts.plan(story, {r["file"] for r in story}, species, directions)
               + tts.plan_system(system, {f for f, _ in cast}, cast, system_directions))
    story_files, system_files = tts.parse_specs(specs)
    clips = [c for c in planned if c.file in (story_files if c.set == "story" else system_files)]
    owners = {c.source for c in clips if c.source} - {c.name for c in clips}
    clips += [c for c in planned if c.name in owners]          # a take shared with a line of another file
    out = root / "build" / "tts" / "clips"
    todo = tts.pending(clips, out, retime, redirect)
    shared = sum(1 for c in clips if c.source)
    print(f"{len(clips)} clips{f' ({shared} share another line’s take)' if shared else ''}, {len(todo)} to make: "
          f"{sum(len(c.text) for c in todo)} characters, about {tts.credits(todo, rate)} credits on {tts.MODEL}"
          f"{f' at {rate} a character' if rate != tts.CREDITS_PER_CHAR else ''}")
    if dry:
        for clip in todo:
            print(f"  {clip.stem} {clip.speaker} -> {clip.voice}")
        return 0
    lines = {("story", r["file"], r["line"]): r for r in story} | {("system", r["file"], r["line"]): r for r in system}
    scenes = {("story", f): scene for f, scene in tts.load_scenes(tts.SCENES).items()}
    scenes |= {("system", f): who["scene"] for (f, _), who in cast.items() if who["scene"]}
    every_direction = {("story", *k): v for k, v in directions.items()}
    every_direction |= {("system", *k): v for k, v in system_directions.items()}
    if todo:
        key = tts.read_key(root)
        if not key:
            raise SystemExit("no ELEVENLABS_API_KEY in the environment or .env")
        api = tts.ElevenLabs(key)
        left = api.credits_left()
        if tts.credits(todo, rate) > left:
            raise SystemExit(f"only {left} credits left: make fewer files at a time")
        tts.generate(api, todo, out)
        print(f"done: {api.credits_left()} credits left")
    return write_clip_index(tts, planned, lines, scenes, every_direction, out, pause)


def write_clip_index(tts, planned, lines, scenes, directions, out: Path, pause: float | None = None) -> int:
    """clips.csv and pieces.csv list every clip on disk, whichever run made it: voice.bin is built from them."""
    made = [c for c in planned if (out / c.take).exists()]
    known = {c.name for c in made if not c.source}
    unplanned = sorted(p.name for p in out.glob("*.mp3") if p.name not in known)
    index = out.parent / "clips.csv"
    tts.write_index(made, lines, scenes, directions, index)
    print(f"{index}: {len(made)} clips")
    if unplanned:
        print(f"  left out {len(unplanned)} clip(s) no line plans any more: {', '.join(unplanned[:10])}")
    rows = tts.cut_pieces(made, out, out.parent / "pieces", tts.PAUSE if pause is None else pause)
    tts.write_pieces(rows, out.parent / "pieces.csv")
    print(f"{out.parent / 'pieces.csv'}: {len(rows)} pieces from "
          f"{len({(r['set'], r['story_file'], r['line']) for r in rows})} timed clips")
    return 0


def install_sd(root: Path, card: Path, debug_line: bool) -> int:
    from . import sdcard

    try:
        written = sdcard.install(root / "dist", card, debug_line)
    except sdcard.CardError as error:
        print(error)
        return 1
    for path in written:
        print(f"copied {path.relative_to(card)}")
    config = (card / "jpreader" / "config.txt").read_text()
    linked = any(line.startswith("kokoro_token=") and len(line) > len("kokoro_token=") for line in config.splitlines())
    print("config.txt: kokoro linked" if linked else "config.txt: kokoro not linked (run jpreader kokoro-link)")
    print("Turn on Luma's game patching (hold SELECT while powering on) if it is off.")
    return 0


def read_crash(root: Path, path: Path) -> int:
    from . import crash
    from .payload import parse_nm

    symbols_file = root / "build" / "payload-release" / "symbols.txt"
    symbols = parse_nm(symbols_file.read_text()) if symbols_file.exists() else {}
    try:
        dump = crash.parse(path.read_bytes())
    except crash.CrashError as error:
        print(error)
        return 1
    for line in crash.describe(dump, symbols):
        print(line)
    if not symbols:
        print("(no build/payload-release/symbols.txt: run `jpreader build` to name payload functions)")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="jpreader")
    commands = parser.add_subparsers(dest="command", required=True)

    build_cmd = commands.add_parser("build", help="build the patch from a ROM")
    build_cmd.add_argument("rom", nargs="?")
    build_cmd.add_argument("--debug", action="store_true", help="test build with button injection (emulator only)")

    e2e_cmd = commands.add_parser("e2e", help="end-to-end checks in the Azahar emulator")
    e2e_cmd.add_argument("stage", choices=["stage0", "stage1", "stage2", "stage3", "stage4", "stage9", "stage10", "stage11", "stage12", "stage13", "stage15", "stage17", "boot"])
    e2e_cmd.add_argument("rom", nargs="?")
    e2e_cmd.add_argument("--count", type=int, default=20, help="consecutive dialog lines to validate")
    e2e_cmd.add_argument("--soak", type=int, default=600, help="seconds to keep playing afterwards")
    e2e_cmd.add_argument("--manual", action="store_true", help="a person presses the buttons")
    e2e_cmd.add_argument("--kanji", action="store_true", help="play in kanji mode")
    e2e_cmd.add_argument("--messages", type=int, default=25, help="boot: lines to press through")

    drive_cmd = commands.add_parser("drive", help="press buttons in the running emulator and report state")
    drive_cmd.add_argument("keys", nargs="*", help="A, A*3, STICK_UP*4, TAP:x,y, WAIT:seconds")
    drive_cmd.add_argument("--shot", type=Path, help="save a screenshot of the emulator window here")

    sd_cmd = commands.add_parser("sd", help="copy the release build onto a mounted 3DS SD card")
    sd_cmd.add_argument("card", type=Path, help="the root of the SD card, e.g. /Volumes/3DS")
    sd_cmd.add_argument("--debug-line", action="store_true", help="turn on the debug line in config.txt")

    link_cmd = commands.add_parser("kokoro-link", help="make a kokoro device token for the 3DS and keep it in .env")
    link_cmd.add_argument("--email", required=True, help="the email you sign in to kokoro with")
    link_cmd.add_argument("--kokoro", type=Path, default=Path.home() / "Development" / "kokoro",
                          help="the kokoro checkout (default ~/Development/kokoro)")

    crash_cmd = commands.add_parser("crash", help="read a Luma3DS crash dump from the 3DS")
    crash_cmd.add_argument("dump", type=Path, help="/luma/dumps/arm11/crash_dump_*.dmp from the SD card")

    accuracy_cmd = commands.add_parser("accuracy", help="score the built entries against the hand-checked gold set")
    accuracy_cmd.add_argument("rom", nargs="?")
    annotate_cmd = commands.add_parser("annotate", help="annotate the script and report coverage")
    annotate_cmd.add_argument("rom", nargs="?")
    annotate_cmd.add_argument("--sample", type=int, default=50, help="story lines to write to the sample")
    annotate_cmd.add_argument("--seed", type=int, default=1)

    literal_cmd = commands.add_parser("literal", help="ask Claude for the peek's literal translations, into build/cache")
    literal_cmd.add_argument("rom", nargs="?")
    literal_cmd.add_argument("--budget", type=float, default=5.0, help="most US dollars to spend")

    script_cmd = commands.add_parser("script", help="dump every line of game text, with who says it, to build/script")
    script_cmd.add_argument("rom", nargs="?")

    tts_cmd = commands.add_parser("tts", help="make voice clips for story and system files, from build/script, "
                                  "into build/tts/clips")
    tts_cmd.add_argument("files", nargs="+", help="story text files, e.g. 27-65 70; system files cast in "
                         "tools/data/tts-system.tsv with sys, e.g. sys38")
    tts_cmd.add_argument("--dry", action="store_true", help="only list the clips and what they would cost")
    tts_cmd.add_argument("--pause", type=float, help="seconds a speaker must pause for a cut mid-sentence (default 0.2)")
    tts_cmd.add_argument("--retime", action="store_true", help="also remake takes that have no character timings")
    tts_cmd.add_argument("--rate", type=float, help="credits a character costs on this account, for the estimate "
                         "and the budget check (default 0.5, the model's list rate)")
    tts_cmd.add_argument("--redirect", action="store_true",
                         help="also remake takes whose directions or pauses changed (the words did not)")

    args = parser.parse_args(argv)
    root = find_root()
    if args.command == "tts":
        return make_clips(root, args.files, args.dry, args.pause, args.retime, args.redirect, args.rate)
    if args.command == "drive":
        from . import e2e

        return e2e.drive(root, args.keys, args.shot)
    if args.command == "sd":
        return install_sd(root, args.card, args.debug_line)
    if args.command == "crash":
        return read_crash(root, args.dump)
    if args.command == "kokoro-link":
        from . import kokoro

        try:
            kokoro.link(args.kokoro, args.email, root / ".env")
        except kokoro.LinkError as error:
            print(error)
            return 1
        print("Linked: kokoro's URL and a device token are saved in .env. Run `jpreader build`, then `jpreader sd`.")
        return 0
    rom = resolve_rom(args.rom, root)

    if args.command == "build":
        from . import build

        info = build.build(root, rom, args.debug)
        print(f"built: payload {info['payload_size']} bytes, stub {info['stub_size']} bytes, "
              f"font {info['font_bytes']} bytes{' (debug)' if args.debug else ''}")
        print("config.txt: kokoro linked" if info["kokoro_linked"] else
              "config.txt: kokoro not linked (run jpreader kokoro-link)")
        v = info["voice"]
        if v.get("reason"):
            print(f"voice: {v['reason']}; skipped")
        else:
            print(f"voice: {v['clips']} clips in {v['parts']} part(s), {len(v['skipped'])} skipped, "
                  f"{v['bytes'] / 1e6:.1f} MB{' (unchanged)' if v.get('reused') else ''}")
            for line in v["skipped"]:
                print(f"  skipped {line}")
        return 0

    if args.command == "accuracy":
        from . import accuracy, annotate
        from .rom import Rom

        with Rom(rom) as opened:
            sha = opened.code_sha256()
        annot, _dictionary, ids = annotate.cached_files(root, rom, sha)
        result = accuracy.score_rows(accuracy.read_gold(root / "tests" / "data" / "gold-entries.tsv"),
                                     accuracy.chosen_from(annot, ids))
        print(f"overall {result['overall']:.1%} of {result['count']}; ambiguous {result['ambiguous']:.1%} of "
              f"{result['ambiguous_count']}; {result['wrong_count']} wrong")
        for row, got in result["wrong"]:
            print(f"  {row.archive}/{row.file}/{row.line}/{row.unit} {row.surface} ({row.lemma}): "
                  f"gold {row.jmdict_id}, built {got}  {row.note}")
        return 0

    if args.command == "literal":
        from . import claude, literal
        from .rom import Rom
        from .text import load_archive

        with Rom(rom) as opened:
            asks = literal.asks_from(*(load_archive(opened, arc) for arc in (30, 31, 32)))
        cache = literal.Literals(literal.cache_path(root))
        summary = literal.run(asks, cache, claude.read_key(root), args.budget, root / "build" / "cache", print)
        print(f"literal: {summary['asked']} strings, {summary['cached']} cached, {summary['answered']} answered, "
              f"estimate ${summary['cost_estimate_usd']:.2f}")
        return 0

    if args.command == "annotate":
        from . import annotate
        from .rom import Rom

        annot, dictionary, result = annotate.build_files(root, rom)
        out = root / "build" / "annot"
        out.mkdir(parents=True, exist_ok=True)
        (out / "annot.bin").write_bytes(annot)
        (out / "dict.bin").write_bytes(dictionary)
        table = annotate.report(result)
        with Rom(rom) as opened:
            lines = annotate.sample(opened, result, args.sample, args.seed)
        (root / "docs" / "stage3a-sample.md").write_text(
            "# Stage 3A: coverage and sample\n\n" + table + "\n\n## Sample\n\n" + lines + "\n")
        print(table)
        print(f"annot.bin {len(annot)} bytes, dict.bin {len(dictionary)} bytes; sample in docs/stage3a-sample.md")
        return 0

    if args.command == "script":
        from collections import Counter

        from . import textdump
        from .rom import Rom

        with Rom(rom) as opened:
            sets = textdump.rows(opened)
        textdump.apply_guesses(sets["story"], textdump.load_guesses())
        textdump.write(root / "build" / "script", sets)
        story = sets["story"]
        kinds = Counter(r["kind"] for r in story)
        print(f"build/script: {len(sets['system'])} system lines, {len(story)} story lines: "
              + ", ".join(f"{n} {k}" for k, n in kinds.most_common()))
        return 0

    from . import e2e

    if args.stage == "stage0":
        return e2e.stage0(root, rom)
    if args.stage == "stage1":
        return e2e.stage1(root, rom, args.count, args.soak, args.manual)
    if args.stage == "boot":
        return e2e.boot(root, rom, args.messages, args.kanji)
    if args.stage == "stage4":
        return e2e.stage4(root, rom, args.manual)
    if args.stage == "stage10":
        return e2e.stage10(root, rom, args.manual)
    if args.stage == "stage11":
        return e2e.stage11(root, rom, args.manual)
    if args.stage == "stage12":
        return e2e.stage12(root, rom, args.manual)
    if args.stage == "stage13":
        return e2e.stage13(root, rom, args.manual)
    if args.stage == "stage15":
        return e2e.stage15(root, rom, args.manual)
    if args.stage == "stage9":
        return e2e.stage9(root, rom, args.manual)
    if args.stage == "stage17":
        return e2e.stage17(root, rom, args.manual)
    if args.stage == "stage3":
        return e2e.stage3(root, rom, args.kanji, args.count, args.manual)
    return e2e.stage2(root, rom, args.kanji, args.count, args.manual)
