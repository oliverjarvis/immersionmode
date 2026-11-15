"""End-to-end checks that run the game in Azahar."""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass
import subprocess
import time
from pathlib import Path

from . import capture, emu, formats
from .layout import CODE_BASE, LANG_BYTE, PANEL_IMAGE_ADDR, SOCK_BUF_ADDR, TITLE_ID
from .rom import Rom
from .text import TAG_CLEAR, TAG_MARK, render, split_blocks


def install(profile: Path, mod: Path | None, sound: str | None = None) -> None:
    """The patch folder into the emulator profile. The dist's config.txt names the real kokoro (build()
    copies it from .env), so a run starts unlinked; stages that need kokoro write the stand-in's URL. A run
    starts with the mixer's defaults unless it brings its own sound.txt."""
    emu.remove_mod(profile)
    if mod is not None:
        emu.install_mod(mod, profile)
        sdmc = emu.profile_dirs(profile)["data"] / "sdmc"
        write_config(sdmc)
        (sdmc / "jpreader" / "sound.txt").unlink(missing_ok=True)
        if sound is not None:
            (sdmc / "jpreader" / "sound.txt").write_text(sound)


def start(root: Path, rom_path: Path, mod: Path | None, sound: str | None = None) -> emu.Rpc:
    """Launch the game in a clean emulator profile, optionally with a patch folder installed."""
    app = emu.install(root / "build" / "emu")
    profile = root / "build" / "emu" / "profile"
    emu.seed_config(emu.profile_dirs(profile)["config"] / "qt-config.ini")
    install(profile, mod, sound)
    local_rom = emu.prepare_rom(rom_path, root / "build" / "rom" / "ultramoon.cci")
    emu.stop()
    emu.launch(app, profile, local_rom)
    rpc = emu.Rpc()
    emu.wait_for_rpc(rpc, 90)
    deadline = time.monotonic() + 60
    while True:
        try:
            rpc.select_title(TITLE_ID)
            return rpc
        except emu.EmuNotResponding:
            if time.monotonic() > deadline:
                raise
            time.sleep(1)


def stage0(root: Path, rom_path: Path) -> int:
    """The unmodified game boots and its memory can be read."""
    with Rom(rom_path) as rom:
        rom.verify()
        code = rom.exefs_file(".code")
    rpc = start(root, rom_path, None)
    try:
        seen = rpc.read(CODE_BASE, 16)
        if seen != code[:16]:
            print(f"stage 0 FAILED: memory at {CODE_BASE:#x} is {seen.hex()}, expected {code[:16].hex()}")
            return 1
        time.sleep(30)
        if not emu.is_running():
            print("stage 0 FAILED: the emulator exited within 30 seconds of booting")
            return 1
        language = rpc.read(LANG_BYTE, 1)[0]
        print(f"stage 0 OK: game code readable, emulator alive after 30 s, language byte = {language}")
        return 0
    finally:
        emu.stop()


BUTTONS = {
    "LEFT": 0x1, "RIGHT": 0x2, "UP": 0x4, "DOWN": 0x8, "A": 0x10, "B": 0x20,
    "X": 0x40, "Y": 0x80, "L": 0x100, "R": 0x200, "START": 0x1000,
    # Circle pad: the debug build turns these into a full push in one direction.
    "STICK_LEFT": 0x10000, "STICK_RIGHT": 0x20000, "STICK_UP": 0x40000, "STICK_DOWN": 0x80000,
}
PRESS_CYCLE = ["A"]                     # name entry is handled by enter_name
JAPANESE = (0, 1)                       # kana, kanji


# After Kukui asks for your name, the keyboard opens on あ; type いきし and confirm.
NAME_KEYS = ["DOWN", "A", "RIGHT", "A", "RIGHT", "A", "START", "A"]
# The first screen lists languages with にほんご selected; choosing it opens a second menu with
# ひらがな selected and 漢字 below it. The driver's own A presses then confirm 漢字.
LANGUAGE_SCREEN_WAIT = 8.0
KANJI_KEYS = ["A", "DOWN"]


def is_name_prompt(units) -> bool:
    shown = "".join(map(chr, units))
    return ("なまえ" in shown and "とうろく" in shown) or ("名前" in shown and "登録" in shown)


def enter_name(session: "Session", wait: float = 2.0) -> None:
    """Get past the name screen, which presses of A alone cannot (repeated あ is refused)."""
    time.sleep(wait)
    session.press("A", settle=0.35 if wait else 0)       # close Kukui's message
    time.sleep(wait * 1.25)                               # the keyboard slides in
    for key in NAME_KEYS:
        session.press(key, settle=0.4 if wait else 0)


class Session:
    """The payload's memory blocks, read and written through the emulator."""

    def __init__(self, rpc, symbols: dict[str, int]):
        self.rpc = rpc
        self.cap = symbols["jpr_cap"]
        self.dbg = symbols["jpr_dbg"]
        self.panel_addr = symbols.get("jpr_panel")
        self.shown_addr = symbols.get("jpr_shown_text")
        self.rows_addr = symbols.get("jpr_peek_shown")

    def debug(self) -> capture.DebugBlock:
        return capture.parse_debug(self.rpc.read(self.dbg, capture.DBG_SIZE))

    def header(self) -> capture.CaptureHeader:
        return capture.parse_header(self.rpc.read(self.cap, capture.HEADER_SIZE))

    def message(self, seq: int) -> capture.Message | None:
        message = capture.parse_message(self.rpc.read(self.cap + capture.slot_offset(seq), capture.MSG_SIZE))
        return message if message.seq == seq else None

    def language(self) -> int:
        return self.rpc.read(LANG_BYTE, 1)[0]

    def voice_test(self, line: int) -> None:
        """Debug builds: from now on every message stands in for story file 27, line `line` (0: off)."""
        self.rpc.write(self.dbg + capture.DBG_VOICE_TEST, struct.pack("<I", line + 1 if line >= 0 else 0))

    def select(self) -> None:
        """Debug builds: the next button poll counts as a SELECT press (the emulator's real buttons are not ours)."""
        self.rpc.write(self.dbg + capture.DBG_PEEK_TOGGLE, struct.pack("<I", 1))
        wait_for(lambda: self.debug().peek_toggle == 0, 2)
        time.sleep(0.2)

    def mix_test(self, which: int) -> None:
        """Debug builds: the game calls Sound::ChangeBGMPlayerVolume, 1 = 0.5 at once, 2 = a fade to 1.0."""
        self.rpc.write(self.dbg + capture.DBG_MIX_TEST, struct.pack("<I", which))

    def press(self, button: str, polls: int = 6, settle: float = 0.35) -> None:
        self.rpc.write(self.dbg + capture.DBG_PAD_MASK, struct.pack("<II", BUTTONS[button], polls))
        deadline = time.monotonic() + 3
        while settle and time.monotonic() < deadline:
            if struct.unpack("<I", self.rpc.read(self.dbg + capture.DBG_PAD_POLLS, 4))[0] == 0:
                break
            time.sleep(0.02)
        time.sleep(settle)

    def shown_text(self) -> str:
        """The text the panel was given for the peek or a choice menu (stage 14)."""
        units = struct.unpack(f"<{capture.MSG_MAX}H", self.rpc.read(self.shown_addr, 2 * capture.MSG_MAX))
        end = units.index(0) if 0 in units else len(units)
        return "".join(map(chr, units[:end]))

    def peek_rows(self) -> list[dict]:
        """The peek's rows (stage 15, core/screen.h struct jpr_peek_rows): each row's Japanese and its strings."""
        rows, size = 24, 12 + 24 * 20 + 2048
        blob = self.rpc.read(self.rows_addr, size)
        n, header, _used = struct.unpack_from("<III", blob)
        text, pool = self.shown_text(), blob[12 + rows * 20:]
        out = []
        for k in range(min(n, rows)):
            start, end, *fields = struct.unpack_from("<10H", blob, 12 + 20 * k)
            row = {"japanese": text[start:end], "header": bool(header and k == 0)}
            for name, (at, length) in zip(("literal", "english", "meta", "pill"), zip(fields[::2], fields[1::2])):
                row[name] = pool[at:at + length].decode("utf-8", "replace")
            out.append(row)
        return out

    def panel(self) -> capture.PanelState:
        if self.panel_addr is None:
            raise RuntimeError("this build has no panel")
        return capture.parse_panel(self.rpc.read(self.panel_addr, capture.PANEL_SIZE))

    def fault(self, kind: int, arg: int = 0) -> None:
        """Inject a fault into the next grammar requests (debug builds; 0 clears it)."""
        self.rpc.write(self.dbg + capture.DBG_FAULT, struct.pack("<II", kind, arg))

    def forget_reply(self) -> None:
        """Make the panel forget its stored reply, as a new conversation does (debug builds)."""
        self.rpc.write(self.dbg + capture.DBG_FORGET_REPLY, struct.pack("<I", 1))

    def link_result(self) -> tuple[int, int, int, int, str]:
        """The worker's last result: id, failure, code, HTTP status, text."""
        return capture.parse_link_result(self.rpc.read(self.debug().link_result, capture.LINK_RESULT_SIZE))

    def tap(self, x: int, y: int, polls: int = 4, settle: float = 0.3) -> None:
        """Touch the bottom screen at (x, y) for `polls` touch polls (debug build only)."""
        self.rpc.write(self.dbg + capture.DBG_TOUCH, struct.pack("<III", x, y, polls))
        deadline = time.monotonic() + 3
        while settle and time.monotonic() < deadline:
            if struct.unpack("<I", self.rpc.read(self.dbg + capture.DBG_TOUCH + 8, 4))[0] == 0:
                break
            time.sleep(0.02)
        time.sleep(settle)


def _mb(value: int) -> str:
    return f"{value / (1 << 20):.1f} MB"


def _origin(ident: capture.Ident) -> str:
    if ident.arc == capture.ARC_MEMORY:
        return "memory"
    if ident.arc == capture.ARC_UNKNOWN:
        return "unknown"
    return f"archive {ident.arc}"


def results_document(*, passed, debug_at_start, peak_used, language, validated, failures, ends,
                     soak_seconds, origins, log) -> str:
    total = debug_at_start.app_mem_total
    lines = [
        "# Stage 1 results",
        "",
        f"Result: **{'PASSED' if passed else 'FAILED'}**",
        "",
        "Written by `jpreader e2e stage1`. Azahar in Old 3DS mode, debug build of the payload.",
        "",
        "## Memory",
        "",
        "| Figure | Value |",
        "|---|---|",
        f"| Application memory | {_mb(total)} |",
        f"| Used when the payload started | {_mb(debug_at_start.app_mem_used_init)} |",
        f"| Used after the socket buffer was claimed | {_mb(debug_at_start.app_mem_used_now)} |",
        f"| Peak used during the run | {_mb(peak_used)} |",
        f"| Free at the peak | {_mb(total - peak_used)} |",
        f"| Socket buffer address | {debug_at_start.sock_buf_addr:#010x} (result {debug_at_start.sock_buf_result:#x}) |",
        f"| Result of the protection call | {debug_at_start.protect_result:#x} |",
        "",
        "## Capture",
        "",
        "| Figure | Value |",
        "|---|---|",
        f"| Game language byte | {language} |",
        f"| Lines validated | {validated} |",
        f"| Lines that failed validation | {failures} |",
        f"| Talk windows seen to end | {ends} |",
        f"| Soak time | {soak_seconds} s |",
        "",
        "Where the validated lines came from: " + ", ".join(f"{k}: {v}" for k, v in sorted(origins.items())),
        "",
        "## Log",
        "",
        "```",
        *log,
        "```",
        "",
    ]
    return "\n".join(lines)


def stage1(root: Path, rom_path: Path, count: int, soak: int, manual: bool) -> int:
    from . import build

    info = build.build(root, rom_path, debug=True)
    index = capture.ScriptIndex(rom_path)
    rpc = start(root, rom_path, root / "dist" / "azahar-debug")
    log: list[str] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    passed = False
    debug_at_start = None
    peak_used = 0
    validated = failures = ends = 0
    origins: dict[str, int] = {}
    language = -1
    try:
        session = Session(rpc, info["symbols"])

        deadline = time.monotonic() + 60
        while session.debug().magic != capture.DBG_MAGIC:
            if time.monotonic() > deadline:
                say("FAILED: the payload never reported that it started")
                return 1
            time.sleep(0.5)
        debug_at_start = session.debug()
        peak_used = debug_at_start.app_mem_used_now
        say(f"payload started; application memory {_mb(debug_at_start.app_mem_total)}, "
            f"used {_mb(debug_at_start.app_mem_used_now)}")
        if debug_at_start.sock_buf_addr != SOCK_BUF_ADDR:
            say(f"FAILED: socket buffer not allocated (result {debug_at_start.sock_buf_result:#x})")
            return 1
        if debug_at_start.panel_image_addr != PANEL_IMAGE_ADDR:
            say("FAILED: the panel's image buffer was not allocated")
            return 1
        if session.header().magic != capture.CAP_MAGIC:
            say("FAILED: the capture block was not initialised")
            return 1

        deadline = time.monotonic() + 180
        while session.debug().polls == 0:
            if time.monotonic() > deadline:
                say("FAILED: the game never polled its buttons (did it boot?)")
                return 1
            time.sleep(0.5)
        say("the game is polling input")

        # Give the first screen time to appear before pressing anything.
        deadline = time.monotonic() + 60
        while session.header().seq == 0 and time.monotonic() < deadline:
            time.sleep(0.5)
        language = session.language()
        say(f"language byte before any input: {language}")
        if language not in JAPANESE and not manual:
            say("the default language is not Japanese; moving the cursor up once")
            session.press("UP")

        seen = 0
        run = 0
        step = 0
        end_marks = session.header().end_count
        deadline = time.monotonic() + 900
        while time.monotonic() < deadline and not (run >= count and ends > 0):
            if manual:
                time.sleep(0.3)
            else:
                session.press(PRESS_CYCLE[step % len(PRESS_CYCLE)])
                step += 1
            head = session.header()
            if head.end_count != end_marks and head.open == 0:
                ends += head.end_count - end_marks
                end_marks = head.end_count
            while seen < head.seq:
                seen += 1
                message = session.message(seen)
                if message is None:
                    say(f"#{seen} LOST: overwritten before it was read")
                    failures += 1
                    run = 0
                    continue
                good, why = index.check(message)
                shown = render(message.text).replace("\n", "/")[:60]
                say(f"#{seen} {'OK ' if good else 'BAD'} {why} | {shown}")
                if good:
                    validated += 1
                    run += 1
                    origins[_origin(message.ident)] = origins.get(_origin(message.ident), 0) + 1
                    if is_name_prompt(message.text) and not manual:
                        say("name prompt seen; typing a name")
                        enter_name(session)
                else:
                    failures += 1
                    run = 0
        language = session.language()
        if run < count:
            say(f"FAILED: only {run} consecutive lines validated (wanted {count})")
            return 1
        if ends == 0:
            say("FAILED: no talk window was seen to end")
            return 1
        if language not in JAPANESE:
            say(f"FAILED: the game language is {language}, not Japanese")
            return 1
        say(f"{run} consecutive lines validated; {ends} talk window(s) ended")

        soak_end = time.monotonic() + soak
        stalled = 0
        last_polls = session.debug().polls
        while time.monotonic() < soak_end:
            if manual:
                time.sleep(2)
            else:
                session.press("A", settle=1.5)
            now = session.debug()
            peak_used = max(peak_used, now.app_mem_used_now)
            stalled = stalled + 1 if now.polls == last_polls else 0
            last_polls = now.polls
            if stalled >= 5:
                say("FAILED: the game stopped polling input during the soak")
                return 1
            if not emu.is_running():
                say("FAILED: the emulator exited during the soak")
                return 1
        say(f"soak of {soak} s completed; peak memory used {_mb(peak_used)}")
        passed = True
        return 0
    except emu.EmuNotResponding as error:
        say(f"FAILED: {error}")
        return 1
    finally:
        emu.stop()
        if debug_at_start is not None:
            (root / "docs" / "stage1-results.md").write_text(results_document(
                passed=passed, debug_at_start=debug_at_start, peak_used=peak_used, language=language,
                validated=validated, failures=failures, ends=ends, soak_seconds=soak,
                origins=origins, log=log))


STICK_POLLS = 24        # one circle-pad unit: about 0.4 s of walking


def parse_key(key: str) -> tuple:
    if key.startswith("TAP:"):
        x, y = key[4:].split(",")
        return ("tap", int(x), int(y))
    if key.startswith("WAIT:"):
        return ("wait", float(key[5:]))
    name, _, count = key.partition("*")
    if name not in BUTTONS:
        raise ValueError(f"unknown key {key!r}")
    return ("press", name, int(count or 1))


def status_lines(session: Session) -> list[str]:
    head = session.header()
    debug = session.debug()
    lines = [f"capture: seq {head.seq}, open {head.open}, ends {head.end_count}, language {session.language()}",
             f"touch: hidden {debug.touch_hidden}, passed {debug.touch_passed}"]
    if session.panel_addr is not None:
        p = session.panel()
        lines.append(f"panel: visible {p.visible}, frames {p.frames}, block {p.view_block + 1}/{p.block_count} "
                     f"(box on {p.live_block + 1}), scroll {p.scroll}, taps {p.taps}, "
                     f"font {p.font_status} ({p.font_glyphs} glyphs, error {p.font_error}), "
                     f"buffer {p.fb_addr:#x} format {p.fb_format:#x}")
        lines.append(f"words: data {p.data_status} (error {p.data_error:#x}), line {p.annot_status}, "
                     f"units {p.unit_count}, selected {p.selected} at {p.sel_start}+{p.sel_len}, "
                     f"entry {p.card_entry:#x}, card {p.card_status}")
    message = session.message(head.seq) if head.seq else None
    if message:
        lines.append("message: " + render(message.text).replace("\n", "/")[:80])
    return lines


def _session(root: Path) -> Session:
    info = json.loads((root / "build" / "build-info-debug.json").read_text())
    rpc = emu.Rpc()
    rpc.select_title(TITLE_ID)
    return Session(rpc, info["symbols"])


def drive(root: Path, keys: list[str], shot: Path | None) -> int:
    """Act on the game already running in the emulator, then report its state."""
    session = _session(root)
    for key in keys:
        action = parse_key(key)
        if action[0] == "tap":
            session.tap(action[1], action[2])
        elif action[0] == "wait":
            time.sleep(action[1])
        else:
            polls = STICK_POLLS if action[1].startswith("STICK_") else 6
            for _ in range(action[2]):
                session.press(action[1], polls=polls)
    for line in status_lines(session):
        print(line)
    if shot:
        from . import screen

        print("screenshot:", screen.capture(shot))
    return 0


def choose_language(session: Session, kanji: bool) -> None:
    """On the first screen, pick kana (the default) or kanji."""
    if kanji:
        time.sleep(LANGUAGE_SCREEN_WAIT)
        for key in KANJI_KEYS:
            session.press(key, settle=2.0)   # each key changes the screen


def boot(root: Path, rom_path: Path, messages: int, kanji: bool) -> int:
    """Boot the debug build and press on until `messages` lines are captured; leave it running."""
    from . import build

    info = build.build(root, rom_path, debug=True)
    session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
    choose_language(session, kanji)
    seen = 0
    deadline = time.monotonic() + 600
    while session.header().seq < messages and time.monotonic() < deadline:
        session.press("A")
        head = session.header()
        while seen < head.seq:
            seen += 1
            message = session.message(seen)
            if message and is_name_prompt(message.text):
                enter_name(session)
    for line in status_lines(session):
        print(line)
    return 0


GL_FORMATS = (0x8058, 0x8051, 0x8D62, 0x8057, 0x8056)
TEXT_TAP = (160, 48)            # middle of the panel's text area
NEXT_TAP = (102, 224)           # panel.h: JPR_NEXT_X + 18, the next-page button
DRAW_BUDGET_MS = 2.0          # of a 33 ms frame; the game needs the rest
STAGE2_REQUIRED = ("font loaded", "bottom-screen format", "drawing time", "panel on screen", "touch kept from the game",
                   "every message shown", "panel goes away", "touch reaches the game when closed")


def wait_for(predicate, timeout: float, interval: float = 0.1) -> bool:
    deadline = time.monotonic() + timeout
    while True:
        if predicate():
            return True
        if time.monotonic() > deadline:
            return False
        time.sleep(interval)


def stage2_document(*, mode, passed, checks, log, images) -> str:
    lines = [f"# Stage 2 results ({mode})", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage2`. Azahar in Old 3DS mode, debug build of the payload.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def _illustrate(root: Path, name: str, say=print) -> str | None:
    """A screenshot for a results document only (no check reads it): None, and a note, when macOS refuses the capture
    (a locked screen), so the run's checks still finish."""
    from . import screen

    try:
        return _publish(root, screen.capture(root / "build" / "shots" / name), name)
    except subprocess.CalledProcessError:
        say(f"no screenshot {name}: macOS refused the window capture (is the screen locked?)")
        return None


def _publish(root: Path, shot: Path, name: str) -> str:
    target = root / "docs" / "images" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["sips", "-Z", "900", str(shot), "--out", str(target)], check=True, capture_output=True)
    return f"images/{name}"


def stage2(root: Path, rom_path: Path, kanji: bool, count: int, manual: bool) -> int:
    from . import build, screen

    mode = "kanji" if kanji else "kana"
    info = build.build(root, rom_path, debug=True)
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    def open_checks(session: Session) -> None:
        panel = session.panel()
        check("font loaded", panel.font_status == capture.FONT_OK and panel.font_glyphs > 2000,
              f"status {panel.font_status}, {panel.font_glyphs} glyphs, error {panel.font_error}")
        check("bottom-screen format", panel.fb_format in GL_FORMATS,
              f"{panel.fb_format:#x}, buffer at {panel.fb_addr:#x}")
        ticks = []
        for _ in range(10):
            ticks.append(session.panel().draw_ticks)
            time.sleep(0.1)
        worst = capture.ticks_to_ms(max(ticks))
        check("drawing time", worst < DRAW_BUDGET_MS,
              f"worst of 10 frames {worst:.2f} ms of CPU time (budget {DRAW_BUDGET_MS} ms)")
        shot = screen.capture(shots / f"stage2-{mode}-dialog.png")
        share = screen.colour_fraction(shot)
        check("panel on screen", share > 0.03, f"{share:.1%} of the window is panel colour")
        images.append(("A talk window is open", _publish(root, shot, f"stage2-{mode}-dialog.png")))
        before = session.debug()
        session.tap(*TEXT_TAP)
        after = session.debug()
        check("touch kept from the game",
              after.touch_hidden > before.touch_hidden and after.touch_passed == before.touch_passed,
              f"hidden {before.touch_hidden} → {after.touch_hidden}, passed {before.touch_passed} → {after.touch_passed}")
        panel = session.panel()
        if panel.block_count == 1:
            session.tap(*NEXT_TAP)
            later = session.panel()
            check("ends of the message", later.view_block == 0 and later.taps == panel.taps,
                  f"▶ on a one-block message: view {later.view_block}, taps {panel.taps} → {later.taps}")

    def close_checks(session: Session) -> bool:
        """True once the closed-window checks are done; False to try again at the next close."""
        hidden = wait_for(lambda: session.panel().visible == 0, 2.0)
        if not hidden:
            check("panel goes away", False, f"visible {session.panel().visible} two seconds after the window closed")
            return True
        shot = screen.capture(shots / f"stage2-{mode}-closed.png")
        if session.header().open:
            return False
        share = screen.colour_fraction(shot)
        before = session.debug()
        session.tap(*TEXT_TAP)
        after = session.debug()
        if session.header().open:
            return False
        check("panel goes away", share < 0.005, f"{share:.2%} of the window is panel colour")
        images.append(("The window has closed", _publish(root, shot, f"stage2-{mode}-closed.png")))
        check("touch reaches the game when closed",
              after.touch_passed > before.touch_passed and after.touch_hidden == before.touch_hidden,
              f"passed {before.touch_passed} → {after.touch_passed}, hidden {before.touch_hidden} → {after.touch_hidden}")
        return True

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        choose_language(session, kanji)
        seen = shown = 0
        failures: list[str] = []
        opened = closed = saw_kanji = False
        ends = session.header().end_count
        deadline = time.monotonic() + 900
        while time.monotonic() < deadline and not (shown >= count and closed):
            head = session.header()
            while seen < head.seq:
                seen += 1
                message = session.message(seen)
                if message is None:
                    continue
                saw_kanji = saw_kanji or any(0x4E00 <= u <= 0x9FFF for u in message.text)
                if seen == head.seq and head.open:
                    expected = min(len(split_blocks(message.text)), 32)
                    ok = wait_for(lambda: (p := session.panel()).visible == 1 and p.msg_seq == seen, 3.0)
                    panel = session.panel()
                    ok = ok and panel.block_count == expected
                    say(f"#{seen} {'shown' if ok else 'NOT SHOWN'}: blocks {panel.block_count}/{expected} | "
                        + render(message.text).replace("\n", "/")[:50])
                    shown += 1
                    if not ok:
                        failures.append(f"#{seen}")
                    elif not opened:
                        open_checks(session)
                        opened = True
                if is_name_prompt(message.text) and not manual:
                    enter_name(session)
            head = session.header()
            if head.end_count != ends:
                ends = head.end_count
                if opened and not closed and not head.open:
                    closed = close_checks(session)
            if not manual:
                session.press("A")
        check("every message shown", shown >= count and not failures,
              f"{shown} messages; not shown or wrong block count: {', '.join(failures) or 'none'}")
        if kanji:
            check("kanji mode", session.language() == 1 and saw_kanji,
                  f"language byte {session.language()}, kanji seen: {saw_kanji}")
        for name in STAGE2_REQUIRED:
            checks.setdefault(name, (False, "not reached"))
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / f"stage2-results-{mode}.md").write_text(
            stage2_document(mode=mode, passed=passed, checks=checks, log=log, images=images))


PANEL_TEXT_X, PANEL_TEXT_TOP, PANEL_TEXT_W, PANEL_LINE_PITCH = 6, 2, 308, 24   # panel.h
STAGE3_TAPS = 3
ANNOT_NAMES = {0: "no data", 1: "found", 2: "unknown line", 3: "no words", 4: "did not fit", 5: "read error"}
STAGE3_REQUIRED = ("data files open", "units match the build", "lines found", "words selected by tapping",
                   "cards read", "drawing time with a card")


def first_line(text, advance) -> list[tuple[int, int, int]]:
    """(index, x, advance) of each character on the first line the panel shows for a message."""
    out: list[tuple[int, int, int]] = []
    x, i = 0, 0
    while i < len(text):
        unit = text[i]
        if unit == TAG_MARK:
            if i + 2 < len(text) and text[i + 2] == TAG_CLEAR and out:
                break
            i += 2 + (text[i + 1] if i + 1 < len(text) else 0)
            continue
        if unit == 0x000A:
            if out:
                break
            i += 1
            continue
        width = advance(unit)
        if out and x + width > PANEL_TEXT_W:
            break
        out.append((i, x, width))
        x += width
        i += 1
    return out


def first_line_units(text, placed, advance) -> list[tuple[int, int, int]]:
    """(unit index, x, y) of each unit with an entry lying wholly on the first line the panel shows."""
    where = {i: (x, width) for i, x, width in first_line(text, advance)}
    out = []
    for k, unit in enumerate(placed):
        if unit.unit.entry is None or any(i not in where for i in range(unit.start, unit.start + unit.length)):
            continue
        x, width = where[unit.start]
        out.append((k, PANEL_TEXT_X + x + width // 2, PANEL_TEXT_TOP + PANEL_LINE_PITCH // 2))
    return out


def tap_target(text, placed, advance) -> tuple[int, int, int] | None:
    """(unit index, x, y) of the last unit with an entry lying wholly on the first line: aiming away
    from the start of the line exercises the payload's hit test."""
    units = first_line_units(text, placed, advance)
    return units[-1] if units else None


def stage3_document(*, mode, passed, checks, statuses, log, images) -> str:
    lines = [f"# Stage 3B results ({mode})", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage3`. Azahar in Old 3DS mode, debug build of the payload.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    lines += ["", "## Lines by outcome", "", "| Outcome | Messages |", "|---|---|"]
    lines += [f"| {ANNOT_NAMES[code]} | {n} |" for code, n in sorted(statuses.items())]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage3(root: Path, rom_path: Path, kanji: bool, count: int, manual: bool) -> int:
    from collections import Counter

    from . import align, build, font, formats, screen

    mode = "kanji" if kanji else "kana"
    info = build.build(root, rom_path, debug=True)
    sd = root / "dist" / "azahar-debug" / "sdmc" / "jpreader"
    archives = formats.read_annot((sd / "annot.bin").read_bytes())
    advance = font.advance_function(font.read_font((sd / "font.bin").read_bytes()))
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []
    statuses: Counter = Counter()

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    taps: list[tuple[bool, bool, str]] = []      # selected right, card read, detail
    tapped_messages: set[int] = set()

    def try_tap(session: Session, seq: int, message, placed) -> None:
        target = tap_target(message.text, placed, advance)
        if target is None:
            return
        k, x, y = target
        unit = placed[k]
        session.tap(x, y)
        selected = wait_for(lambda: session.panel().selected == k + 1, 2.0)
        read = wait_for(lambda: session.panel().card_status == capture.CARD_OK, 2.0)
        panel = session.panel()
        selected = selected and panel.sel_start == unit.start and panel.card_entry == unit.unit.entry
        word = "".join(map(chr, message.text[unit.start:unit.start + unit.length]))
        taps.append((selected, read, f"#{seq} {word}: selected {panel.selected}/{k + 1} at {panel.sel_start}/{unit.start}, "
                                      f"entry {panel.card_entry}/{unit.unit.entry}, card {panel.card_status}"))
        tapped_messages.add(seq)
        say(f"tap #{seq} at ({x}, {y}) on {word}: {taps[-1][2]}")
        if len(images) < 2:
            name = f"stage3b-{mode}-card-{len(images) + 1}.png"
            images.append((f"Card for {word}", _publish(root, screen.capture(shots / name), name)))
        if "drawing time with a card" not in checks and read:
            ticks = []
            for _ in range(10):
                ticks.append(session.panel().draw_ticks)
                time.sleep(0.1)
            worst = capture.ticks_to_ms(max(ticks))
            check("drawing time with a card", worst < DRAW_BUDGET_MS,
                  f"worst of 10 frames {worst:.2f} ms of CPU time (budget {DRAW_BUDGET_MS} ms)")

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        choose_language(session, kanji)
        seen = shown = agree = with_record = found = 0
        disagreements: list[str] = []
        deadline = time.monotonic() + 900
        while time.monotonic() < deadline and not (shown >= count and len(taps) >= STAGE3_TAPS):
            head = session.header()
            while seen < head.seq:
                seen += 1
                message = session.message(seen)
                if message is None:
                    continue
                if seen == head.seq and head.open:
                    if not wait_for(lambda: (p := session.panel()).visible == 1 and p.msg_seq == seen, 3.0):
                        disagreements.append(f"#{seen} not shown")
                        continue
                    panel = session.panel()
                    if "data files open" not in checks:
                        check("data files open", panel.data_status == capture.DATA_OK,
                              f"status {panel.data_status}, error {panel.data_error:#x}")
                    # the language byte as the payload read it: it changes only after the language menu
                    status, placed = align.lookup(archives, message.ident, session.language(), message.text)
                    statuses[status] += 1
                    shown += 1
                    ok = panel.annot_status == status and panel.unit_count == len(placed)
                    agree += ok
                    if status in (align.OK, align.MISMATCH):
                        with_record += 1
                        found += status == align.OK
                    say(f"#{seen} {ANNOT_NAMES.get(panel.annot_status, '?')}, {panel.unit_count} units "
                        f"(host: {ANNOT_NAMES[status]}, {len(placed)}) | " + render(message.text).replace("\n", "/")[:40])
                    if not ok:
                        disagreements.append(f"#{seen}")
                    if len(taps) < STAGE3_TAPS and placed and panel.view_block == 0 and panel.scroll == 0:
                        try_tap(session, seen, message, placed)       # one tap per message
                if is_name_prompt(message.text) and not manual:
                    enter_name(session)
            if not manual:
                session.press("A")
        check("units match the build", shown >= count and not disagreements,
              f"{agree} of {shown} messages agree; differing: {', '.join(disagreements[:10]) or 'none'}")
        share = found / with_record if with_record else 0.0
        check("lines found", with_record and share >= 0.9,
              f"{found} of {with_record} lines with words placed in the displayed text ({share:.0%})")
        check("words selected by tapping", len(taps) >= STAGE3_TAPS and len(tapped_messages) >= 2
              and all(ok for ok, _read, _d in taps), "; ".join(d for _ok, _read, d in taps) or "no taps")
        check("cards read", taps and all(read for _ok, read, _d in taps),
              f"{sum(read for _ok, read, _d in taps)} of {len(taps)} cards read")
        for name in STAGE3_REQUIRED:
            checks.setdefault(name, (False, "not reached"))
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / f"stage3b-results-{mode}.md").write_text(
            stage3_document(mode=mode, passed=passed, checks=checks, statuses=statuses, log=log, images=images))


(FAULT_NONE, FAULT_NO_WIFI, FAULT_BUSY, FAULT_REFUSED, FAULT_CLOCK, FAULT_TIMEOUT, FAULT_STATUS, FAULT_NO_ENTROPY,
 FAULT_CONNECT) = range(9)
GRAMMAR_TAP = (282, 224)        # panel.h: JPR_GRAMMAR_X + 34, in the bar
VOICE_TAP = (176, 224)          # panel.h: JPR_VOICE_X + 22
SLOW_TAP = (222, 224)           # panel.h: JPR_SLOW_X + 22
BACK_TAP = (24, 224)
RETRY_TAP = (100, 224)
UP_TAP, DOWN_TAP = (240, 224), (292, 224)
REPLY_LIMIT_S = 15.0            # spec 10, stage 4
REQUEST_WAIT_S = 50.0           # the payload's own limit is 45 s
GR_ASKING, GR_DONE, GR_ERROR = 1, 2, 3


@dataclass(frozen=True)
class Row:
    message: str                # the text on screen, or its start when it carries a code
    failure: int                # JPR_NET_*
    how: str                    # how the e2e produces it
    linked: bool = True         # config.txt names the stand-in kokoro
    token: str | None = None    # None: the stand-in's token
    fault: int = FAULT_NONE
    arg: int = 0


ROWS = (
    Row("Grammar needs kokoro: run jpreader kokoro-link", 1, "config.txt without kokoro_url and kokoro_token",
        linked=False),
    Row("No Wi-Fi connection", 2, "fault NO_WIFI (Azahar always reports Wi-Fi)", fault=FAULT_NO_WIFI),
    Row("Network is busy (the game is online)", 3, "fault BUSY", fault=FAULT_BUSY),
    Row("Could not reach kokoro (code 4", 4, "fault REFUSED: a real refused connection to 127.0.0.1:1",
        fault=FAULT_REFUSED),
    Row("Could not verify kokoro (check the console's date)", 5,
        "fault CLOCK: a real certificate check against 2000-01-01", fault=FAULT_CLOCK),
    Row("Token rejected: run jpreader kokoro-link", 6, "the stand-in, asked with the token kkr_wrong",
        token="kkr_wrong"),
    Row("kokoro is busy, try again soon", 6, "fault STATUS 429 on a real response", fault=FAULT_STATUS, arg=429),
    Row("kokoro could not explain that, try again", 6, "the stand-in answering 500"),
    Row("Timed out", 7, "fault TIMEOUT: a real 1 ms time limit", fault=FAULT_TIMEOUT),
    Row("Could not reach kokoro (code 5900)", 4,
        "fault NO_ENTROPY: ssl:C gives no random bytes, so TLS must not start", fault=FAULT_NO_ENTROPY),
)


def write_config(sdmc: Path, kokoro: tuple[str, str] | None = None, debug: bool = False, voice: bool = True) -> Path:
    """config.txt on the emulator's SD card. The token is written, never printed. Runs pass the stand-in's
    URL and token, never the real kokoro's."""
    path = sdmc / "jpreader" / "config.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    text = f"debug={int(debug)}\n" + ("" if voice else "voice=0\n")
    if kokoro:
        text += f"kokoro_url={kokoro[0]}\nkokoro_token={kokoro[1]}\n"
    path.write_text(text)
    return path


def next_dialog(session: "Session", after: int, manual: bool) -> None:
    """Presses A until a new message with tappable text is on the panel."""
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline:
        head, panel = session.header(), session.panel()
        if head.seq > after and head.open and panel.visible and panel.msg_seq == head.seq and panel.unit_count:
            time.sleep(0.5)
            return
        message = session.message(head.seq) if head.seq else None
        if message and is_name_prompt(message.text) and not manual:
            enter_name(session)
        if not manual:
            session.press("A")
    raise emu.EmuNotResponding("no message with text came up")


def stage4_document(*, passed, checks, rows, log, images) -> str:
    lines = ["# Stage 4 results", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage4`. Azahar in Old 3DS mode, debug build, kana mode. Grammar asks "
             "the stand-in kokoro over HTTPS (never the real kokoro).", "", "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    lines += ["", "## Messages of spec 9.4", "", "| Expected | Shown | How it was produced | Result |", "|---|---|---|---|"]
    lines += [f"| {row.message} | {shown} | {row.how} | {'pass' if ok else 'FAIL'} |" for row, shown, ok in rows]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage4(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build, screen, standin, tls

    info = build.build(root, rom_path, debug=True)
    sdmc = emu.profile_dirs(root / "build" / "emu" / "profile")["data"] / "sdmc"
    replies = sdmc / "jpreader" / "replies.bin"
    replies.unlink(missing_ok=True)     # every request of this run asks kokoro until it is answered
    server = standin.StandIn(tls.ensure_test_ca(root), STANDIN_TOKEN).start()
    linked = (server.url, STANDIN_TOKEN)
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    rows: list[tuple[Row, str, bool]] = []
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    def shot(name: str, caption: str) -> None:
        images.append((caption, _publish(root, screen.capture(shots / name), name)))

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        next_dialog(session, 0, manual)

        for n, row in enumerate(ROWS):
            server.explain_status = 500 if row.message.startswith("kokoro could not explain") else 200
            write_config(sdmc, kokoro=(server.url, row.token or STANDIN_TOKEN) if row.linked else None)
            session.fault(row.fault, row.arg)
            session.tap(*(GRAMMAR_TAP if n == 0 else RETRY_TAP))
            done = wait_for(lambda: session.panel().gr_state == GR_ERROR, REQUEST_WAIT_S)
            panel = session.panel()
            ok = done and panel.gr_failure == row.failure and panel.gr_message.startswith(row.message)
            rows.append((row, panel.gr_message, ok))
            say(f"{'PASS' if ok else 'FAIL'} row: {panel.gr_message!r} (failure {panel.gr_failure}, "
                f"HTTP {panel.gr_http}) via {row.how}")
            if n < 2:
                shot(f"stage4-error-{n + 1}.png", panel.gr_message)
        check("every message of spec 9.4", all(ok for _row, _shown, ok in rows),
              f"{sum(ok for _row, _shown, ok in rows)} of {len(ROWS)} rows shown as specified")

        server.explain_status = 200
        write_config(sdmc, kokoro=linked)
        session.fault(FAULT_NONE)
        asked = len(server.explained)
        start_s = time.monotonic()
        session.tap(*RETRY_TAP)
        done = wait_for(lambda: session.panel().gr_state == GR_DONE, REQUEST_WAIT_S)
        elapsed = time.monotonic() - start_s
        result = session.link_result()
        check("a reply within 15 s", done and elapsed <= REPLY_LIMIT_S and len(server.explained) == asked + 1,
              f"state {session.panel().gr_state} after {elapsed:.1f} s; {len(result[4])} bytes; "
              f"stand-in asked {len(server.explained) - asked} time(s)")
        lines = result[4].splitlines()
        first = lines[0] if lines else ""
        check("the reply is in the tagged format", first[:3] in ('T "', "T \u201c") and
              any(line.startswith("C ") for line in lines) and any(line.startswith("G ") for line in lines),
              f"first line: {first[:60]!r}; {sum(line.startswith('C ') for line in lines)} chunks; "
              f"{sum(line.startswith('G ') for line in lines)} grammar points kept (stage 17)")
        say("reply: " + result[4].replace("\n", " / ")[:300])
        shot("stage4-reply.png", "A reply")

        requests = session.panel().gr_requests
        session.tap(*DOWN_TAP)
        scrolled = session.panel().gr_scroll
        session.tap(*UP_TAP)
        check("scrolling", session.panel().gr_scroll == 0 and 0 <= scrolled <= 54,
              f"down: {scrolled} px (three rows, or less at the end of the reply; 0 when it fits), "
              f"up: {session.panel().gr_scroll} px")
        session.tap(*BACK_TAP)
        closed = session.panel().gr_open == 0
        session.tap(*GRAMMAR_TAP)
        panel = session.panel()
        check("the stored reply comes back", closed and panel.gr_state == GR_DONE and panel.gr_requests == requests,
              f"after Back: open {int(not closed)}; Grammar again: state {panel.gr_state}, requests "
              f"{requests} -> {panel.gr_requests}")
        session.tap(*BACK_TAP)
        session.forget_reply()              # as a new conversation does: only replies.bin can answer now
        session.tap(*GRAMMAR_TAP)
        panel = session.panel()
        check("a reply is remembered on the SD card", panel.gr_state == GR_DONE and panel.gr_requests == requests
              and replies.exists(),
              f"Grammar again after forgetting: state {panel.gr_state}, requests {requests} -> {panel.gr_requests}; "
              f"replies.bin {replies.stat().st_size if replies.exists() else 'missing'} bytes")
        session.tap(*BACK_TAP)

        # the remembered reply took a request id the worker never saw; kokoro's word sync shares the result
        # buffer, so count from the Grammar reply's own id
        last_id = result[0] + 1
        session.fault(FAULT_CONNECT)        # Azahar always reports Wi-Fi: make the requests connect as on a 3DS
        write_config(sdmc, kokoro=linked, debug=True)
        log_file = sdmc / "jpreader" / "log.txt"
        log_file.unlink(missing_ok=True)
        next_dialog(session, session.header().seq, manual)
        session.tap(*GRAMMAR_TAP, settle=0.1)
        session.tap(*BACK_TAP, settle=0.1)
        session.tap(*GRAMMAR_TAP)
        done = wait_for(lambda: session.panel().gr_state == GR_DONE, REQUEST_WAIT_S)
        panel = session.panel()
        final = session.link_result()
        check("Back cancels a request", done and panel.gr_requests == requests + 2 and final[0] == last_id + 2,
              f"state {panel.gr_state}, requests {requests} -> {panel.gr_requests}, result id {last_id} -> {final[0]}")
        check("a reply through the game's Wi-Fi connection sequence", done and final[1] == 0,
              f"fault CONNECT: state {panel.gr_state}, failure {final[1]}, code {final[2]}")
        steps = log_file.read_text() if log_file.exists() else ""
        check("debug=1 logs each step to jpreader/log.txt",
              "ac connect result 00000000" in steps and "http status 000000C8" in steps and STANDIN_TOKEN not in steps,
              f"{len(steps.splitlines())} lines; connect result logged: {'ac connect result' in steps}")
        write_config(sdmc, kokoro=linked)
        session.fault(FAULT_NONE)

        ends = session.header().end_count
        deadline = time.monotonic() + 300
        while session.header().end_count == ends and time.monotonic() < deadline:
            session.press("A")
        time.sleep(1)
        panel = session.panel()
        stays = panel.visible == 1 and panel.gr_open == 1 and not session.header().open
        passed_before = session.debug().touch_passed
        session.tap(*BACK_TAP, polls=12)
        gone = wait_for(lambda: session.panel().visible == 0, 2.0)
        leaked = session.debug().touch_passed - passed_before
        check("the view outlives the dialog", stays and gone and leaked == 0,
              f"after the dialog ended: visible {panel.visible}, open {panel.gr_open}; after Back: gone {gone}, "
              f"touch samples the game saw {leaked}")

        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        server.stop()
        write_config(sdmc, kokoro=linked)
        (root / "docs" / "stage4-results.md").write_text(
            stage4_document(passed=passed, checks=checks, rows=rows, log=log, images=images))


KOKORO_ADD_TAP = (248, 194)     # panel.h: JPR_ADD_X + 64, the middle of the card's kokoro row
KOKORO_NONE, KOKORO_NEW, KOKORO_LEARNING, KOKORO_KNOWN, KOKORO_QUEUED = range(5)
ADD_ASKING, ADD_DONE, ADD_QUEUED, ADD_REJECTED, ADD_FAILED = range(1, 6)
STANDIN_TOKEN = "kkr_emulator-stand-in"
STANDIN_WORDS = 4000            # known, and as many learning: 8,000 lines, the most kokoro sends
STANDIN_FALLBACK = 40           # kokoro cards with no id, by a kanji spelling of a kana game word


def standin_ids(entries, by_spelling: set[int]) -> dict[int, str]:
    """The stand-in's words by id: dictionary entries whose id is 0 mod 3 known and 1 mod 3 learning, so a run
    knows what every unit should show and still finds words kokoro does not have. Words kokoro holds by
    spelling (`by_spelling`) are left to that."""
    known: dict[int, str] = {}
    learning: dict[int, str] = {}
    for entry in entries:
        word = entry.jmdict_id
        if entry.kind != "dictionary" or not word or word in by_spelling:
            continue
        if word % 3 == 0 and len(known) < STANDIN_WORDS:
            known[word] = "known"
        elif word % 3 == 1 and len(learning) < STANDIN_WORDS:
            learning[word] = "learning"
    return {**known, **learning}


def standin_fallback(entries, words) -> tuple[dict[str, str], set[int]]:
    """kokoro cards with no id, as the 3DS must still mark them: for game words written in kana, a kanji spelling
    of the word (それ's 其れ) that no other JMdict word has, so the expected marks are exact. Returns the cards by
    front and the ids they stand for."""
    owners: dict[str, set[int]] = {}
    for word in words:
        for form in (*word.kanji, *word.kana):
            owners.setdefault(form, set()).add(int(word.id))
    forms = {int(word.id): word.kanji for word in words}
    fallback: dict[str, str] = {}
    used: set[int] = set()
    for entry in entries:
        word = entry.jmdict_id
        if entry.kind != "dictionary" or not word or word in used or entry.headword in forms.get(word, ()):
            continue
        spelling = next((k for k in forms.get(word, ()) if owners.get(k) == {word}), None)
        if spelling is None:
            continue
        fallback[spelling] = "known"
        used.add(word)
        if len(used) >= STANDIN_FALLBACK:
            break
    return fallback, used


def expected_marks(placed, entries, ids: dict[int, str], by_spelling: set[int]) -> tuple[int, int]:
    """(known, learning) units of a message, as the panel should count them."""
    states = []
    for u in placed:
        if u.unit.entry is None or entries[u.unit.entry].kind != "dictionary":
            continue
        word = entries[u.unit.entry].jmdict_id
        states.append("known" if word in by_spelling else ids.get(word))
    return states.count("known"), states.count("learning")


def stage9_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 9C results", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage9`. Azahar in Old 3DS mode, debug build, kana mode, against the "
             "stand-in kokoro (`standin.py`, protocol v2) over HTTPS with the test CA; the real kokoro is never "
             "contacted.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage9(root: Path, rom_path: Path, manual: bool) -> int:
    """Stage 9C on the 3DS (it replaces stage 8B's run, whose list by spelling the payload no longer reads):
    marks by JMdict id and by spelling, the add by id (added, and queued for a word kokoro has unstudied),
    an add saved offline and sent later, and a wrong token."""
    from . import align, build, font, formats, jmdict, screen, standin, tls

    info = build.build(root, rom_path, debug=True)
    sd = root / "dist" / "azahar-debug" / "sdmc" / "jpreader"
    archives = formats.read_annot((sd / "annot.bin").read_bytes())
    entries, _labels = formats.read_dict((sd / "dict.bin").read_bytes())
    advance = font.advance_function(font.read_font((sd / "font.bin").read_bytes()))
    sdmc = emu.profile_dirs(root / "build" / "emu" / "profile")["data"] / "sdmc"
    for name in ("kokoro.bin", "outbox.bin", "heads.bin"):
        (sdmc / "jpreader" / name).unlink(missing_ok=True)
    fallback, by_spelling = standin_fallback(entries, jmdict.load(jmdict.fetch(root / "build" / "cache")))
    ids = standin_ids(entries, by_spelling)
    server = standin.StandIn(tls.ensure_test_ca(root), STANDIN_TOKEN, ids=ids, fallback=fallback).start()
    listed = dict(ids)
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    def shot(name: str, caption: str) -> None:
        images.append((caption, _publish(root, screen.capture(shots / name), name)))

    def shown(session: Session):
        message = session.message(session.header().seq)
        return message, align.lookup(archives, message.ident, session.language(), message.text)[1]

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        # before the first dialog, where the payload reads config.txt: only the stand-in, never the real kokoro
        write_config(sdmc, kokoro=(server.url, STANDIN_TOKEN))
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        check("kokoro's memory block", session.debug().kokoro_addr == 0x0FE10000,
              f"at {session.debug().kokoro_addr:#x}")
        next_dialog(session, 0, manual)
        synced = wait_for(lambda: session.panel().kokoro_syncs >= 1, 60)
        panel = session.panel()
        want_words = len(ids) + len(fallback)
        check("the v2 word list syncs at the first dialog", synced and panel.kokoro_words == want_words
              and "GET /jpreader/words?v=2 200" in server.log,
              f"{panel.kokoro_words} of {want_words} words ({len(fallback)} by spelling); stand-in saw {server.log[:2]}")
        stored = sdmc / "jpreader" / "kokoro.bin"
        check("the list is kept on the SD card", stored.exists() and stored.stat().st_size >= 20 + 4 * want_words,
              f"kokoro.bin {stored.stat().st_size if stored.exists() else 'missing'} bytes")

        agree, known, learning, spelled, details = 0, 0, 0, 0, []
        for _ in range(8):
            message, placed = shown(session)
            want = expected_marks(placed, entries, listed, by_spelling)
            panel = session.panel()
            got = (panel.marks_known, panel.marks_learning)
            agree += got == want
            known += got[0]
            learning += got[1]
            spelled += sum(1 for u in placed if u.unit.entry is not None
                           and entries[u.unit.entry].jmdict_id in by_spelling)
            details.append(f"#{panel.msg_seq} {got}/{want}")
            if got[0] and got[1] and not images:
                shot("stage9-marks.png", "Words kokoro knows (green) and is learning (amber), by JMdict id")
            next_dialog(session, session.header().seq, manual)
        check("underlines follow kokoro's states by id", agree == 8 and known and learning, "; ".join(details))
        check("a kana word kokoro holds only under a kanji spelling is marked",
              agree == 8 and spelled > 0, f"{spelled} such units in these messages")

        steps = ["added", "queued", "offline", "flush", "rejected"]
        tried: set[int] = set()
        queued_id = 0
        deadline = time.monotonic() + 600
        while steps and time.monotonic() < deadline:
            message, placed = shown(session)
            for k, x, y in first_line_units(message.text, placed, advance):
                entry = entries[placed[k].unit.entry]
                word = entry.jmdict_id
                if not steps:
                    break
                if entry.kind != "dictionary" or not word or word in listed or word in by_spelling or word in tried:
                    continue
                tried.add(word)
                session.tap(x, y)
                if not wait_for(lambda: session.panel().card_status == capture.CARD_OK, 3.0):
                    continue
                step = steps[0]
                if step == "added":
                    shot("stage9-card.png", f"{entry.headword}: not in kokoro, with the add button")
                if step == "queued":
                    server.unmet.add(word)          # a Kaishi word not met yet
                if step == "offline":
                    session.fault(FAULT_NO_WIFI)
                if step == "rejected":
                    write_config(sdmc, kokoro=(server.url, "kkr_wrong"))
                session.tap(*KOKORO_ADD_TAP)
                wait_for(lambda: session.panel().add_state not in (0, ADD_ASKING), 60)
                panel = session.panel()
                sent = server.added[-1] if server.added else {}
                if step in ("added", "queued"):
                    reply = "OK added" if step == "added" else "OK queued"
                    check(f"an add {step}", panel.add_state == ADD_DONE and panel.card_kokoro == KOKORO_NEW
                          and sent.get("jmdict") == str(word) and sent.get("front") == entry.headword
                          and sent.get("sentence") and f"POST /jpreader/word 200" in server.log[-1]
                          and server.ids.get(word) == "new" and word not in server.unmet,
                          f"{entry.headword} ({word}): add {panel.add_state}, card {panel.card_kokoro}; "
                          f"stand-in answered {reply} to {sent}")
                    shot(f"stage9-{step}.png", f"{entry.headword}: {reply}, up next")
                elif step == "offline":
                    session.fault(FAULT_NONE)
                    queued_id = word
                    check("an add without Wi-Fi is saved for later", panel.add_state == ADD_QUEUED and panel.outbox == 1
                          and (sdmc / "jpreader" / "outbox.bin").exists(),
                          f"{entry.headword}: add {panel.add_state}, outbox {panel.outbox}")
                elif step == "flush":
                    delivered = wait_for(lambda: session.panel().outbox == 0 and
                                         str(queued_id) in [a.get("jmdict") for a in server.added], 90)
                    resynced = wait_for(lambda: session.panel().kokoro_syncs >= 2, 60)
                    check("saved adds go, by id, after the next request succeeds",
                          panel.add_state == ADD_DONE and delivered and resynced,
                          f"add {panel.add_state}; outbox {session.panel().outbox}; {queued_id} sent {delivered}; "
                          f"syncs {session.panel().kokoro_syncs}")
                else:
                    write_config(sdmc, kokoro=(server.url, STANDIN_TOKEN))
                    check("a wrong token says so", panel.add_state == ADD_REJECTED and panel.kokoro_http == 401,
                          f"add {panel.add_state}, HTTP {panel.kokoro_http}")
                steps.pop(0)
            if steps:
                next_dialog(session, session.header().seq, manual)
        for step in steps:
            check(f"add step '{step}'", False, "no word to add came up in time")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        server.stop()
        (root / "docs" / "stage9c-results.md").write_text(
            stage9_document(passed=passed, checks=checks, log=log, images=images))


POINT_TAP_X = 292               # panel.h: JPR_CARD_RIGHT - 20, inside a grammar point row's add column
GR_ROWS_TOP, GR_ROWS_BOTTOM = 52, 207   # JPR_GR_TOP + 2 and JPR_BAR_TOP - 1: the reply's rows on the screen
GR_ROW = 18                     # JPR_GR_PITCH


def stage17_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 17 results: kokoro's grammar points", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage17`. Azahar in Old 3DS mode, debug build, kana mode, against the stand-in "
             "kokoro (`standin.py`) over HTTPS with the test CA; the real kokoro is never contacted. The stand-in's "
             "explanation has five grammar points: one with no Bunpro match under ちょっと, one kokoro lacks under 待って, "
             "and three of no chunk (one known, two kokoro lacks).", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage17(root: Path, rom_path: Path, manual: bool) -> int:
    """Stage 17 (8C) on the 3DS: kokoro's grammar points as rows under their chunks and in "Other grammar", an add
    online, an add offline saved and sent after the next request that succeeds, a remembered reply showing this
    session's adds, and a wrong token."""
    from . import build, standin, tls

    info = build.build(root, rom_path, debug=True)
    sdmc = emu.profile_dirs(root / "build" / "emu" / "profile")["data"] / "sdmc"
    for name in ("replies.bin", "outbox.bin"):
        (sdmc / "jpreader" / name).unlink(missing_ok=True)
    server = standin.StandIn(tls.ensure_test_ca(root), STANDIN_TOKEN).start()
    linked = (server.url, STANDIN_TOKEN)
    log_file = sdmc / "jpreader" / "log.txt"
    log_file.unlink(missing_ok=True)
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")
        if not ok:
            say(f"  worker log: {worker_log()}")

    def scroll_to_end(session: Session) -> None:
        for _ in range(8):
            before = session.panel().gr_scroll
            session.tap(*DOWN_TAP)
            if session.panel().gr_scroll == before:
                return

    def add_tap(session: Session) -> bool:
        """Scrolls the first "+ Add" into view and taps it; True once the panel took the tap (its add then waits for
        kokoro's answer, ADD_ASKING, until it comes)."""
        for _ in range(10):
            panel = session.panel()
            if not panel.gr_buttons:
                say(f"no + Add to tap (scroll {panel.gr_scroll})")
                return False
            y = panel.gr_button_y
            if GR_ROWS_TOP <= y and y + GR_ROW <= GR_ROWS_BOTTOM:
                taps = panel.taps
                session.tap(POINT_TAP_X, y + GR_ROW // 2)
                if wait_for(lambda: session.panel().taps > taps, 2):
                    return True
                say(f"the tap at y {y + GR_ROW // 2} was not taken (scroll {session.panel().gr_scroll})")
                return False
            session.tap(*(UP_TAP if y < GR_ROWS_TOP else DOWN_TAP))
        say(f"the first + Add never came into view (y {session.panel().gr_button_y})")
        return False

    def answered(session: Session) -> capture.PanelState:
        wait_for(lambda: session.panel().gr_add_state != ADD_ASKING, 60)
        return session.panel()

    def worker_log() -> str:
        steps = log_file.read_text(errors="replace") if log_file.exists() else ""
        return " / ".join(steps.splitlines()[-14:])

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        write_config(sdmc, kokoro=linked, debug=True)   # before the first dialog, where the payload reads config.txt
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        next_dialog(session, 0, manual)

        session.tap(*GRAMMAR_TAP)
        done = wait_for(lambda: session.panel().gr_state == GR_DONE, REQUEST_WAIT_S)
        panel, text = session.panel(), session.link_result()[4]
        kept = [line for line in text.splitlines() if line.startswith("G ")]
        check("grammar points are rows under their chunks and in Other grammar",
              done and panel.gr_points == 5 and panel.gr_other == 3 and panel.gr_buttons == 3 and len(kept) == 5,
              f"state {panel.gr_state}; rows {panel.gr_points}, of no chunk {panel.gr_other}, + Add {panel.gr_buttons}; "
              f"G lines in the reply {len(kept)}")
        images.append(("A grammar point under its chunk (ちょっと: no Bunpro match)",
                       _illustrate(root, "stage17-points.png", say)))
        scroll_to_end(session)
        images.append(("The rest: 〜て under 待って, then Other grammar", _illustrate(root, "stage17-other.png", say)))

        sent = len(server.grammar_added)
        tapped = add_tap(session)
        panel = answered(session)
        check("an add online", tapped and panel.gr_add_state == ADD_DONE and server.grammar_added[sent:] == ["bunpro:1082"]
              and server.grammar.get(1082) == "new" and panel.gr_buttons == 2 and "POST /jpreader/grammar 200" in server.log,
              f"add {panel.gr_add_state}; stand-in got {server.grammar_added[sent:]}; + Add left {panel.gr_buttons}")
        images.append(("〜て added", _illustrate(root, "stage17-added.png", say)))

        session.fault(FAULT_NO_WIFI)
        tapped = add_tap(session)
        panel = answered(session)
        time.sleep(3)                       # the flush that follows fails too, without Wi-Fi
        later = session.panel()
        check("an add without Wi-Fi is saved for later", tapped and panel.gr_add_state == ADD_QUEUED
              and later.outbox == 1 and (sdmc / "jpreader" / "outbox.bin").exists() and later.gr_buttons == 1
              and server.grammar_added[sent:] == ["bunpro:1082"],
              f"tapped {tapped}; add {panel.gr_add_state} (kokoro failure {panel.kokoro_failure}); outbox {later.outbox}; "
              f"+ Add left {later.gr_buttons}")
        images.append(("〜ましょう saved for later", _illustrate(root, "stage17-queued.png", say)))

        requests = session.panel().gr_requests
        session.tap(*BACK_TAP)
        session.forget_reply()              # as a new conversation does: only replies.bin can answer now
        session.tap(*GRAMMAR_TAP)
        wait_for(lambda: session.panel().gr_state == GR_DONE, 10)
        panel = session.panel()
        check("a remembered reply shows this session's adds",
              panel.gr_state == GR_DONE and panel.gr_requests == requests and panel.gr_points == 5
              and panel.gr_buttons == 1,
              f"state {panel.gr_state}, requests {requests} -> {panel.gr_requests}; rows {panel.gr_points}, "
              f"+ Add {panel.gr_buttons} (added and queued points show so)")
        scroll_to_end(session)
        images.append(("The remembered reply: Added, Queued", _illustrate(root, "stage17-remembered.png", say)))

        session.fault(FAULT_NONE)
        session.tap(*BACK_TAP)
        next_dialog(session, session.header().seq, manual)
        session.tap(*GRAMMAR_TAP)
        done = wait_for(lambda: session.panel().gr_state == GR_DONE, REQUEST_WAIT_S)
        delivered = wait_for(lambda: session.panel().outbox == 0 and "bunpro:2000" in server.grammar_added, 90)
        panel = session.panel()
        check("the saved add goes after the next request that succeeds",
              done and delivered and server.grammar.get(2000) == "new" and panel.gr_buttons == 1,
              f"reply {panel.gr_state}; outbox {panel.outbox}; stand-in got {server.grammar_added[sent:]}; "
              f"+ Add left {panel.gr_buttons}")

        write_config(sdmc, kokoro=(server.url, "kkr_wrong"), debug=True)
        tapped = add_tap(session)
        panel = answered(session)
        write_config(sdmc, kokoro=linked, debug=True)
        check("a wrong token says so", tapped and panel.gr_add_state == ADD_REJECTED and panel.kokoro_http == 401,
              f"add {panel.gr_add_state}, HTTP {panel.kokoro_http}")
        images.append(("なさい with a wrong token", _illustrate(root, "stage17-rejected.png", say)))
        session.tap(*BACK_TAP)

        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        server.stop()
        write_config(sdmc, kokoro=linked)
        (root / "docs" / "stage17-results.md").write_text(
            stage17_document(passed=passed, checks=checks, log=log, images=images))


SOUND_TAP = (137, 224)          # panel.h: JPR_SOUND_X + 13, in the bar
MIX_PLUS_TAP = (279, 49)        # JPR_MIX_PLUS_X + 17, row 0 (Music)
MIX_VOICE_STEP5_TAP = (116 + 4 * 14 + 6, 133)   # the fifth step of row 2 (Voice)
PLAYER_STRIDE = 0x5C


def stage11_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 11 results: the sound mixer", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage11`. Azahar in Old 3DS mode, debug build. The figures are the game's own "
             "nw::snd player volumes, read from its memory (players 0 and 1 are the music, 2 to 11 the effects and "
             "cries), and our voice's nn::snd volume. As in stage 10, a debug switch makes each message stand in for "
             "story file 27, so a clip plays. Azahar's sound output itself is not checked.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage11(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build, screen

    build.build(root, rom_path, debug=True)
    info = json.loads((root / "build" / "build-info-debug.json").read_text())
    sdmc = emu.profile_dirs(root / "build" / "emu" / "profile")["data"] / "sdmc"
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []
    NONE = 0xFFFFFFFF

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    def f32(bits: int) -> float:
        return struct.unpack("<f", struct.pack("<I", bits))[0]

    def players(session: Session) -> list[float]:
        at = session.debug().mix_players
        return [struct.unpack("<f", session.rpc.read(at + i * PLAYER_STRIDE, 4))[0] for i in range(12)] if at else []

    def near(values: list[float], want: float) -> bool:
        return bool(values) and all(abs(v - want) < 0.002 for v in values)

    def fmt(values: list[float]) -> str:
        return " ".join(f"{v:.3f}" for v in values) or "none"

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug",
                                sound="music=05\neffects=08\nvoice=10\nduck=1\n"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        up = wait_for(lambda: session.debug().mix_players != 0 and session.debug().mix_applies > 0, 60)
        p = session.panel()
        check("sound.txt is read", up and (p.mix_music, p.mix_effects, p.mix_voice, p.mix_duck) == (5, 8, 10, 1),
              f"music {p.mix_music}, effects {p.mix_effects}, voice {p.mix_voice}, duck {p.mix_duck}")
        vols = players(session)
        check("the music plays at its level", near(vols[:2], 0.25),
              f"players 0-1: {fmt(vols[:2])} (music 5 = 0.25)")
        check("the effects and cries play at theirs", near(vols[2:], 0.64), f"players 2-11: {fmt(vols[2:])} (effects 8 = 0.64)")

        session.voice_test(0)
        next_dialog(session, 0, manual)
        playing = wait_for(lambda: session.debug().voice_clip != NONE and session.debug().snd_active == 1, 3)
        ducked = wait_for(lambda: session.debug().mix_duck == 400, 2)
        vols = players(session)
        check("the music ducks under a voice clip", playing and ducked and near(vols[:2], 0.10) and near(vols[2:], 0.64),
              f"duck {session.debug().mix_duck}, players 0-1: {fmt(vols[:2])}, 2-11 {fmt(vols[2:3])}…")
        d = session.debug()
        voice_vol = struct.unpack("<f", session.rpc.read(d.voice_ptr + 0x64, 4))[0] if d.voice_ptr else -1
        check("our voice plays at its level", abs(voice_vol - 1.0) < 0.002, f"voice volume {voice_vol:.3f} (voice 10 = 1.0)")
        back = wait_for(lambda: session.debug().voice_clip == NONE and session.debug().mix_duck == 1000, 20)
        vols = players(session)
        check("the music comes back after the clip", back and near(vols[:2], 0.25), f"players 0-1: {fmt(vols[:2])}")

        gets, sets = session.debug().mix_gets, session.debug().mix_sets
        say(f"the game's own player-volume calls so far: {sets} sets, {gets} gets (through the replaced functions)")
        session.mix_test(1)
        dimmed = wait_for(lambda: session.debug().mix_sets > sets, 3)
        time.sleep(0.2)
        d, vols = session.debug(), players(session)
        check("the game's own set keeps its value and ours rides on it", dimmed and abs(f32(d.mix_shadow0) - 0.5) < 0.002
              and near(vols[:2], 0.125) and near(vols[2:], 0.64),
              f"ChangeBGMPlayerVolume(0.5): sets {sets} -> {d.mix_sets}, the game's 0.5 kept, players 0-1 {fmt(vols[:2])} "
              f"(0.5 x 0.25), 2-11 unchanged")
        session.mix_test(2)
        faded = wait_for(lambda: abs(f32(session.debug().mix_shadow0) - 1.0) < 0.002, 5)
        d, vols = session.debug(), players(session)
        check("a game fade starts from its own value and lands at ours", faded and d.mix_gets > gets and near(vols[:2], 0.25),
              f"a 30-tick fade to 1.0: gets {gets} -> {d.mix_gets}, sets {d.mix_sets}, players 0-1 {fmt(vols[:2])}")

        session.tap(*SOUND_TAP)
        check("the speaker button opens the Sound view", session.panel().mix_open == 1, f"open {session.panel().mix_open}")
        images.append(("The Sound view", _illustrate(root, "stage11-sound.png", say)))
        session.tap(*MIX_PLUS_TAP)
        session.tap(*MIX_VOICE_STEP5_TAP)
        time.sleep(0.3)
        p, vols = session.panel(), players(session)
        check("Music + raises the music at once", p.mix_music == 6 and near(vols[:2], 0.36),
              f"music {p.mix_music}, players 0-1: {fmt(vols[:2])} (music 6 = 0.36)")
        d = session.debug()
        check("the voice level sets our voice's volume", p.mix_voice == 5 and abs(f32(d.mix_voice) - 0.25) < 0.002,
              f"voice {p.mix_voice}, volume {f32(d.mix_voice):.3f} (voice 5 = 0.25)")
        saves = d.mix_saves
        session.tap(*BACK_TAP)
        saved = wait_for(lambda: session.debug().mix_saves > saves, 3)
        text = (sdmc / "jpreader" / "sound.txt").read_text() if (sdmc / "jpreader" / "sound.txt").exists() else ""
        check("Back writes sound.txt", saved and session.panel().mix_open == 0 and "music=06" in text and "voice=05" in text,
              f"saves {saves} -> {session.debug().mix_saves}, file {text.strip()!r}")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / "stage11-results.md").write_text(
            stage11_document(passed=passed, checks=checks, log=log, images=images))


OPTION_TAP = (6 + 17 + 8, 2 + 24 + 12)   # panel.h: the first option's first glyph after its bullet, on line 2


def stage12_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 12 results: choice menus", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage12`. Azahar in Old 3DS mode, debug build. The opening's photo prompt "
             "(えらんだので いいよね？) is an app menu (app::tool::MenuWindow) under a talk window, with options from "
             "message data built in memory; it is the menu the emulator can reach. Field menus (App::Tool::ListMenu: "
             "NPC questions, script choices) come after the opening and are checked on the console "
             "(docs/hardware-test.md).", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage12(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build, screen

    info = build.build(root, rom_path, debug=True)
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        seen = 0
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline and not session.debug().menu_showing:
            if manual:
                time.sleep(0.3)
            else:
                session.press("A")
            head = session.header()
            while seen < head.seq:
                seen += 1
                message = session.message(seen)
                if message:
                    say(f"#{seen} {render(message.text).replace(chr(10), '/')[:50]}")
                if message and is_name_prompt(message.text) and not manual:
                    enter_name(session)
        d = session.debug()
        question = session.message(session.header().seq)
        check("a menu under a question is captured", d.menu_showing and d.menu_count >= 2,
              f"menus {d.menus_shown}, options {d.menu_count}, under "
              f"{render(question.text).replace(chr(10), '/')[:40] if question else '?'}")
        time.sleep(0.5)
        d = session.debug()
        check("each option's words are found", d.menu_count and d.menu_found == d.menu_count,
              f"{d.menu_found} of {d.menu_count} options")
        p = session.panel()
        check("the panel shows the options under the question", p.visible and p.unit_count >= d.menu_count,
              f"visible {p.visible}, units {p.unit_count}")
        images.append(("The question with its options", _illustrate(root, "stage12-choices.png", say)))
        session.tap(*OPTION_TAP)
        time.sleep(0.3)
        p = session.panel()
        check("an option's word can be looked up", p.selected and p.card_status == capture.CARD_OK and p.sel_start > 0,
              f"selected {p.selected} at {p.sel_start}, card status {p.card_status}")
        images.append(("An option's word looked up", _illustrate(root, "stage12-option-word.png", say)))
        menus = session.debug().menus_shown
        session.press("A")
        gone = wait_for(lambda: not session.debug().menu_showing, 5)
        check("choosing hides the options", gone, f"showing {session.debug().menu_showing} after A (menus {menus})")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / "stage12-results.md").write_text(
            stage12_document(passed=passed, checks=checks, log=log, images=images))


def stage13_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 13 results: the peek", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage13`. Azahar in Old 3DS mode, debug build. SELECT is pressed through a debug "
             "switch (the emulator's buttons are not the driver's); that the payload finds the HID word real SELECT is "
             "read from is checked. The emulator cannot reach a battle from a new game in a test's time, so the battle "
             "content (moves, the battle's messages, the command labels) is checked on the console "
             "(docs/hardware-test.md); here the peek shows the field's latest message.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage13(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build, hooks, screen

    info = build.build(root, rom_path, debug=True)
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        next_dialog(session, 0, manual)
        d = session.debug()
        check("the HID word SELECT is read from is found", d.hid_addr != 0, f"at {d.hid_addr:#x}")
        flag_at = struct.unpack("<I", session.rpc.read(hooks.HID_SELECT_CALL + 8 + 0x24, 4))[0]
        flag = session.rpc.read(flag_at, 1)[0]
        check("SELECT is kept apart from START", flag == 1, f"nn::hid's select flag {flag}")
        session.select()
        p = session.panel()
        check("SELECT opens the peek", p.peek_open == 1 and p.visible == 1 and session.debug().peek_composes > 0,
              f"open {p.peek_open}, visible {p.visible}, composed {session.debug().peek_composes}")
        images.append(("The peek over a dialog", _illustrate(root, "stage13-peek.png", say)))
        seq = session.header().seq
        for _ in range(3):
            session.press("A")
        time.sleep(0.5)
        check("the game sees no buttons while it is open", session.header().seq == seq,
              f"messages {seq} -> {session.header().seq} after three A presses")
        session.select()
        check("SELECT again closes it", session.panel().peek_open == 0, f"open {session.panel().peek_open}")
        moved = False
        for _ in range(12):
            session.press("A")
            if session.header().seq != seq:
                moved = True
                break
        check("the game has its buttons back", moved, f"messages {seq} -> {session.header().seq}")
        d = session.debug()
        say(f"window messages seen {d.screen_msgs}, lower-screen texts {d.screen_texts}, known {d.screen_matched}")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / "stage13-results.md").write_text(
            stage13_document(passed=passed, checks=checks, log=log, images=images))


def stage15_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 15 results: the peek's rows", "", f"Result: **{'PASSED' if passed else 'FAILED'}**",
             "", "Written by `jpreader e2e stage15`. Azahar in Old 3DS mode, debug build. The test reaches the opening's "
             "photo prompt, whose はい/いいえ buttons are layout text on the upper screen, and opens the peek there "
             "(SELECT through a debug switch). Battles, the Bag and other menus are not reachable from a new game in a "
             "test's time; they are checked on the console (docs/hardware-test.md).", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)




def stage15(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build

    info = build.build(root, rom_path, debug=True)
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        seen = 0
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline and not session.debug().menu_showing:
            if manual:
                time.sleep(0.3)
            else:
                session.press("A")
            head = session.header()
            while seen < head.seq:
                seen += 1
                message = session.message(seen)
                if message and is_name_prompt(message.text) and not manual:
                    enter_name(session)
        check("the photo prompt's options are up", session.debug().menu_showing,
              f"options {session.debug().menu_count}")
        time.sleep(0.5)
        session.select()
        time.sleep(0.5)
        d, p = session.debug(), session.panel()
        rows = session.peek_rows()
        check("SELECT opens the peek", p.peek_open == 1 and p.visible == 1, f"open {p.peek_open}, visible {p.visible}")
        described = " | ".join(f"{r['japanese']} [{r['literal']}|{r['english']}|{r['meta']}|{r['pill']}]" for r in rows)
        check("the message heads the rows", rows and rows[0]["header"] and rows[0]["japanese"], described[:120])
        options = {r["japanese"]: r for r in rows[1:]}
        check("a row a button: はい and いいえ", {"はい", "いいえ"} <= set(options), described[:160])
        check("each with its English from strings.bin",
              options.get("はい", {}).get("english") == "Yes" and options.get("いいえ", {}).get("english") == "No",
              f"{d.screen_matched} known")
        check("their words can be tapped", p.unit_count > 0, f"units {p.unit_count}")
        images.append(("The peek's rows over the photo prompt", _illustrate(root, "stage15-rows.png", say)))
        hit = None
        for y in range(10, 200, 8):                     # the first word in the rows, wherever the layout put it
            session.tap(14, y)
            if session.panel().selected:
                hit = y
                break
        p = session.panel()
        check("a tapped word opens its card", hit is not None and p.selected and p.card_entry != 0xFFFFFFFF,
              f"y {hit}, unit {p.selected}, entry {p.card_entry}, card {p.card_status}")
        images.append(("A word's card over the lower rows", _illustrate(root, "stage15-card.png", say)))
        if hit is not None:
            session.tap(14, hit - p.scroll)          # the rows may have scrolled to show it above the card
        check("tapping it again closes the card", session.panel().selected == 0, f"unit {session.panel().selected}")
        session.select()
        check("SELECT again closes the peek", session.panel().peek_open == 0, f"open {session.panel().peek_open}")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / "stage15-results.md").write_text(
            stage15_document(passed=passed, checks=checks, log=log, images=images))


OPENING_CALL = 38                # system text file: Professor Kukui's video call, the first lines of a new game


def stage10_document(*, passed, checks, log, images) -> str:
    lines = ["# Stage 10 results: voice acting", "", f"Result: **{'PASSED' if passed else 'FAILED'}**", "",
             "Written by `jpreader e2e stage10`. Azahar in Old 3DS mode, debug build. A new game opens with "
             "Professor Kukui's video call, system text file 38: its first line must play its own system clip. "
             "The test cannot reach the story through character creation, so after that a debug switch makes each "
             "message stand in for story file 27's lines (0 and 1 alternately); which story line has which clip is "
             "checked by the host tests. "
             "Azahar's sound output itself is not checked: the figures are the DSP's own progress.", "",
             "## Checks", "", "| Check | Result | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {'pass' if ok else 'FAIL'} | {detail} |" for name, (ok, detail) in checks.items()]
    if images:
        lines += ["", "## Screenshots", ""] + [f"![{caption}]({path})" for caption, path in images if path]
    lines += ["", "## Log", "", "```", *log, "```", ""]
    return "\n".join(lines)


def stage10(root: Path, rom_path: Path, manual: bool) -> int:
    from . import build, screen

    build.build(root, rom_path, debug=True)
    info = json.loads((root / "build" / "build-info-debug.json").read_text())
    sdmc = emu.profile_dirs(root / "build" / "emu" / "profile")["data"] / "sdmc"
    shots = root / "build" / "shots"
    checks: dict[str, tuple[bool, str]] = {}
    log: list[str] = []
    images: list[tuple[str, str]] = []
    NONE = 0xFFFFFFFF

    def say(line: str) -> None:
        print(line, flush=True)
        log.append(line)

    def check(name: str, ok, detail: str) -> None:
        checks[name] = (bool(ok), detail)
        say(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    voice_dir = root / "dist" / "azahar-debug" / "sdmc" / "jpreader"

    def fed_from_part(session: Session, clip: int) -> tuple[int, int, int]:
        """(buffers of the ring that match the clip's own samples, of 4; its part; the parts in voice.bin)."""
        index = (voice_dir / "voice.bin").read_bytes()
        _f, _l, part, _z, at, total, _sa, _st = struct.unpack_from("<HHHHIIII", index, 20 + 24 * clip)
        with open(voice_dir / f"voice-{part}.bin", "rb") as fh:       # parts are up to 1 GB: read the clip only
            fh.seek(at)
            blob = fh.read(2 * total)
        ring, found = session.debug().voice_ring, 0
        for slot in range(4):
            data = session.rpc.read(ring + slot * 11040, 2 * 5512)
            chunks = [2 * 5512 * k for k in range((total + 5511) // 5512)]
            found += any(blob[c:c + len(data)] == data[:len(blob[c:c + len(data)])] and data[:16] != bytes(16)
                         for c in chunks)
        return found, part, struct.unpack_from("<I", index, 16)[0]

    passed = False
    try:
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        if not wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60):
            check("payload starts", False, "the debug block never appeared")
            return 1
        index = (voice_dir / "voice.bin").read_bytes()
        call, seen = None, 0
        while call is None and seen < 12:
            next_dialog(session, seen, manual)
            seen = session.header().seq
            message = session.message(seen)
            if message and message.ident.arc in (30, 31) and message.ident.dat == OPENING_CALL:
                call = message
        if call is None:
            check("the opening call's first line plays its system clip", False, f"no line of system file "
                  f"{OPENING_CALL} in the first {seen} messages")
        else:
            playing = wait_for(lambda: session.debug().voice_clip != NONE and session.debug().snd_active == 1, 3)
            d = session.debug()
            key = struct.unpack_from("<HH", index, 20 + 24 * d.voice_clip) if playing else None
            check("the opening call's first line plays its system clip",
                  playing and key == (0x8000 | OPENING_CALL, call.ident.line),
                  f"archive {call.ident.arc} file {call.ident.dat} line {call.ident.line}: clip {d.voice_clip}, "
                  f"voice.bin key {key and (hex(key[0]), key[1])}, DSP active {d.snd_active}")
            if playing:                     # system clips sort after every story clip: never in the first part
                time.sleep(0.3)               # of a many-part build (part 2 on is the same path, host tested)
                found, part, parts = fed_from_part(session, d.voice_clip)
                check("its samples come from a later voice part", found >= 2 and part >= 1,
                      f"{found} of 4 buffers match voice-{part}.bin byte for byte; voice.bin has {parts} parts")
            starts = d.voice_starts
            session.tap(*VOICE_TAP)
            replayed = wait_for(lambda: session.debug().voice_starts > starts, 3)
            check("▶ replays it", replayed and session.debug().voice_clip == d.voice_clip,
                  f"starts {starts} -> {session.debug().voice_starts}")
        session.voice_test(0)
        next_dialog(session, session.header().seq, manual)
        d = session.debug()
        check("voice.bin is read", d.voice_fault == 0, f"fault {d.voice_fault}")
        playing = wait_for(lambda: session.debug().voice_clip != NONE and session.debug().snd_active == 1, 3)
        first = session.debug()
        time.sleep(0.5)
        later = session.debug()
        check("a message's clip plays as its box opens", playing and later.voice_next > first.voice_next,
              f"clip {first.voice_clip}, samples read {first.voice_next} -> {later.voice_next} of {first.voice_total}, "
              f"DSP active {first.snd_active}")
        found, part, _parts = fed_from_part(session, session.debug().voice_clip)
        check("the DSP is fed the clip's own samples", found >= 2,
              f"ring at {session.debug().voice_ring:#x}: {found} of 4 buffers match voice-{part}.bin byte for byte")
        check("the panel offers the buttons", session.panel().voice_available == 1,
              f"voice_available {session.panel().voice_available}")
        images.append(("The bar: ▶ and 0.7x beside Grammar", _illustrate(root, "stage10-bar.png", say)))

        clip_before, starts = later.voice_clip, later.voice_starts
        next_dialog(session, session.header().seq, manual)
        switched = wait_for(lambda: session.debug().voice_starts > starts, 3)
        d = session.debug()
        check("the next message's clip replaces it", switched and d.voice_clip != clip_before,
              f"clip {clip_before} -> {d.voice_clip}, starts {starts} -> {d.voice_starts}")

        starts = session.debug().voice_starts
        time.sleep(1.0)
        session.tap(*VOICE_TAP)
        replayed = wait_for(lambda: session.debug().voice_starts > starts, 3)
        d = session.debug()
        check("▶ replays the clip from the start", replayed and d.voice_speed == 0 and d.voice_next < d.voice_total,
              f"starts {starts} -> {d.voice_starts}, speed {d.voice_speed}, read {d.voice_next}/{d.voice_total}")
        normal_total = d.voice_total

        starts = d.voice_starts
        session.tap(*SLOW_TAP)
        slowed = wait_for(lambda: session.debug().voice_starts > starts, 3)
        d = session.debug()
        ratio = d.voice_total / normal_total if normal_total else 0
        check("0.7x replays the slow version", slowed and d.voice_speed == 1 and 1.38 <= ratio <= 1.48,
              f"speed {d.voice_speed}, {d.voice_total} samples = {ratio:.2f}x the normal {normal_total}")
        playing = wait_for(lambda: session.debug().snd_active == 1, 2)
        check("the slow clip plays", playing and session.debug().voice_next > 0,
              f"DSP active {session.debug().snd_active}, read {session.debug().voice_next}")

        say("voice=0 is read with config.txt; it is read once a session, so this run restarts the game for it")
        emu.stop()
        session = Session(start(root, rom_path, root / "dist" / "azahar-debug"), info["symbols"])
        write_config(sdmc, voice=False)
        wait_for(lambda: session.debug().magic == capture.DBG_MAGIC, 60)
        session.voice_test(0)
        next_dialog(session, 0, manual)
        time.sleep(1.0)
        d = session.debug()
        check("voice=0 stops autoplay", d.voice_starts == 0 and session.panel().voice_available == 1,
              f"starts {d.voice_starts}, buttons offered {session.panel().voice_available}")
        session.tap(*VOICE_TAP)
        replayed = wait_for(lambda: session.debug().voice_starts == 1, 3)
        check("▶ still plays with voice=0", replayed, f"starts {session.debug().voice_starts}")
        passed = all(ok for ok, _detail in checks.values())
        return 0 if passed else 1
    except emu.EmuNotResponding as error:
        check("emulator", False, str(error))
        return 1
    finally:
        emu.stop()
        (root / "docs" / "stage10-results.md").write_text(
            stage10_document(passed=passed, checks=checks, log=log, images=images))
