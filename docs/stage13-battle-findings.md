# Stage 13 research: reading the battle screen in Ultra Moon (USA 1.0)

Confidence: **H** = read in the disassembly and consistent everywhere; **R** = also seen at runtime in Azahar;
**M** = inferred (strong evidence, not executed); **L** = guess.

Method (scratchpad `t/`): `cro.py` parses a CRO, relocates it at 0x6DD000 (the address FieldRo had at runtime; Battle
is only ever loaded while FieldRo is out) and resolves its named imports from `build/re/exports.json` and its
index/anonymous imports from `static.crs`; `sdis.py` disassembles code.bin or a relocated CRO and labels exports and
import veneers; `xref.py` lists `b`/`bl` callers of an address or of an import (by demangled name); `vtables.py` lists
every RTTI vtable in a CRO; `vt.py` names the class of a vtable slot. `sysdump.py` dumps the system text archives
30 (kana), 31 (kanji) and 32 (English). `Battle +0x…` addresses below assume base 0x6DD000 (relocated image); they
are only for understanding. **Every recommended hook is in code.bin.**

**Nothing here was seen at runtime (no R).** Reaching a battle from a new game is long, so I only tried a cheap check: boot the existing `dist/azahar-debug` build (no rebuild) and read the language screen's layout objects from the heap over the RPC memory channel (no breakpoints). Both attempts lost the emulator within a minute: the Azahar process was killed from outside, with no crash in its log, probably another session restarting the shared emulator. I stopped there and left nothing running. Where the heap is: earlier runs put gfl2 objects at 0x30xxxxxx (linear heap); 0x08000000 reads back as zeros.

## 0. Where the battle code lives

- `Battle.cro` (romfs root, 1.29 MB): text 0x6DD180–0x7D9974, rodata 0x7DA000, data 0x817D60, bss 0x819000 (at base
  0x6DD000). 1069 imports, 842 veneers, all resolved. It also imports from `Bag`, `WazaOshie`, `Object`,
  `SequencePlayer` CROs (anonymous imports). (H)
- A battle loads it from code.bin: `gfl2::ro::RoManager::LoadModule(this, AsyncFileManager*, "Battle.cro", heap,
  fix)` at **0x402258**, then `LoadModule(…"Object.cro"…)` 0x40227C, two `StartModule`s, and
  `gfl2::proc::Manager::CallProc` 0x4022DC (the battle proc). A second site (0x404248 area, capture tutorial) loads
  the same pair. Strings at 0x402330 / 0x40233C / 0x4042E0 / 0x4042EC. (H)
- Battle UI classes (RTTI in Battle.cro): `btl::app::BtlvUiMsgWin` (top-screen message window),
  `btl::app::BattleUIActSelectLowerDraw` (たたかう/バッグ/ポケモン/にげる), `BattleUISkillSelectLowerDraw` (4 moves,
  with `MegaButton`/`ZenryokuButton`/`UltraBurstButton`), `BattleUISkillStatusLowerDraw` (L+A move info),
  `BattleUITargetSelectLowerDraw`, `BattleUIPokeListLowerDraw` (party), `BattleUISelf/EnemyPokeStatusLowerDraw`,
  `BattleUIItemConfirmLowerDraw`, `BattleUIYesNoLowerDraw`, `BattleUIPokeEntryConfirmLowerDraw`, plus their
  `…Frame` controllers. The battle bag is `Bag.cro` (app::bag), shared with the field. All are `app::ui::UIView`s that
  draw through `app::util::G2DUtil` layouts. (H)

## 1. The battle message window (top screen)

### Who sets the text (H)

`btl::app::BtlvUiMsgWin` (vtable Battle +0x7E7270, 25 slots) is a UIView with its own G2DUtil message window:

- Create (vslot +0x54, Battle 0x7787E8): `UIView::Create2D`, text panes 0xC1/0xC3 in parts pane 0x60, then
  **`G2DUtil::CreateMessageWindow(dev, 0, tb 0xC1, tb 0xC3, 1, heap)`** (code.bin 0x3183E0) →
  a **`print::MessageWindow`** (a `gfl2::str::StrWin`). Delete (0x7789BC): `DeleteMsgCursor`, `DeleteMessageWindow`.
- Print (Battle **0x7400E8**, `(this, StrBuf *msg, ?, bool, float, …)`): `GetMsgWin()`, show parts 0x60, then
  `MessageWindow::CheckStringWidth`, `StrWin::Clear`, and **`msgwin->vtbl[6](msgwin, StrBuf*)`** (`ldr r2,[r0,#24];
  blx r2`). Speed/auto/finish mode follow.
- Callers: Battle 0x758090/0x758144 in **0x757FE0** (the "start message" command: `this = BattleViewSystem`, the text
  is built into the view system's work buffer `*(this+0x544)` by the BTL_STR builders 0x78D198 / 0x7562F0 / 0x78C6FC /
  0x78C764 and then passed as `*(this+0x4E8+0x5C)` = the same StrBuf), and 0x7562E4 (0x7562C4, prints a caller's
  StrBuf).
- Update (vslot +0x34, 0x778A10): `G2DUtil::PrintMessage`, pause/auto handling, and a countdown at `this+0x130` that
  hides parts pane 0x60 (the window) after the message has finished.

`print::MessageWindow` vtable (code.bin) **0x64AEDC**; slot 6 (+0x18, word at 0x64AEF4) = **0x3D047C**
(`push {r4,r5,r6,lr}`) → falls into 0x3D04A0 `(this, StrBuf*, lang)` → `StrWin::SetString` 0x35DFA0 (copies the text
into the window's own buffer). **The same slot is what `G2DUtil::SetMessage(const StrBuf*)` 0x316E40 calls**
(`ldr r0,[r0,#0x34]; ldr r2,[r0]; ldr r2,[r2,#24]; bx r2`), i.e. TalkWindow::SetMessage (0x2CFA2C …), the
CommonMessageWindow (`CommonMessageWindowManipulator::ShowMessage` 0x2D77FC → 0x2D7878, used by CommonMessageWindowView,
Bag, FieldMenu, Status …) and Battle. `StrWin::SetString` 0x35DFA0 has a second caller, `CheckStringWidth`
0x3D015C (a measuring pass), so hook slot 6, not StrWin. (H)

So **one hook at 0x3D047C (pre: r0 = MessageWindow\*, r1 = StrBuf\*) sees every G2DUtil message window's text**:
battle messages, app message windows (bag "どの ポケモンに つかう？" etc.) and TalkWindow (skip that one:
`msgwin == *(*(jpr_talk+TALK_G2D)+G2D_MSGWIN)`, since the talk hooks already handle it; the talk pre-hook runs before
G2DUtil::SetMessage, so `jpr_talk` is already set). Derive the address at build time as
`vtable_functions(code, MSGWIN_CLASS)[6]` and check its push.

### Do our tags apply? (H statically, not run)

The BTL_STR builders (Battle 0x78C4F8–0x78D2xx; state block at Battle bss 0x8219E8) own 6 `MsgData`s built with
`MsgData(GetMessageArcId(…), dat, heap, 1)` for system files **{17, 15, 13, 14, 19, 18}** (table Battle 0x7DF584),
a WordSet at `state+8`, and two `StrBuf(1600)` temporaries at `state+12/+16`. Every builder is
`MsgData::GetString(msgdata[k], line, tmp)` then **`WordSet::Expand(ws, dst, tmp)`** with `dst` = the caller's StrBuf
(e.g. 0x78CF04: GetString → Expand(r7, …)). Our existing hooks tag `tmp` with (arc, file, line) and copy the tag to
`dst`, and `dst` is exactly the StrBuf that reaches slot 6. So `ident_of(r1)` at the slot-6 hook gives
(30/31, file, line). The arc id passed is the normal system arc (kana 30 / kanji 31), so the English template is
arc 32, same file and line.

System files used by battle text (checked in arcs 30/32):

| file | lines | what | example |
|---|---|---|---|
| 17 | 375 | "std" messages | 17:78 `{010C}は　どうする？` "What will {…} do?"; 17:79 `{0100}は　どうする？`; 17:88 `こうかは　ばつぐんだ！` |
| 15 | 1950 | per-side "set" messages (×4 variants: plain / wild / opposing / totem) | 15:8–11 `…に　こうかは　ばつぐんだ！` |
| 13, 14 | 2916 | move-use messages ("Xの　Yこうげき！") | |
| 18 | 55 | UI/communication messages | |
| 19 | 729 | Z-move names | |
| 12 | 136 | **lower-screen battle UI (btl_app)**: 2 `{0107}` (move name), 9 `PP`, 12 `{0201}` (number), 18 `{0200}/{0200}`, 27 いれかえる, 41 どの ポケモンで たたかう？, 68/69 はい/いいえ, 73 にげる … | |
| 32 | 133 | layout strings: 3 バッグ, 4 たたかう, 5 ポケモン, 6 にげる, 7–10 こうかばつぐん/いまひとつ/こうかなし/こうかあり, 41–58 type names (caps, "NORMAL"), 103–131 Z-move names | |
| 112 | 18 | type names (ノーマル … フェアリー) | |
| 118 | 729 | **move names (wazaname)**, line = move id (1 はたく … ) | |
| 117 / 102 / 39 / 16 | | move / ability / item / field-effect descriptions (BtlvLoaderUiMessage 0x779E54) | |

Gaps where the tag is lost (fall back to "the last Expand output's ident", or to text matching):

- BtlvUiMsgWin::Print copies the message into a local `StrBuf(128)` (sp+0x130) when `0x7784DC` says so (the
  message starts with a tag containing 0xBD06 and has exactly one line break before the first 0xBE00/0xBE01): it
  replaces the first `\n` with a space and strips the leading tag (`SkipTagCode`) so a two-line message fits. The
  StrBuf that reaches slot 6 is then that local copy, untagged. (H)
- 0x757FE0: when `vtbl[0x130]` of the view system is true, the buffer is `SetStr(fixed wchar literal)` (a
  communication/wait message). (H)
- `trainer::TrainerMessage::GetString` (trainer lines in battle) is code.bin; it uses MsgData internally (M: tagged).

### Battle in progress / ended

- Start: `gfl2::ro::RoManager::LoadModule` **0x34CEC8** (push ✓; r2 = `const char *name`), as a *prepost* hook:
  name == "Battle.cro" → remember the returned `nn::ro::Module*` → `battle = 1`. (H)
- End: `gfl2::ro::RoManager::DisposeModule(Module*)` **0x34D104** (push ✓, r1 = module) == remembered → `battle = 0`.
  (H for the functions; M that every battle, including trainer battles, goes through 0x402258 — the only two
  `"Battle.cro"` loaders found are that one and the capture tutorial.)
- Cheaper alternative without new hooks: battle ⇔ a slot-6 message arrived for a msgwin that is not the talk window
  and the field's TalkWindow has not been used since. Use only as a fallback.
- Note: while Battle.cro is loaded the field CRO is gone; module bases change between battles. Never put GDB
  breakpoints or hooks in Battle.cro.

## 2. The bottom screen: how its text gets on screen

### All pane setters funnel into one function (H)

Every `G2DUtil::SetTextBoxPane*` / `SetTextBoxPaneStringExpand` / `SetTextBoxPaneNumber` overload and every
`UIView::SetTextboxPane*` overload ends in **`print::SetStringForLytTextBox`**:

| Entry | Address | First insn | Args | Callers |
|---|---|---|---|---|
| `print::SetStringForLytTextBox(TextBox*, const StrBuf*, MSGLANGID)` | 0x3D1598 | push ✓ | r0 tb, r1 StrBuf* | all G2DUtil StrBuf/msgId/Expand/Number overloads (`b` tail calls), UIView ones; Battle.cro calls it directly 22× (gauges: names, Lv) |
| (unnamed) `(TextBox*, const StrBuf*, ?, lang)` | 0x3D16F8 | push ✓ | | ListMenu (0x2D7FC4, 0x2D804C, 0x2D8948, 0x2D8B04) |
| `G2DUtil::SetTextBoxPaneString(TextBox*, const wchar_t*)` | 0x3D15C4 | push ✓ | | → 0x3D1618 |
| (entry, no push) `(TextBox*, const wchar_t*, lang)` | 0x3D1618 | `mov r3,r2` | | UIView::SetTextboxPaneNumber 0x2EE6F4, 0x2EE7B0, G2DUtil::SetTextBoxPaneString(u32,u32,wchar*) 0x318944 |
| **core `(TextBox *tb, const u16 *text, Font *opt, lang)`** | **0x3D1728** | **push {r4-r8,lr} ✓** | r0 tb, r1 text (0-terminated), r2 0 | only the three above (two `b`, one fall-through) |

0x3D1728 measures the text against the pane width (`tb+0x3C` = size.x), may shrink the font, then calls
`tb->vtbl[27]` = **`nw::lyt::TextBox::SetString(const wchar*, u16 dst, u16 len)` 0x2AA330** (vtable 0x644D08+0x6C),
which copies into the TextBox's own buffer. CROs surveyed (Battle, Bag, FieldMenu, Status, FieldRo) import only
these G2DUtil/UIView/print setters; none imports a TextBox setter. (H) Text a CRO writes with the TextBox virtuals
directly (StrWin's typewriter output, possibly others) and text baked into the layout file do **not** pass through
0x3D1728.

### How the battle menus set their text

- **Command menu** (`BattleUIActSelectLowerDraw`, Battle 0x77E130–0x77F51C): たたかう / バッグ / ポケモン are **not
  set by code**; only the run/back button is: 0x77E9B4 sets pane 22 of parts `+0x480` with
  `SetTextBoxPaneString(tb, 7)` or `RegisterWord(0, name)` + `SetTextBoxPaneStringExpand(tb, 6)` (file 12). The other
  labels come from the layout resource (language-specific layout archives, `AppUtilFileControlState(…, ArcLangType)`);
  the same strings are system file 32 lines 3–6. (H for the code; M that they are layout-baked)
- **Move buttons** (`BattleUISkillSelectLowerDraw` setup Battle 0x780244; 4 iterations): per move it builds a
  "waza button" object (size 0x13C, ctor 0x745A34) and a name/Z object (0xE8, 0x78A3C4), then
  - name: **0x789E44** `SetWazaName(btn, wazaNo)`: `btn+0x138 = wazaNo`; **`G2DUtil::SetRegisterWazaName(g2d, 0,
    wazaNo)`** (code.bin 0x3D2BF4 → `print::WordSet::RegisterWazaName` **0x3D2BFC**, which fetches **file 118 line
    wazaNo**) then **`SetTextBoxPaneStringExpand(g2d, tb=btn+0x118, 2)`** (file 12 line 2 = `{0107:0000}`); then
    `pml::wazadata::GetType(wazaNo)` and **`ReplacePaneTextureByWazaTypeIcon(type, picture)`** — the type is an
    **icon texture, not text**.
  - PP: **0x745614** `SetPP(btn, now, max)`: `G2DUtil::SetTextBoxPaneNumber(g2d, tb, 12, value, 2, …)` twice
    (tb `btn+0x108` = now, `btn+0x10C` = max; file 12 line 12 = `{0201}`), text colours by `GetWazaTextColor(now,max)`
    on `+0x108/+0x10C/+0x110/+0x114`; stores now/max at `btn+0x13A/+0x13B`.
  - "PP" label: 0x745704 ends with `SetTextBoxPaneString(tb=btn+0x124, 9)` (file 12 line 9).
  - Z-move name: 0x78A2F4 `RegisterWord(0, StrBuf)` + `Expand(tb+0xB8, 2)`; mega/ultra burst buttons are separate
    classes.
  - Per-move input record: `SkillSelectLowerDraw + 0xB0 + 24*i`: `+0 u16 waza`, `+4 u8 pp now`, `+5 u8 pp max`,
    `+8 u16 z-waza`, `+12 StrBuf* z-name`, `+16 type?`, `+20 s8 effectiveness icon`. (M for the field meanings)
- So for a move name the ident our tags give is (30/31, **12, 2**) — the template — not the move. The move id is
  only visible at `RegisterWazaName(ws, idx, wazaNo)`.
- Party list / status / item confirm / target select / yes-no / move info use the same G2DUtil calls
  (e.g. PokeList 0x77DA04: `SetRegisterWazaName` + `GetStringExpand`; SkillStatus 0x782490: `UIView::
  SetTextboxPaneMessage/Number` ×12). The Bag (Bag.cro) and Status.cro likewise. (H)

### nw::lyt object layout (code.bin, H)

| Object | Field | Evidence |
|---|---|---|
| `nw::lyt::Pane` | `+0x04` link node (in parent's child list), **`+0x0C` parent**, `+0x10` child count, **`+0x14` child list anchor** (node.next/prev; child = node − 4), `+0x3C` size.x (float), **`+0x44` flags: bit0 visible**, **`+0x45` alpha**, `+0x80` name (char[24]) | `Pane::AppendChild` 0x2A20FC, `~Pane` 0x2A30C8, `G2DUtil::Set/GetPaneVisible` 0x3175A0/0x4AAC74, `SetPaneAlpha` 0x3170EC, `FindPaneByName` 0x2A3384 |
| `nw::lyt::TextBox` (vtable **0x644D08**) | **`+0xA4` u16 \*text**, `+0xB4` font, **`+0xCC` u16 buffer length (chars, incl. 0)**, **`+0xCE` u16 text length**, `+0xD0` flags (bit2 dirty) | `TextBox::SetString` 0x2AA284/0x2AA330, `GetStringBufferLength` 0x4A1DF0 |
| `nw::lyt::Parts` (vtable 0x644B68) | **`+0xAC` Layout\* (the part's layout)** | Parts ctor 0x2A3410, `LytWk::GetTextBoxPane(Parts*,…)` 0x4B5F80 |
| `nw::lyt::Layout` (vtable 0x644C00) | **`+0x10` root pane** | `Layout::Draw` 0x2A787C checks it; lookups call `root->vtbl[0x2C]` = FindPaneByName(name, recursive) |
| `gfl2::lyt::LytWk` | **`+0x04` LytWkBase\*** | every LytWk method |
| `gfl2::lyt::LytWkBase` | **`+0x10` nw::lyt::Layout\*** (`LytWk::GetLayoutCore` 0x4B624C), `+0xF0` display (0 upper, 1 lower, 3 both — M), `+0xF4` resource, `+0x104` pane cache, `+0x110` drawing count (`IsDrawing` 0x4B68A4) | 0x4B62D0, 0x35BB58 |
| type check | `pane->vtbl[2]()` returns a TypeInfo\*; TypeInfo `+0` = base TypeInfo\*. **Pane 0x690560, TextBox 0x690568, Parts 0x69CCE0** | 0x4A1310 / 0x4A1DCC / 0x4A1418; the loop at 0x4B5FCC |
| `~TextBox` | complete 0x2AA850 (push ✓), deleting 0x2AA840 (push ✓, calls 0x2AA850) | vtable slots 0/1 |

### Which layouts are on screen this frame (H for the call, M for the display argument)

`gfl2::lyt::LayoutRenderPath` (vtable 0x648FF0) slot 3 = **Execute 0x358778**: for each queued entry
(`count = this+0x354`; `LytWk* = this+0x14+4i`, `display = this+0x114+4i`, `arg = this+0x214+4i`) it calls
**0x359400 `(LytWk *wk, u32 display, u32 arg)`** (push {r4,lr} ✓), which draws through 0x35BB58
`(LytWkBase*, display, arg)` and, if drawn, registers the LytWk with `DrawingLytWkManager` (0x358EE4). 0x35BB58 maps
display 2 → 0 and skips the LytWk unless `LytWkBase+0xF0` is 3 or equals the display — so `display` is 0 = upper,
1 = lower, 2 = upper right eye (M). **A pre hook at 0x359400 therefore lists, every frame, exactly the layouts the
game draws and on which screen.**

### Stage 14: the walk, checked (emulator)

The `lyt_draw` hook (0x359400, pre: r0 LytWk\*, r1 display) is the only caller path of 0x35BB58 (one call, from
`LayoutRenderPath::Execute` 0x358810), and **display 0 is the upper screen, 1 the lower** (0x35BB58 maps 2, the right
eye, to 0): at the opening's photo prompt the はい/いいえ buttons and the message window are layouts drawn on 0, the
lower screen only a background on 1 (screenshot). The pane offsets above all hold: root
`*(*(*(wk+4)+0x10)+0x10)`, children `+0x14`, visible `+0x44` bit 0, alpha `+0x45`, TextBox vtable 0x644D08 with text
`+0xA4` / length `+0xCE`, Parts vtable 0x644B68 with its layout at `+0xAC`. Vtables are compared directly (no call
into the game). A hidden pane hides its subtree (the language screen's 「１２３４」 placeholders sit under hidden
panes).

On the console the stage 13 setter hooks (move names through RegisterWazaName/Expand/0x3D1728, Battle.cro by name)
showed no moves and no command labels, so stage 14 drops them (waza_name, lyt_text, textbox_dtor, ro_load,
ro_dispose) and reads every screen's text from the walk instead; a string's origin comes from strings.bin (an exact
match on the text without tags and spaces over every short system text line), which gives its English line, its
words, and for file 118 (moves) the move id, so its type.

## 3. Input

### SELECT is free everywhere (H)

`gfl2::ui::CTR_Button` vtable 0x648858, slot 3 = poll **0x4B3E8C** (exit `bx lr` **0x4B4000**; this is the
existing debug-only `pad` hook). It builds the game's button word from the HID hold word `**(u32**)(this+8)`:

| HID bit | 0x20 ← | 0x10 → | 0x40 ↑ | 0x80 ↓ | 0x1 A | 0x2 B | 0x400 X | 0x800 Y | 0x200 L | 0x100 R | 0x8 START | 0x4 SELECT | 0x4000/0x8000 ZL/ZR |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| game bit | 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x1000 | **— (never copied)** | — |

(+ circle pad as D-pad bits 0x10000000–0x80000000 → 1/2/4/8 when `this+12` is set.) **SELECT never reaches the
game**, so no CRO, field menu or key-assign (`Button::AddButtonAssignment`, `ConfigSave::SetButtonMode`) can react to
it. It is the right toggle in battle, on the field and in every app. (ZL/ZR and the C-stick are also unused, but
only exist on New 3DS.)

Two CTR_Button devices exist (ctor 0x34D70C called at 0x34DF6C with flag 0 and 0x34DFB0 with flag 1 = circle pad as
D-pad); both get `this+4 = owner+0x40`, **`this+8 = owner+0x80`**, so they read the same HID hold word. (H)

**On a console SELECT arrives as START** (found on hardware: holding either gave hold word 0x8). nn::hid's pad
readers (`ReadLatest` 0x179EB8, `Read` 0x179FE8) end with `if (!*(u8 *)0x69F4E0) fold(status)`; the fold 0x282354
moves SELECT (0x4) into START (0x8) in hold, trigger and release. The byte is the SDK's "select enabled" flag (in
.data; only these two readers refer to it, nothing sets it). The emulator test never saw this because its SELECT was a
debug switch, not the HID word. The peek sets the byte to 1 at every button poll: SELECT then stays 0x4, which the
game's poll never copies, and START is START alone. `hooks.hid_select_flag` reads the address from the reader's
literal pool and checks both functions' words. (H, console)

### Reading it in release builds

Make the `pad` exit hook (0x4B4000) part of release builds too (it already exists for debug) with a new handler:

- At the exit, `regs[1]` = the game's button word (`u32*`). For the flag-0 device the path to the exit never
  touches r0, so `regs[0]` = `this`; for the flag-1 device r0 is overwritten with the button word (0x4B3FE8). So:
  `if (regs[0] in 0x30000000..0x40000000 && *(u32*)regs[0] == 0x648858) hid = *(u32**)(regs[0] + 8);` — cache that
  pointer the first time and read `*hid & 4` from then on. Edge-detect SELECT (press, not hold). (M: r0 at exit)
- While the peek is open: `*(u32*)regs[1] = 0` (the game sees no buttons; the game's own trigger/repeat logic sees a
  release, so nothing fires when the peek closes), zero the D-pad vector at the CrossKey poll exit (`regs[1]` → 2
  floats) and the circle pad at the Stick poll exit (`regs[5]` → 2 floats) — the debug `cross`/`stick` hooks already
  have those sites — and keep hiding touches (`jpr_touch_hide(&gate, down, visible)` already hides every touch that
  starts while the panel is visible; the peek should count as visible).
- The peek's own navigation (scrolling, closing) reads `*hid` directly.

## 4. Structured data (fallback)

- Move type / PP without a text table: `pml::wazadata::GetType(WazaNo)` **0x3266F8**, `GetMaxPP(WazaNo, u8)` 0x326790,
  `GetPower` 0x326868, `GetCategory` 0x32633C. They read an in-memory table (`*(void**)0x690728` + 40·waza, copied
  into a one-entry cache by 0x326B00) — no file access, safe to call from a hook on the game thread. (H)
- The waza button object holds `u16 waza +0x138`, `u8 pp now +0x13A`, `u8 pp max +0x13B`; the SkillSelect view holds
  the 4 per-move records at `+0xB0`. Getting those object pointers needs a Battle.cro hook or a vtable match at a
  runtime-dependent address, so they are not recommended.
- Battle message: the tag (file, line) plus the English template (arc 32, same file/line) is the structured form.
  The battle's own Pokémon/move structures (btl::BTL_POKEPARAM …) were not analysed (not cheap).

## 5. Recommended hooks and data flow

All in code.bin, all `push` (pre/prepost) except the existing exit hooks.

| Name | Address | Kind | Args | Handler |
|---|---|---|---|---|
| `msgwin_set` | **0x3D047C** (= `vtable_functions(code, "N5print13MessageWindowE")[6]`) | pre | r0 MessageWindow\*, r1 StrBuf\* | Skip if it is the talk window's msgwin. Else record `{msgwin, ident_of(r1) (fallback: last Expand ident), text copy}` as the **current window message** (seq++), and remember `msgwin`. |
| `lyt_text` | **0x3D1728** | pre | r0 TextBox\*, r1 `const u16*` text | `tb_table[tb] = {ident, hash(text), waza}`: ident = the `buf_slots` entry whose StrBuf's `*(u16**)(buf+4) == r1` (pointer compare only), else unknown; waza = that slot's pending move id (below). No text copy needed. |
| `textbox_dtor` | **0x2AA850** (`~TextBox`, complete) | pre | r0 TextBox\* | remove from `tb_table` |
| `lyt_draw` | **0x359400** | pre | r0 LytWk\*, r1 display | append `(wk, display)` to `drawn[cur]` (dedupe; ~10–40 per frame); `jpr_h_present` flips `cur`. |
| `waza_name` | **0x3D2BFC** (`print::WordSet::RegisterWazaName`) | pre | r0 WordSet\*, r1 idx, r2 waza | `pending = {ws, waza}`. In the existing **expand** hook: if `wordset == pending.ws`, store `waza` on `dst`'s buf slot and clear `pending`. |
| `ro_load` | **0x34CEC8** | prepost | r2 name | name == "Battle.cro" → `battle_module = return value`, `battle = 1` |
| `ro_dispose` | **0x34D104** | pre | r1 module | == `battle_module` → `battle = 0`, clear the window message |
| `pad` (release too) | exit 0x4B4000 | exit | regs | SELECT edge → toggle peek; while open zero `*regs[1]` |
| `cross`, `stick` (release too) | existing exit sites | exit | regs | while open zero the vectors |

Peek build (on SELECT, then each frame while open; on the game thread, e.g. in `jpr_h_present`):

1. **Top**: the current window message (text + ident → English template). Optional: the drawn upper-screen layouts'
   text (Pokémon names, levels from the gauges) by the same walk.
2. **Bottom**: for each `(wk, display==1)` in last frame's `drawn` list: `root = *(*(*(wk+4)+0x10)+0x10)`;
   depth-first over children (`+0x14` list, child = node − 4), skipping any pane with `!(+0x44 & 1)` or alpha
   `+0x45 == 0` and its subtree; for a Parts pane also walk `*( *(parts+0xAC) + 0x10)` if it is not the parts pane
   itself and not visited. For each TextBox (TypeInfo chain contains 0x690568): text = `*(u16**)(tb+0xA4)`,
   length = `*(u16*)(tb+0xCE)`; skip empty. Look up `tb_table[tb]`; use its ident/waza only if its hash still matches
   the current text. Group by the nearest Parts ancestor (one group per button: name, "PP", "35", "/", "35").
3. **Translate** each string: ident → English line (arc 32, same file/line) for templates; waza → English move name
   (arc 32 file 118 line waza) and type (`pml::wazadata::GetType` → file 112 / arc 32 file 112, or file 32 41–58 caps);
   untagged text (layout-baked たたかう/バッグ/ポケモン/にげる, Z names, species, items) → a build-time exact-match
   table JP string → (file, line) over the system text name files (both arcs 30 and 31) with the English line.
   That one table also serves the bag, party and summary screens.

Why the walk and not only setter capture: the command labels are layout-baked (never set by code), visibility and
"which view is up" come for free from pane flags and the draw list, a re-set pane simply has new text, and
destroyed layouts drop out of the draw list the same frame. The setter hook only adds origin (ident, move id).

Limits / to verify at runtime (first battle; breakpoints only at the code.bin addresses above):

- `lyt_draw` r1 values and that one LayoutRenderPath runs per display (M).
- `regs[0] == this` at the pad exit for the flag-0 device (M).
- Parts sub-layout root vs the parts pane (M).
- Text in panes can still contain GF tags (0x10 …) after `SetStringForLytTextBox`? (0x3D1728 passes the StrBuf text
  through unchanged except for width fitting — M); the panel's renderer already handles tags.
- Animations that move/scale panes off-screen without hiding them would still be listed (L risk).
- The message window's own TextBoxes (0xC1/0xC3 in parts 0x60) hold the typewriter's partial text: exclude the
  msgwin's layout from the top-screen walk, use `msgwin_set` instead.

