# Game sound volumes in Pokémon Ultra Moon USA (code at 0x100000)

Source: a static reverse-engineering pass (code.bin, the romfs sound archive `data/sound/niji_sound.bcsar`, and the CRO import tables), 2026-10-13. Nothing here was run. Confidence is high unless marked.

## Summary

- The game's sound sits in three volume layers, and they multiply:
  1. the **player** (nw::snd SoundPlayer `m_Volume`, at player+0x3C);
  2. the **handle** (BasicSound "ext" volume, a MoveValue at sound+0xB8, which fades);
  3. the sound's own initial volume.
- `ChangeBGMPlayerVolume` writes layer 1. `ChangeBGMVolume`, `ChangeSEVolume` and `ChangeVoiceVolume` write layer 2. They are **separate multipliers**.
- There are 12 nw::snd players:
  - **P0 and P1 are the BGM players.** P0 holds every stream, music effects (MEs) included; P1 is a second BGM player.
  - **P2 to P10 are sound effects.**
  - **P11 plays every Pokémon cry.**
  - So "music" is {P0, P1} and "sound effects, cries included" is {P2 … P11}.
- The game writes player volumes rarely: the field "trainer focus" dim, a field menu, two script commands, and a "care" routine that puts P6 back to 1.0. Every one of those writes, and every fade step, goes through **one function, 0x3BA324**. The fade start value is read back through **one function, 0x4B7A90**.
- **Recommendation:** replace-hook those two functions with a tiny assembly wrapper around C, keep a "game value" shadow per player, and write `shadow × our gain` into player+0x3C. Fades, trainer focus and the P6 care routine then keep working on the game's own values, and our gain rides on top.

## Key addresses (VA, ARM, hard-float: float arguments arrive in s0)

| What | Address | Notes |
|---|---|---|
| Sound critical section (gfl, recursive) | object **0x6A53A8**; `0x3BD038()` returns it | Enter 0x1076A8 / Leave 0x107730; the owner and a count are kept at +4 and +8 |
| Sound system pointer | `*(void**)0x6915A4` ("sys") | sys+4 = inner gfl SoundSystem ("inner") |
| SoundArchivePlayer | inner + 0x53C ("SAP") | |
| `GetSoundPlayer(SAP, itemId)` | 0x2AF364 | `*(SAP+0x1C) + (itemId & 0xFFFFFF) * 0x5C` |
| SoundPlayer `m_Volume` | player + **0x3C** (float) | the setter clamps it to ≥ 0 and does not cap it above |
| **Game "set player volume"** `void (sys, u32 label, float v)` | **0x3BA324** | label → itemId via 0x3BCB1C, then tail-calls 0x2AD0E8(inner, itemId, v), which writes player+0x3C. **Only 2 callers: 0x3BB324 (fade step), 0x3BD014 (immediate set).** |
| **Game "get player volume"** `float (sys, u32 label)` | **0x4B7A90** | falls through into 0x4B7AB4 (inner, itemId), which returns player+0x3C. **Only 2 callers: 0x3BCFCC (fade start value), 0x3BC2E0 (the wrapper 0x3BC2BC, used by CareFieldBGMState).** |
| Game label → nw::snd itemId | 0x3BCB1C | integer only; table at 0x6013F8 is {count, u32* ids}[24], indexed by label bits 16–23 |
| Player-volume fade table | **0x6A53B4**, 11 × 24 bytes | see below |
| Per-player fade/set `void (u32 idx, u32 frames, float v)` | **0x3BCF68** | |
| Fade step, all 11 entries | 0x3BB288 | called only from Sound::Update |
| Sound::Update | 0x3BDFAC | runs on **Sound::SoundUpdateThread** (see Threads) |
| BasicSound::UpdateParam (volume product) | 0x2B2F20 | (medium) reads `player->+0x3C` at 0x2B2F60 |
| BasicSound::SetVolume(vol, frames), the handle volume | 0x2B3BAC | MoveValue at sound+0xB8 |
| nn::snd Voice::SetVolume(float) | 0x26091C | `voice+0x64 = v; impl=*(voice+0x68); impl+0x68 = v; *(u16*)(impl+0x6C) \|= 1` |
| nn::snd SetMasterVolume(float) | 0x25CAD4 | Called once at init (0x2BCB10), and by MovieLib.cro. Scales **every** nn::snd voice, ours included. |

### Fade table at 0x6A53B4

Built at 0x3BCA10–0x3BCAB0.

```c
struct PlayerVol {   // 24 bytes, 11 entries
  u32 label;         // +0x00 game player label (0x3BCB1C maps it to a nw::snd itemId)
  u32 fading;        // +0x04
  u32 frame;         // +0x08 counter
  u32 frames;        // +0x0C fade length
  float from;        // +0x10 read through 0x4B7A90 when a fade starts
  float to;          // +0x14
};
```

The table does **not** hold the current value. An immediate set (frames = 0) never writes `to`.

| idx | label | nw::snd player | What the archive puts on it (romfs `niji_sound.bcsar`) |
|---|---|---|---|
| 0 | 0x30000 | P0 0x04000000 | all 282 streams: 229 BGM + 51 ME (category 1 and 4 labels) + 2 others. 1 sound at a time. |
| 1 | 0x30001 | P1 0x04000001 | nothing by default. The second BGM player: StopPlayer and IsBGMPlaying both cover 0x30000 and 0x30001 (0x3B8C20–0x3B8C34, 0x4BBFE0). |
| 2–5 | 0x80000–0x80003 | P2–P5 | sound-effect sequences (509 / 733 / 415 / 217), 1 sound each |
| 6 | 0x80004 | P6 | 127 sound-effect sequences, max 3. **CareFieldBGMState puts it back to 1.0** (below). |
| 7 | 0x80005 | P7 | nothing by default, max 4 (presumably a start-info override, such as 3D actors) (low) |
| 8 | 0x80006 | P8 | 418 sound-effect sequences, max 6 |
| — | (0x80007) | P9 | nothing by default, max 4; **not in the table**, so the game never sets it |
| 10 | 0x80008 | P10 | 8 sequences, max 1 |
| 9 | 0x150000 | **P11** 0x0400000B | **Pokémon cries.** The 12 cry slots (built at 0x3BC780, config at 0x5D9944) play labels 0x120000+i, which are sounds 0x1000A95–0x1000AA0, all 12 of them on P11. Each slot swaps in wave archive 0x140000+i (0x05000000+i) for the cry being played. |

The archive has no string block, so the player names are lost. The roles above come from the sound → player table and the code.

### 0x3BCF68 (`idx`, `frames`, `v`): what it does

```
3bcf84  bl 0x1076a8              ; enter CS 0x6a53a8
3bcf94  r4 = 0x6a53b4 + idx*24 ; r7 = entry.label
3bcf98  cmp frames, #0 ; beq immediate
; fade:  entry.frames = frames; entry.fading = 1; entry.frame = 0
3bcfcc  bl 0x4b7a90 (sys, label) ; s17 = current volume
3bcfdc  vstr s17, [r4,#16]       ; from
3bcfe0  vstr s16, [r4,#20]       ; to
; immediate:
3bd014  bl 0x3ba324 (sys, label, v) ; entry.fading = 0
```

Each Sound::Update, the fade step at 0x3BB2A8 does this for each of the 11 entries: if `fading` and `frame < frames`, then `frame++`, `v = from + (to - from) * frame/frames`, `0x3BA324(sys, label, v)`; when the fade finishes, `fading = 0`.

So **yes, it is a linear fade over `frames` Sound::Update ticks** (a tick is about 16 ms; see Threads).

### What the volume of a sound is made of

BasicSound::UpdateParam, 0x2B2F20 (medium):

```
2b2f54  vldr s1,[r4,#200] ; vmul s0, s1, s0        ; initial volume × const
2b2f58  ldr  r0,[r4,#16]                           ; BasicSound->m_pPlayer
2b2f60  vldr s1,[r0,#60] ; vmul s16, s1, s0        ; × player->m_Volume
2b2f6c  bl 0x5ad5b8 (sound+0xB8) ; vmul            ; × handle volume (SetVolume/fade, 0x2B3BAC)
        ... × MoveValues at +0x68, +0x78, +0x88, × +0x30, × +0x58
```

The player volume therefore multiplies every sound on that player, and is independent of the handle volume that ChangeBGMVolume, ChangeSEVolume and ChangeVoiceVolume set.

### The handle-volume API (not usable as a global knob)

| Function | Address | Path |
|---|---|---|
| Sound::ChangeBGMVolume(float, u32 frames) | 0x3BC128 | 0x3BA258, then a BGM handle via 0x4BC190, then 0x3654B4, then BasicSound::SetVolume(v, frames) (0x2B3BAC) |
| Field::FieldSound::ChangeBGMVolume | 0x3BC120 | `mov r0, r1`, then falls into the function above |
| Sound::ChangeSEVolume(u32 se, float, u32 frames, int player) | 0x3BB39C | for each of 11 SE slots whose sound is `se` (0x3BC33C): handle = 0x3BB554(slot), which is 4..14, then 0x3BA228, then 0x3654B4 |
| Sound::ChangeVoiceVolume(u8 slot, float, u32 frames) | 0x3BC934 | cry slot `*(0x6A54E8)[slot]` (0..11): stores the volume at +0x20 and frames at +0x34, then sets dirty bits at +0x1C. Applied later to handles 15..26. |

The handle indices are 0..3 for BGM (0x3BA258), 4..14 for sound effects (0x3BA228 range check), and 15..26 for cries (0x3BA240).

All of these are per-sound and the game drives them (battle, cutscenes, Pokémon Refresh, the Dex and others call them; see the import table below). They are **not** a global knob, and anything we wrote there would be overwritten.

## 1. Who sets player volumes, and when

Two sources were used: BL scans of code.bin, and CRO import names (CROs reach static code only through the exports in static.crs).

| Caller | Writes | When |
|---|---|---|
| Field::FieldSound::SetTrainerFocusMode(float, FadeFrame) 0x37D4D0 | ChangeBGMPlayerVolume(v, frames), so P0 and P1 | from FieldRo.cro (trainer spotted / focus) |
| Field::FieldSound::ResetTrainerFocusMode(FadeFrame) 0x37D540 | P0 and P1 back to 1.0 over the given frames | FieldRo.cro; static 0x39CE54 |
| 0x37D564 (reset) | P0 and P1 to 1.0 immediately | static 0x39AF0C, 0x3A235C |
| Field event state machine, 0x3916FC and 0x3917A0 | P0 and P1 to 0.5 or 0.7 over 30 ticks, later back to 1.0 over 30 ticks | static (field) |
| FieldMenu.cro | imports ChangeBGMPlayerVolume | the field menu (low: the call site was not read) |
| **Field::FieldSound::CareFieldBGMState 0x37D1E0** | **if P6 is not fading and abs(vol − 1.0) > 1e-6: P6 back to 1.0 immediately** | FieldRo.cro (a field "care" pass) |
| Script command handler 0x389B88 | P7 to `v` over `frames` (from script arguments) | event scripts |
| Script command handler 0x38AB50 | P10 to `v` over `frames` | event scripts |
| Fade step 0x3BB288 | any entry that is fading, every tick | SoundUpdateThread |

- **Would the game overwrite a value we poke into player+0x3C?**
  - Yes, at every event above. P6 is put back to 1.0 whenever CareFieldBGMState runs.
  - Worse, while a fade is running the game rewrites the player on every tick, and the volume product is computed on that same thread straight afterwards. A once-per-frame "re-apply" from the main thread therefore always loses: every fade tick would be heard unscaled.
  - **Hooking is required.**
- **Is the writer list complete?** (medium-high)
  - 0x3BA324 → 0x2AD0E8 is the only path that writes. 0x2AD0E8 has exactly 1 caller, and GetSoundPlayer has 4 callers, all of them accounted for.
  - static.crs exports no nw::snd SoundPlayer function, so CROs cannot get around this path.
  - nw::snd itself sets `m_Volume = 1.0` only when the players are set up.

## 2. One overall sound-effects volume

- **Use the per-player volume of P2 to P11**, writing player+0x3C as shown below. This covers every sound-effect sequence and every cry, and touches no BGM or ME.
  - P9 is never set by the game, so a plain write there sticks.
- There is no cleaner global knob:
  - nn::snd SetMasterVolume (0x25CAD4) scales everything, our own voice included, and MovieLib.cro writes it during movies.
  - nw::snd has no per-category bus here. Every sound uses the main bus; aux buses are effects sends.
- The music, ME jingles included, is P0 and P1, and the same mechanism covers it.

## 3. Threads

- **The game's sound logic runs on its own thread.**
  - Sound::SoundUpdateThread (RTTI at 0x639AE2; vtable slot 0x64ABC8 → 0x3BDF80) loops `Sound::Update(); Sleep(16 ms)` (0x367404 → svcSleepThread).
  - Sound::Update (0x3BDFAC) holds the CS 0x6A53A8 while it does three things:
    1. the BGM request queues;
    2. the player-volume fade step (0x3BB288);
    3. 0x3BB370 → 0x3BAA94 → 0x2B18A8, the nw::snd archive-player update that runs UpdateParam (medium).
- Every exported `Sound::*` function takes the same CS (through 0x3BD038 → 0x1076A8), so main-thread callers and the update thread are serialised on it. **The CS is recursive** (0x1076A8 checks the TLS thread id against +4 and counts at +8).
- What the payload has to do:
  - **From the main thread** (the display-swap hook): enter CS 0x6A53A8 → write → leave. Calling game Sound functions is also fine; they take the CS themselves.
  - **The hooks themselves** run inside the CS already, on whichever thread the game called from. They must take no locks.
  - **Never take the CS from the nn::snd sound thread, or while holding nn::snd Lock (0x25CAC0).** (medium) The update thread holds the CS while it runs nw::snd, which talks to the nn::snd/driver layer, so taking the CS there would invert the lock order. Do the mixer work on the main thread only.
  - Our own voice (nn::snd) keeps the stage 10 rule: Voice calls go between Lock 0x25CAC0 and Unlock 0x25D418.

## 4. Recommended payload API

The payload is built with `-mfloat-abi=soft`, and the game is hard-float. So:

- floats are passed to the game, and read back, as bit patterns, through a 4-line assembly wrapper;
- C reads and writes player+0x3C as a plain float in memory, which is fine;
- soft-float C never touches VFP registers, so the game's s0–s31 survive our hooks.

### Hooks

Two `replace` sites, in the style of `dsp_address`:

```python
make_site("player_vol_set", "replace", 0x3BA324, code, ("jpr_h_player_vol_set_asm",)),
make_site("player_vol_get", "replace", 0x4B7A90, code, ("jpr_h_player_vol_get_asm",)),
```

Check these words first, as snd.py does:

- 0x3BA324: `e92d4010 e1a04000 ed2d8b02 eeb08a40`
- 0x4B7A90: `e92d4010 e1a04000 e1a00001 ebfc141e`

```asm
    .fpu vfpv2
jpr_h_player_vol_set_asm:          @ (sys, label, s0 = v)
    vmov  r2, s0
    b     jpr_h_player_vol_set     @ void (void *sys, u32 label, u32 vbits)
jpr_h_player_vol_get_asm:          @ (sys, label) -> s0
    push  {r4, lr}
    bl    jpr_h_player_vol_get     @ u32 (void *sys, u32 label)
    vmov  s0, r0
    pop   {r4, pc}
@ for calling float-taking game functions, e.g. our own Voice::SetVolume (0x26091C):
jpr_call_f32:                      @ void (void *fn, u32 a0, u32 a1, u32 fbits)
    mov   ip, r0
    mov   r0, r1
    mov   r1, r2
    vmov  s0, r3
    bx    ip
```

### C side (sketch)

```c
#define SND_SYS     (*(u8 *volatile *)0x6915A4)
#define SND_CS      ((void *)0x6A53A8)          /* = 0x3BD038() */
#define LABEL2ID    0x3BCB1C                    /* u32 (u32 label) */
#define N_PLAYERS   12

static float shadow[N_PLAYERS] = {1,1,1,1,1,1,1,1,1,1,1,1};   /* what the game asked for */
static float gain[N_PLAYERS]   = {1,1,1,1,1,1,1,1,1,1,1,1};   /* ours: P0,P1 = bgm*duck; P2..P11 = se */

static float *player_vol(u32 i) {                /* NULL until nw::snd is set up */
    u8 *sys = SND_SYS, *inner, *players;
    if (!sys || !(inner = *(u8 **)(sys + 4))) return 0;
    players = *(u8 **)(inner + 0x53C + 0x1C);
    return players ? (float *)(players + i * 0x5C + 0x3C) : 0;
}
static void write(u32 i) { float *p = player_vol(i); float v = shadow[i] * gain[i];
                           if (p) *p = v < 0 ? 0 : v; }

void jpr_h_player_vol_set(void *sys, u32 label, u32 vbits) {      /* game write; CS held */
    u32 id = CALL(u32 (*)(u32), LABEL2ID)(label), i = id & 0xFFFFFF;
    float v; memcpy(&v, &vbits, 4);
    if ((id >> 24) != 4 || i >= N_PLAYERS) { /* unknown: do what the original did */
        u8 *p = *(u8 **)(*(u8 **)((u8 *)sys + 4) + 0x53C + 0x1C) + i * 0x5C;
        *(float *)(p + 0x3C) = v < 0 ? 0 : v; return; }
    shadow[i] = v; write(i);
}
u32 jpr_h_player_vol_get(void *sys, u32 label) {                   /* CS held */
    u32 i = CALL(u32 (*)(u32), LABEL2ID)(label) & 0xFFFFFF; float v = i < N_PLAYERS ? shadow[i] : 1.0f;
    u32 b; memcpy(&b, &v, 4); return b;
}
/* main thread only (the present hook) */
void jpr_mix_apply(void) {
    CALL(void (*)(void *), 0x1076A8)(SND_CS);
    for (u32 i = 0; i < N_PLAYERS; i++) write(i);
    CALL(void (*)(void *), 0x107730)(SND_CS);
}
```

### Public API

- `jpr_mix_set(bgm, se, voice)` sets `gain[0..1] = bgm * duck` and `gain[2..11] = se`, then calls `jpr_mix_apply()`.
  - That is 12 float stores under one CS enter and leave: cheap enough to do every frame while anything is moving.
- The voice gain goes on our nn::snd voice at clip start, under nn::snd Lock: `jpr_call_f32((void*)0x26091C, voice, 0, bits(voice_gain))`. Alternatively write the three fields of Voice::SetVolume directly, as listed in the key-address table.
  - nn::snd master volume stays at the game's value. MovieLib changes it only during movies.
- **Ducking:**
  - keep `duck` in [0.3, 1];
  - when a clip starts, ramp it down over about 6–8 frames; after the clip ends, ramp it back over about 15–20 frames;
  - re-apply each frame of the ramp from the present hook.
  - Ramp rather than step: a player-volume change reaches the voices at the next UpdateParam with no smoothing, so a step clicks. (medium)
- **Persistence:** none is needed beyond the hooks.
  - Every game write lands in `shadow` and comes out as `shadow × gain`.
  - Fades start from the game's own value (the getter hook), so there is no double scaling.
  - CareFieldBGMState sees P6 at the game's 1.0 and leaves it alone.
- Before the sound system exists, `player_vol()` returns NULL and `shadow` stays at 1.0. That matches nw::snd's initial player volume, so the hooks are safe from boot.

### Not recommended

| Option | Why not |
|---|---|
| Poke player+0x3C each frame, with no hooks | loses to every game fade tick (an unscaled blip each ~16 ms), and the P6 care routine fights it |
| Call ChangeBGMPlayerVolume / 0x3BCF68 ourselves | it shares the fade table with the game, and the game's next set overwrites it |
| ChangeBGMVolume / ChangeSEVolume / ChangeVoiceVolume | per-handle; the game uses them constantly |
| nn::snd SetMasterVolume | global, scales our voice too, and MovieLib writes it |
| Patch the read at 0x2B2F60 (`vldr s1,[r0,#60]` in UpdateParam) to multiply by a gain looked up from the player pointer | No shadow is needed and game values are never touched. But it is a mid-function VFP patch, and other sound types may read player volume elsewhere (not checked). Keep it as a fallback. |

## Open items (for the console or emulator)

- Check which caller in FieldMenu.cro dims the BGM, and P7's real user (3D actors?). Neither changes the design.
- Confirm by ear that the cries follow P11 and the sound effects follow P2–P10. A quick test is to set gain[11] = 0 and play a cry in the Dex.
