# Stage 12 research: choice menus in Ultra Moon (USA 1.0)

Confidence: **H** = read in the disassembly and consistent everywhere; **R** = also seen at runtime in Azahar;
**M** = inferred (strong evidence, not executed); **L** = guess.

Tools used (scratchpad `ch/`): `cro.py` parses and relocates a CRO and resolves its imports from `static.crs`;
`ven.py` writes the relocated `FieldRo.img` and labels calls to import veneers; `sdis.py` and `sxref.py` disassemble and
cross-reference `code.bin` with export names; `menutrace.py` and `tracer.py` are GDB-stub tracers (copied from the
speakers session's `rsp.py`). The CROs were pulled from the romfs root into `scratchpad/cro/`.

## 0. Where the code lives

- Field and event-script code is **not** in `code.bin`. It is in **`FieldRo.cro`** (romfs root, 1.1 MB). In both
  runtime sessions (the speakers session's dump and this one), FieldRo loaded at **0x6DD000**: runtime address =
  0x6DD000 + CRO file offset. The `FieldRo 0x…` addresses below assume that base. CRO addresses are only for
  understanding; **every recommended hook is in `code.bin`** (static, so no CRO swap problems).
- FieldRo calls static code through `ldr pc,[pc,#-4]; .word addr` veneers, patched from FieldRo's named-import table.
  FieldRo imports **all of `App::Tool::ListMenu`** (ctor, AddItem, Start, DeleteMenu, IsInstanceDelete, GetSelectId).
  It imports none of `MessageMenuView` or `MenuWindow`. **No other CRO, and nothing in `code.bin`, calls
  `ListMenu::AddItem`** (checked: an import survey of all 130 CROs, plus a `bl` scan of `code.bin`). (H)
- Names: FieldRo RTTI has `Field::FieldWindow::FieldYesNoWindow` and `Field::FieldWindow::FieldListMenuWindow`.
  Its native table (`{char *name, fn}` pairs at FieldRo 0x7ACxxx) gives the native names. (H)

## 1. Field choice menus (event scripts)

### Script natives (H for the code; M for the hash↔name pairing, which comes from argument shapes, since the hash function wasn't found)

| Native name (FieldRo table) | Handler (FieldRo) | Script hash | Script args (the `params[]` that are read) |
|---|---|---|---|
| `ListMenuInit_Seq` | 0x6EC398 | **0x75321ba3** | x, y, ?, cancelable, ?, use-memory-MsgData. It is always called through library proc `ec976cde9b`, e.g. (180, 72, 0, 1, 0, 0). |
| `ListMenuAdd` | 0x6E82BC | **0x7903fd3e** | params[1] = **message line**, params[2] = **item id** (the value returned when it is picked) |
| `ListMenuStart_Seq` | 0x6ED5B4 | **0x5cb69d67** | none. Result → temp work **0x8010**, 0xFFFF on cancel. Proc `8775d93121` loops on it, then reads 0x8010 with native 8e50f765. |
| `YesNoWin_Seq` | 0x6E8E64 | **0x0511b06e** | params[1] = use the window's text file (1) or story file 10 (0); params[2], params[3] = the two message lines; params[4] = default cursor (<2); params[5] = use memory MsgData. Result → work 0x8010 (cancel → 1 = "No"). Proc `f8f69d9b82` (= speakers' `TWO_CHOICES`) calls it with flag 1 and returns `!result`. |
| `DebugListMenuAdd_` | 0x6ECAE8 | – | debug only |

The script pattern (from the AMX in a/0/9/2, e.g. script 9) is: message proc `dc89edd33f(msg,…)` (the question, in the
TalkWindow) → `fb51a321fa(1)` → `ListMenuInit` → N × `ListMenuAdd(line, id, 0)` → `ListMenuStart` → `b5a9195fea(0)`.
**No window-close call comes between the question and the menu.** (H for the sequence; M that `b5a9195fea` is the close.)

### Class A: `FieldYesNoWindow` (two items: はい/いいえ or two script lines). Vtable FieldRo 0x7B1FA0.

- Ctor **FieldRo 0x6F6404**, called from `YesNoWin_Seq` state 0 (0x6E8FB0). It builds its own
  `MsgData(arc, dat, heap, 0)` (or `MsgData(void*)`), two `StrBuf(55)`, then: (H)
  - `GetString(msg, line0, sbA)` → **`ListMenu::AddItem(menu, 0, sbA)`**
  - `GetString(msg, line1, sbB)` → **`ListMenu::AddItem(menu, 1, sbB)`**
  - `ListMenu::Start(menu)`, `UIView::AddSubView(parent, menu)`. It then deletes sbA, sbB and the MsgData
    straight away. (ListMenu has copied the strings by then.)
  - **The StrBuf passed to AddItem is exactly the buffer `GetString` filled**, with no WordSet. Our GetString hook has
    already tagged it with (arc, dat, line).
  - ListMenu* is stored at `this+0x30`.
- Text source: with flag 1, the window's text file. That is `msgwin+0x250/+0x254` (the one-shot override), else
  `+0x24/+0x28` (the zone/event default), the same file the question came from. With flag 0, it is
  `print::GetMessageArcId_Script(lang)` = **story arc 40/41, file 10**: line 0 "Yes" (はい), line 1 "No" (いいえ),
  checked in build/script/story.jsonl. (H)
- `YesNoWin_Seq` state machine (`u16` at `work+4`): 0 create → 2 wait for `ListMenu::IsInstanceDelete` (the ListMenu's
  own close animation has finished), then `GetSelectId` → work 0x8010, vfunc +0x3C(0), `DeleteMenu` → 3 wait for
  `IsInstanceDelete` → delete the FieldYesNoWindow (deleting dtor FieldRo 0x6F6768: RemoveFromSuperView, DeleteMenu,
  virtual delete of the ListMenu).

### Class B: `FieldListMenuWindow` (2–128 script choices). Vtable FieldRo 0x7B1FC0, size 0x460.

- Ctor **FieldRo 0x6F6B80** (from `ListMenuInit_Seq`, 0x6EC47C): it builds its MsgData from the window's text file
  (same rule as above) at `+0x28`. Then `ListMenuInit_Seq` stores the script WordSet at **`+0x45C`**. (H)
- `AddEntry` **FieldRo 0x6F6A24** (from `ListMenuAdd`): `items[n] = {StrBuf(55) at +0x58+8n, id at +0x5C+8n}`,
  n = `+0x458`, max 128. (H)
  - With a WordSet (the normal case): `GetString(msg, line, temp)` then **`WordSet::Expand(items[n].buf, temp)`**, then
    temp is deleted.
  - Without one: `GetString(msg, line, items[n].buf)`.
  - Either way **our existing hooks tag `items[n].buf`**: the GetString hook tags temp, and the Expand hook copies
    temp's ident to dst. **So the buffer AddItem later receives is the Expand output, not the GetString buffer, but
    `ident_of()` still resolves it.** (H; at runtime M)
- `Show` **FieldRo 0x6F68C8** (from `ListMenuStart_Seq` state 0): `new ListMenu(PARAM)` at `+0x24`, then for each item
  **`ListMenu::AddItem(menu, items[i].id, items[i].buf)`**, then `ListMenu::Start`, then AddSubView. (H)
- `ListMenuStart_Seq` state 1 waits for `IsInstanceDelete`, then `GetSelectId` (−2 → 0xFFFF) → work 0x8010, then
  `DeleteMenu` → state 2 waits for `IsInstanceDelete` → deletes the window (dtor FieldRo 0x6F6CAC/0x6F6D60 frees the item
  StrBufs and MsgData, RemoveFromSuperView, DeleteMenu, virtual delete of the ListMenu). (H)
- The AddEntry → Show gap spans several script frames. In practice only the question's GetString runs in between, and
  the menu runs ~1–10 entries, well within the payload's 32 `buf_slots` (M). Taking the ident at AddItem time (below)
  is the robust choice anyway.

## 2. `App::Tool::ListMenu` (code.bin) — layout and lifecycle (H)

ListMenu is an `app::ui::UIView` subclass: its ctor calls `UIView::UIView`. Vtable 0x645B70; complete dtor 0x2D93C0,
deleting dtor 0x2D92F4 (vt+0x18); Update/state machine 0x2D8CF8 (vt+0x34); input 0x2D8204.

| Function | Address | First insn | Args |
|---|---|---|---|
| `ListMenu::ListMenu(const PARAM*)` | 0x2D90EC | push ✓ | r0 this, r1 PARAM* |
| **`ListMenu::AddItem(u32 id, StrBuf*)`** | **0x2D8EB0** | push ✓ | **r0 this, r1 id, r2 source StrBuf\*** |
| **`ListMenu::Start()`** | **0x2D8C30** | push ✓ | r0 this |
| `ListMenu::DeleteMenu()` | 0x2D7E38 | `add` ✗ (4 insns, `bx lr` at 0x2D7E44) | r0 this; sets state = 4 |
| `ListMenu::IsInstanceDelete()` | 0x2D8828 | ✗ | state == 6 |
| `ListMenu::GetSelectId() const` | 0x4A8920 | ✗ | −2 if cancelled, else `items[cursor+scroll]->id` |
| `UIView::~UIView` (D2) | 0x2EEEA4 | already hooked (`view_dtor`) | every ListMenu dies through it |

Object fields:

- `+0x14C + 4*i` → item `{u32 id; StrBuf *copy}`. AddItem does `new StrBuf(src, heap)`, a **copy**. The copy is
  untagged, so take the ident from the **source** StrBuf at the AddItem pre-hook.
- `+0x354` u32 item count (AddItem's index = count before the call); `+0x95` u8 max items.
- `+0x34C` u32 cursor row on screen, `+0x350` u32 scroll offset (highlighted item = cursor + scroll; 4 rows visible).
- **`+0x35C` u16 state**: 0 built, 1 Start called (loading), 2 opening, **3 on screen and taking input**,
  4 close requested (the player picked or cancelled, or DeleteMenu), 5 close animation, **6 gone**
  (IsInstanceDelete).
- `+0x360` s32 result: −1 = picked (A), −2 = cancelled (B, only when PARAM allows); written by input handler 0x2D8204.

**On screen now vs gone.** The menu shows from `Start` until state reaches 4. It closes **by itself** after A/B
(state 3→4→5→6) before the script calls `DeleteMenu`. Then the field calls `DeleteMenu`, waits for state 6 again, and
destroys the object (`~UIView`).

## 3. `gfl2::str::StrBuf` (H, matches payload `game.h`)

`+0x00` vtable, **`+0x04` u16 \*text**, `+0x08` u16 capacity (units), **`+0x0A` u16 length (units)**, `+0x0C` u8 valid.
Checked against `StrBuf::StrBuf(u32, heap)` 0x35CCD4 and `SetStr` 0x35CAC4. Same as `STRBUF_PTR` 0x04 / `STRBUF_LEN`
0x0A in `payload/src/platform/game.h`.

## 4. Other menu kinds

| Where | Path | Notes |
|---|---|---|
| Field scripts: NPC questions, Poké Mart counter, starter/most story choices, はい/いいえ | **ListMenu** (above) | H |
| App menus: Bag (use/give/toss + confirm), Box/PC, Shop app's buy/sell confirm, FieldMenu, Finder, **HeroSelect** (opening "Is this OK?" and name confirm), TitleMenu, TrainerPhoto, WazaOmoidasi (move reminder) | **`app::tool::MenuWindow`** (code.bin) | H; R for HeroSelect |
| Config, Zukan, Kisekae (clothes shop), BattleVideo, MysteryGift, NetApp/Festival Plaza | `App::Tool::MessageMenuView` → its `AddItem`s and `ShowMessageWithListMenu` call **MenuWindow::AddItem** (0x2D42D8, 0x2D4310, 0x2D4328, 0x2D3B34…) | H. Their question text goes through CommonMessageWindow, **not TalkWindow**, so it isn't captured today. |
| Battle: "Switch Pokémon?", nickname after a catch, "Use next Pokémon?" | `btl::BattleUIYesNoFrame` / `btl::app::BattleUIYesNoLowerDraw` in Battle.cro: **lower-screen touch buttons**, text set with `G2DUtil::SetTextBoxPaneString*(TextBox*, msgId)` | M. A different path, not ListMenu or MenuWindow; battle messages are not TalkWindow either. |

`app::tool::MenuWindow` (code.bin, H):

- `AddItem(MsgData*, u32 line, bool, bool, u32)` **0x2F1580** (push ✓): does `GetString(msg, line, items[n].buf)`
  directly into the window's own buffer, so the GetString hook tags it.
- `AddItem(const StrBuf&, bool, bool, u32)` **0x2F1610** (push ✓): `SetStr` copy; the ident must be taken from r1
  (the source).
- Items: `+0xD8 + 16*i` StrBuf\* (persistent); count u8 `+0x158` (max 8).
- `StartMenu(u32, PutType, f, f)` 0x2F19AC (push ✓), reached through `StartYesNoMenu` 0x2F1188.
  `StartMenuDisplay(u32, PutType, int, int)` 0x2F11A0 (push ✓) is a separate entry: HeroSelect used only this one.
- `EndMenu(bool clear)` 0x2F1694 (push ✓): clears the shown flag `+0xC2`; with `clear`, count = 0.
- Destroyed through `~UIView`.

## 5. Runtime evidence (Azahar, debug build from dist/azahar-debug, GDB stub breakpoints)

New game from the language screen through Kukui's video call (system arc 30, file 38) and character creation:

```
TalkWindow::End (Kukui's window) … seq=15 "どの しゃしんが いいか おしえて くれるかな？"
MenuWindow::AddItem(MsgData) this=301f4df8 msg=30102bb8 id=15   (HeroSelect, lr in its CRO)
MenuWindow::AddItem(MsgData) this=301f4df8 msg=30102bb8 id=16
TalkWindow::CallMsgWin this=301dbd0c        → seq=16 "えらんだので いいよね？" (arc -1 = memory MsgData, line 7)
MenuWindow::StartMenuDisplay this=301f4df8  cap[seq=16 open=1]   ← question window still open under the menu
MenuWindow::EndMenu this=301f4df8 clear=0   cap[seq=16 open=1]
TalkWindow::CallMsgWin → seq=17 …   … TalkWindow::End … UIView::~UIView this=301f4df8 (the MenuWindow)
(same again for the name confirm: seq=18 "か で いいかい？", StartMenuDisplay with open=1)
```

- **Verified (R):** the MenuWindow path; that the question's TalkWindow **stays open** (`jpr_cap.open = 1`, no End)
  while the menu shows; that `EndMenu` comes before the window's End; that the menu dies via `~UIView`; and the
  FieldRo base 0x6DD000 (talk calls from FieldRo 0x6E8248).
- **Not reached at runtime:** a **field ListMenu**. The opening has no field choice before the new-house cutscene.
  The run then stalled in the bedroom scene and the emulator exited. I suspect a GDB temporary breakpoint at a CRO
  return address was left armed while modules swapped. This is the hazard the speakers notes warn about; the tracer
  now guards only static addresses, and the LR temp breakpoint is still unguarded. So the ListMenu items/ident flow,
  the state values and "TalkWindow stays open during a field ListMenu" are **H statically / unverified at runtime**.
  To check them: a save just before any NPC with a question (e.g. a Poké Mart counter), with breakpoints only at the
  code.bin ListMenu addresses (all static), and no temp breakpoint at LR inside a CRO.

## 6. Recommended payload hooks

All in code.bin, all start with `push` (fits `make_site(..., "pre", ...)` in `tools/jpreader_tools/hooks.py`). Exports
from `build/re/exports.json`:

| Name | Symbol | Addr | Kind | Handler args (pre trampoline: r0–r3) | Record |
|---|---|---|---|---|---|
| `listmenu_add` | `_ZN3App4Tool8ListMenu7AddItemEjPN4gfl23str6StrBufE` | 0x2D8EB0 | pre | `(ListMenu *m, u32 id, const StrBuf *src)` | If `m` is not the current pending menu, start a new one (`m`, count 0). Append `{id, ident_of(src), text copy from *(u16**)(src+4), len *(u16*)(src+0xA)}`. Index = `*(u32*)(m+0x354)` before the call (or our own counter). |
| `listmenu_start` | `_ZN3App4Tool8ListMenu5StartEv` | 0x2D8C30 | pre | `(ListMenu *m)` | Commit: menu `m` is now on screen. Publish to a `jpr_menu` block: seq, kind = list, count, items. Pair it with the current question = latest `jpr_cap` message while `jpr_cap.open`. |
| `menuwin_add_msg` | `_ZN3app4tool10MenuWindow7AddItemEPN4gfl23str7MsgDataEjbbj` | 0x2F1580 | prepost (or skip it and read at start) | `(MenuWindow *w, MsgData *msg, u32 line)` | Optional: the item buffer is `*(StrBuf**)(w+0xD8+16*n)` (n = u8 `w+0x158`). The GetString hook already tags it, so reading at start is enough. |
| `menuwin_add_buf` | `_ZN3app4tool10MenuWindow7AddItemERKN4gfl23str6StrBufEbbj` | 0x2F1610 | pre | `(MenuWindow *w, const StrBuf *src)` | `jpr_cap_copy_ident(item buf n, src)`, i.e. copy the ident onto the window's own buffer, as the Expand hook does. |
| `menuwin_start` | `_ZN3app4tool10MenuWindow9StartMenuEjNS1_7PutTypeEff` | 0x2F19AC | pre | `(MenuWindow *w)` | Publish: count = u8 `w+0x158`; item i = `*(StrBuf**)(w+0xD8+16*i)` with its text and `ident_of`. |
| `menuwin_start_disp` | `_ZN3app4tool10MenuWindow16StartMenuDisplayEjNS1_7PutTypeEii` | 0x2F11A0 | pre | same | same (separate entry; HeroSelect uses it) |
| `menuwin_end` | `_ZN3app4tool10MenuWindow7EndMenuEb` | 0x2F1694 | pre | `(MenuWindow *w, bool clear)` | Menu `w` gone. |
| *(existing)* `view_dtor` | `_ZN3app2ui6UIViewD2Ev` | 0x2EEEA4 | pre | `(UIView *v)` | Also: if `v` is the current ListMenu or MenuWindow → menu gone. **This is required for safety**: it stops the payload from reading a freed object. |

ListMenu "closing" needs no hook: each frame (e.g. in `jpr_h_present`), while a ListMenu is current, read
`u16 *(m+0x35C)`. Values 1–3 mean show the options; ≥4 means the player has chosen, so hide or grey them. Optionally
highlight `cursor = *(u32*)(m+0x34C) + *(u32*)(m+0x350)` as the player moves. It's safe because `view_dtor` clears the
pointer before the memory is freed. (`DeleteMenu` and `GetSelectId` don't start with `push`; if wanted,
`DeleteMenu` can be an `exit` hook at its `bx lr`, 0x2D7E44, but polling makes that unnecessary.)

What the bottom screen gets per option: the text (already WordSet-expanded for script menus; raw for はい/いいえ),
the item id, and the ident `(arc, dat, line, hdr_total, hdr_lines)`:

- story choices → arc 40/41, the event or zone's story file, the line from ListMenuAdd;
- はい/いいえ → arc 40/41 file 10 lines 0/1 (or the script's own two lines);
- memory-MsgData menus (HeroSelect etc.) → arc −1, resolved by header like today.

Capacity: ListMenu menus can have up to 128 entries (4 visible, scrolling). Script 9 has menus of 8 and 10. Keep
~16–32 option slots and a "more" flag.

Caveats:

1. `buf_slots` is 32 entries. Taking the ident at `listmenu_add` (not at Start) keeps the lookup next to the
   Expand/GetString that produced it.
2. App menus (MessageMenuView/CommonMessageWindow, the Shop app, Bag) show their question outside TalkWindow, so
   the panel has their options but no question until CommonMessageWindow is hooked
   (`CommonMessageWindowManipulator::ShowMessage(const StrBuf*)` 0x2D77FC is a candidate; not analysed).
3. Battle yes/no is a different system on the lower screen.
