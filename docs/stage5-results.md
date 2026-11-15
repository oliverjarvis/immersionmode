# Stage 5 results: jpreader on a 3DS

Tested 2026-10-11 on an Old 3DS (EUR, firmware 11.17.0-50E, Luma3DS 13.4, boot9strap) with
Pokémon Ultra Moon USA v1.0 installed as a CIA, following `docs/hardware-test.md`, with the
release build and the debug line on.

| Item | Result | Notes |
|---|---|---|
| 1. Boot | pass | after the stub fix below |
| 2. Kana mode | pass | text matches the top box, no glitches, paging works |
| 3. Words | pass, one gap | cards work; a card shows only the senses that fit its area, and cannot scroll to the rest (捕まえる shows one of three) |
| 4. Speed | to improve | `draw` over 4 ms (target about 2 ms); the game felt normal |
| 5. Grammar, Wi-Fi on | pass | reply in about 10 s; Back and Grammar again in the same conversation is instant; a new conversation asks again |
| 6. Grammar failures | pass | wireless switch off: "No Wi-Fi connection", Retry works; wrong key and clock covered by the emulator checks |
| 7. Dialog closing | pass | the Grammar view stays until Back, as designed |
| 8. Kanji mode | pass | |
| 9. Ten minutes of play | pass | battle, Pokémon Center, menu, HOME, lid |
| Festival Plaza Internet | pass | reaches Nintendo's closed server (002-0110) |

## What the console found

- **Boot crash.** On a 3DS, `svcControlProcessMemory` refuses the current-process pseudo-handle
  (0xD9001BF7). The stub now takes a real handle from `svcDuplicateHandle`.
- **No Internet from inside the game.** Grammar's connections were refused (ac C9209C64, error
  003-4199), and with jpreader installed so were the game's own (Festival Plaza 003-1099). Found by
  bisecting the patch on the console, with Festival Plaza's Internet option as the probe:
  - the exheader changes, the payload's memory and the text hooks were innocent;
  - the drawing hook's `svcFlushProcessDataCache` on the bottom-screen buffer, every frame, broke
    every connection the game made. It is gone; the panel draws correctly without it.
  - while the game holds ndm in local wireless mode (from the point Festival Plaza is unlocked),
    the 3DS refuses every Internet connection from the game's process. A one-instruction edit
    stops the game's local wireless from starting; link trades and battles with people nearby
    are off while jpreader is installed.
- The emulator showed none of these: it implements neither the handle check, nor caches, nor ndm's
  refusal.

## Left for the next stage

- Word cards scroll when their senses do not fit.
- Drawing time on the console down towards 2 ms.
- Grammar replies remembered across conversations (kept on the SD card).
