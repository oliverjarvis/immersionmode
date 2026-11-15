# jpreader on a 3DS: setup and test checklist

This is stage 5: trying jpreader on the real console. Everything here has passed in the Azahar
emulator; the console can still differ in Wi-Fi, timing and memory, and this checklist is how we
find out. Tick each item, and for anything that fails send back what the last section asks for.

## You need

- An Old 3DS or 2DS with Luma3DS 13.x (the version shows at the top of Luma's settings menu).
- Pokémon Ultra Moon on the flashcard: the USA cartridge image, version 1.0, **no update installed**.
- Wi-Fi set up in System Settings → Internet Settings (for the Grammar button).
- The SD card from the 3DS, and this project on the Mac.

## Setup

1. Turn the 3DS off and take out its SD card. On the Mac, build the release version (this also writes kokoro's URL and your device token into `config.txt`; run `jpreader kokoro-link` first if you have not):

   ```
   .venv/bin/jpreader build
   ```

2. Put the 3DS's SD card in the Mac and copy jpreader onto it, with the debug line turned on for
   testing. Use the card's name as it shows in Finder, and keep the quotes: cards are often
   called "NO NAME" or "Untitled".

   ```
   .venv/bin/jpreader sd "/Volumes/<card name>" --debug-line
   ```

   It copies two folders: `luma/titles/00040000001B5100/` (the patch) and `jpreader/` (dictionary,
   font, settings). It refuses if the folder is not a Luma SD card or if there is no build.

3. Eject the card in Finder and put it back in the 3DS while it is off.
4. Turn Luma's game patching on, if it is not already: hold **SELECT** while switching the 3DS on,
   tick **"Enable game patching"**, and press **START** to save.
5. Start the game from the flashcard as usual.

To go back to the stock game at any time, delete `luma/titles/00040000001B5100` from the card.

## The debug line

With `--debug-line`, the bottom row of the word card shows something like:

```
mem 3412K 41/22/7 err 0 draw 1.5ms
```

- `mem`: free memory left for the game. Write down the lowest you see.
- `41/22/7`: which line of the game's script is on screen (`mem/-/3` for a few menu-built messages).
- `err`: the last Grammar failure code (0 when there has been none).
- `draw`: how long the bottom screen took to draw in the last frame.

## Checklist

1. **Boot.** The game starts and plays to the first dialog as usual: no crash, no black screen,
   no slowdown.
2. **Kana mode.** When someone talks, the bottom screen shows the same text in black on cream.
   - It moves on with the top box, block by block.
   - Nothing flickers, tears or shows garbage, even while the top screen animates.
   - ◀ ▶ page back and forward within a long message, and the counter matches.
3. **Words.** Words are underlined. The text takes only the lines it needs, and the card fills the
   rest of the screen down to the buttons.
   - Tapping one highlights it and shows a card panel: the dictionary form in bold, its reading in
     grey, the part of speech on a grey pill, and numbered meanings. A conjugated word shows its
     ending on a yellow chip, for example "in text しました → polite past".
   - When the meanings do not fit, a small ▼ shows at the card's bottom right and dragging inside the
     card scrolls it.
   - Dragging the text scrolls a box that has more than four lines.
   - While the panel is up, tapping does nothing in the game.
4. **Speed.** Note `mem` and `draw` from the debug line. Pass: `draw` stays under about 2 ms, and
   the game feels as smooth as without the patch.
5. **Grammar, Wi-Fi on.** Tap **Grammar**.
   - "Asking…" appears, then within about 15 s the reply: the translation in italics in a box with
     an orange bar, then one block per part of the sentence (the Japanese in bold, its meaning, and
     the grammar in grey italics), every other block shaded.
   - **Back** returns to the text, and Grammar again on the same sentence shows the same reply at
     once.
   - Leave the conversation, talk to the same person again and tap Grammar on the same line: the
     reply comes back at once, without "Asking…". Replies are kept in `jpreader/replies.bin`
     (up to 256; deleting the file forgets them).
   - Note roughly how many seconds the reply took.
6. **Grammar failures.** Each one should show its message and a Retry button:
   - Wireless switched off with the switch on the side of the console: "No Wi-Fi connection".
   - A wrong token: edit `jpreader/config.txt` on the card so `kokoro_token=` ends in a different
     letter. Expect "Token rejected: run jpreader kokoro-link". Put the right token back afterwards.
   - The console's date set to 2000: "Could not verify kokoro (check the console's date)".
     Close the game first (System Settings cannot open while it runs), change the date, start the
     game again, test, then set the date back. Changing the clock locks the game's daily events for
     about 24 hours; it does no other harm, so skip this test if that matters to you.
7. **Dialog closing.** When a conversation ends, the normal bottom screen (the Rotom Dex) comes back
   and taps work in the game again. Open Grammar, keep pressing A until the conversation ends, then
   tap Back: the panel goes away and the Rotom Dex does not react to that tap.
8. **Kanji mode.** Switch the game's text to kanji in the game's options, if it offers that, and
   repeat items 2 and 3. If kanji can only be chosen when starting a new game, **back up your save
   first**: the game has a single save, and a new game replaces it. If you would rather not, skip
   this item; kanji mode passed in the emulator.
9. **Ten minutes of play.** Include a battle, a Pokémon Center, the menu, the HOME menu and closing
   the lid.
   - Nothing behaves differently from the stock game.
   - Closing the lid or pressing HOME during a Grammar request may end it with an error message;
     Retry then works.
   - Festival Plaza's Internet option: after a Grammar reply, it should still reach Nintendo's
     server, which has closed: expect error 002-0110. An error starting 003- means it could not
     connect at all.
   - jpreader turns off the game's local wireless (link trades and battles with people nearby).
     A 3DS refuses every Internet connection from the game while the game holds the radio for
     local wireless, and that includes Grammar's. Removing jpreader brings local wireless back.
10. **Removal.** Delete `luma/titles/00040000001B5100`: the game is stock again.

### Stage 8B: kokoro words

These need `jpreader kokoro-link` to have been run once on the Mac. `jpreader build` and
`jpreader sd <card>` then copy `ids.bin` and a `config.txt` that has `kokoro_url` and
`kokoro_token`. (Since stage 9C the list is by JMdict id; items 13 and 14 read as below.)

11. **Sync.** Start a conversation with the wireless switch on. Within about ten seconds, words you
    know in kokoro are underlined green and words you are learning amber; the rest stay grey.
    Afterwards `jpreader/kokoro.bin` is on the card, and the colours show from the start of the next
    session even before it syncs.
12. **A known word.** Tap a green word: the card's last row says "Known", with no button.
13. **Adding a word.** Tap a word with a grey underline that has a dictionary entry. The last row
    says "Not in kokoro", with a green "+ Add to kokoro" button. Tap the button: "Adding…", then
    "Added: up next", and the row says "Up next in kokoro". On the phone, kokoro's "My words" shows the
    word after its next sync, with the game's sentence.
14. **Adding offline.** Turn the wireless switch off and add another word: "Saved: will send later".
    Turn wireless on and add a third word. Both words reach kokoro, and the saved word's row then
    says "Up next in kokoro".
15. **Unchanged.** The debug line's `mem` stays above 3 MB, Grammar still answers, and the Festival
    Plaza still reaches Nintendo's server (error 002-0110).

### Stage 10: voice acting

`voice.bin` and `voice-0.bin` (about 125 MB together) are copied by `jpreader sd` with the rest of `jpreader/`;
later runs skip voice parts already on the card.

16. **A clip plays.** Start a new game. At the first scene in the player's house, Mom's first line
    is heard as its box appears, over the game's music. There is no crackle, and the music keeps
    playing.
17. **A message's later boxes.** Press A through the boxes of one line. The clip carries on and does
    not start again.
18. **Replay.** Tap ▶ in the bottom bar: the line plays again from the start. Tap 0.7x: it plays
    slower, at the same pitch.
19. **Skipping ahead.** Press A quickly through several voiced lines. Each line's clip stops the one
    before: never two voices at once.
20. **Off.** Put `voice=0` in `jpreader/config.txt` and restart the game. Lines are silent; ▶ still
    plays them.
21. **No clip.** On a line without a clip (most of the game, for now), ▶ and 0.7x are greyed out.

### Stage 9C: words by identity

Needs kokoro deployed with stage 9C (it is) and the phone app rebuilt from kokoro's main.

22. **A kana word kokoro has in kanji.** If kokoro has それ, ちょっと or する from Kaishi (any spelling),
    the game's それ / ちょっと / する are underlined in its colour.
23. **An unmet Kaishi word.** Tap a grey word you have not met in Kaishi yet (most of the 1.5k), then
    "+ Add to kokoro": "Added: up next". No new card appears in My words; on the phone, the next study
    session starts its new cards with that Kaishi card.
24. **A word kokoro lacks.** Add one: "Added: up next". On the phone it is in My words, first among
    new cards, and within a minute or so plays a native recording (if JapanesePod101 has one).
25. **A word you know.** A green word has no button. (A word you set aside in kokoro is green too.)

### Stage 11: the sound mixer

The speaker button sits in the bottom bar between the page arrows and ▶, while someone is talking. The levels are
kept in `jpreader/sound.txt` on the card; `jpreader sd` never overwrites it. A card without one starts at Music 6,
Effects 10, Voice 10, with the music quieter under a voice.

26. **Quieter music.** With no `sound.txt`, the game's music is clearly quieter than before (Music 6), and sound
    effects and Pokémon cries are as loud as ever.
27. **Ducking.** When a voiced line plays, the music dips smoothly (no click) and comes back after the line.
28. **The Sound view.** Tap the speaker: Music, Effects and Voice rows, and "Quieter music while a voice plays".
    Each − and + changes the sound at once; tapping a step jumps to it. Effects 0 silences footsteps, menus and
    cries; Music 0 silences the music and the jingles (e.g. a Pokémon Center heal).
29. **Kept.** Tap Back, restart the game: the levels are as you left them.
30. **The game's own dips still work.** When a trainer spots you, the music still dims and comes back, at your level.

### Stage 12: choice menus

When the game asks a question with a menu, the bottom screen lists the options under the question, each on its own
line after a ・. The emulator reached only the opening's photo prompt; the field menus are first checked here.

31. **はい / いいえ.** Talk to someone who asks a yes/no question (Mom at home, a Poké Mart clerk). Under the
    question, ・はい and ・いいえ appear; tap いいえ: its card opens.
32. **A script's choices.** At a menu of two or more script options (e.g. a Pokémon Center or Mart counter), every
    option is listed, its words underlined; tap one and add it to kokoro as you would any word.
33. **Grammar.** Tap Grammar with the options showing: the explanation covers the question and the options.
34. **Choosing.** Pick an option: the list goes away and the next line shows as usual. Pick with B (cancel) where
    the menu allows it: the same.
35. **Shops and the Bag.** App menus (the Bag's use/give/toss, a shop's buy/sell confirmation) list their options
    too when a talk window is open; where the question is not in a talk window the panel does not appear, as before.

### Stage 13: the peek (SELECT)

SELECT, which the game itself never uses, swaps the bottom screen to jpreader's panel and back. In the emulator only a
stand-in for SELECT was pressed, and no battle was reached: these are the first real checks.

36. **SELECT on the field.** Walk around and press SELECT: the panel appears with the latest message; press it again:
    the game's own bottom screen is back. While the panel is up, walking and buttons do nothing. On a console the
    system turns SELECT into START unless told not to; jpreader tells it not to, so START still does what it did and
    SELECT no longer acts as START. (Passed on the console once fixed: both buttons read as START before.)
37. **In a battle: the message.** When the top screen says "～は どうする？" (or "こうかは ばつぐんだ！"), press
    SELECT: that message is first on the panel, its words underlined; tap one; tap Grammar.

### Stage 14: the peek reads the screens

The peek now lists the text of everything the game draws, the bottom screen's first (the controls), then the top
screen's: a line a button or box, with the English line when the cartridge's English text has the same string, and
for a move its type. The emulator only reached the opening's はい/いいえ buttons ("・はい  Yes", "・いいえ  No").

38. **The command menu.** In a battle at "～は どうする？", SELECT: ・たたかう FIGHT, ・バッグ BAG, ・ポケモン
    POKÉMON, ・にげる RUN (whatever the buttons say), each word tappable.
39. **The moves.** Open FIGHT, then SELECT: a line a move, e.g. ・はたく PP 35/35  Pound  ノーマル Normal.
40. **The Bag.** Open the Bag, pick a pocket, SELECT: the items on the page with their English names (キズぐすり
    Potion …), and the selected item's description from the top screen. Move the cursor with the peek closed, SELECT
    again: the list follows.
41. **Party and summary.** The party screen and a Pokémon's summary: names, levels, moves, ability, nature.
42. **Back to the game.** SELECT again: the game's buttons work as before (nothing was chosen while the panel was
    up). Tell me what the peek showed that was noise, and what on screen it missed.

### Stage 15: the peek's rows, literal translations

43. **Moves as rows.** In a battle, open FIGHT and SELECT: a row a move, the Japanese in large type with the type
    (a grey pill) and PP at the right, and under it a literal translation, then the official name in grey (でんきショック:
    "electric shock · Thunder Shock"). No row's text runs into the next.
44. **A word's card.** Tap a word in a row: the dictionary card slides up over the lower half, and the row stays in
    view above it. Tap the same word again, or anywhere else in the rows: the card goes. With kokoro linked, the card's
    "+ Add to kokoro" works as on a talk window.
45. **Scrolling.** On a screen with more rows than fit (the Bag, a summary), the bar's ▲ and ▼ move the rows and the
    number between them counts the rows further down; dragging the rows scrolls them too.
46. **The Bag's description.** In the Bag, the selected item's description has its English under it, and its words
    can be tapped.

### Stage 16: word splitting

47. **Kana words stay whole.** At home, Mom's 「ほら！ …… でかける 準備を して！」: でかける is one underline, and its
    card is 出かける (to go out). In a battle or the summary, やせい, とくせい and たいりょく are one word each.
48. **Grammar as one word.** A line with じゃない (or ではない, かもしれない): it is one underline, and its card says
    "is not; isn't it?". Tell me any word that is still cut in two, with the line.

### Stage 17: grammar points

Needs kokoro linked (`jpreader kokoro-link`), as Grammar does since stage 9-0. Grammar replies kept on the card
before this stage are asked again once, so they come back with their points.

49. **Rows under their chunks.** Tap Grammar on a line with some grammar (a て-form request, a polite past). Under
    each chunk, after its meaning and note, each grammar point kokoro found is a row: a small dot, the pattern in
    bold, then a short note. The dot is green for a point you know in kokoro, amber for one you are learning, grey
    otherwise, and a point kokoro has says "· known", "· learning" or "· up next" after its note.
50. **Other grammar.** When kokoro names a point that is in no single chunk, it is in a last, unshaded block titled
    "Other grammar" (not every reply has one). Scroll with ▼ to see it.
51. **Adding a point.** A grey point with a green "+ Add" at the right: tap it. "Adding…", then "Added". On the
    phone, kokoro's grammar reviews have the point after its next sync, as a new point. The rows never jump while
    the button changes.
52. **Remembered.** Talk to the same person again, so the line you added a point from comes up again, and tap
    Grammar: the reply comes back at once (no "Asking…"), and the point you added says "Added", with no button.
    (Within a session only: after a restart the kept reply offers "+ Add" again, and tapping it just says "Added".)
53. **Adding offline.** Turn the wireless switch off and tap "+ Add" on another point: "Queued". Turn wireless on
    and ask Grammar about a new line: the queued point reaches kokoro (check on the phone after its sync) and its row
    then says "Added".
54. **Unchanged.** A point with no Bunpro match has no button, words still add from the card, and the debug
    line's `mem` stays above 3 MB.

### System text voices: the opening video call

The opening's lines are system text (file 38), voiced apart from the story's. `jpreader sd` copies the new
`voice.bin` and `voice-0.bin`. With the debug line on, it shows each line's archive, file and line.

55. **The video call is voiced.** Start a new game. When Professor Kukui's video call begins (「ちょっと 待って……」),
    his voice plays as each line's box appears, and the call's last line (Mom telling you to start packing) is in
    Mom's voice. The debug line shows `30/38/…` (kana; `31/38/…` in kanji), or `mem/38/…` for a few lines the game
    builds in memory: those are voiced too. The はい / いいえ answers are silent.
56. **Replay on the call.** On one of Kukui's lines, tap ▶: the line plays again from the start. Tap 0.7x: it plays
    slower, at the same pitch.
57. **The story still plays its own clips.** Carry on to the first scene at home: Mom's first line plays her story
    clip, not a line of the video call (story file 38 is another scene, on Route 1).

### Voices across Melemele, Akala and Ula'ula

The story is voiced up to Gladion's farewell on arriving at Poni Island (story file 326), plus the townsfolk
of Melemele, Akala and Ula'ula, the Pokémon Center lady and the trainers' field lines.

58. **A trainer.** Walk into a trainer's line of sight on a route: their line before the battle is voiced as its
    box appears, and talking to them after the battle plays their after-battle line. The line they say in the
    battle itself is silent (battle text is not voiced yet).
59. **Townsfolk.** In Hau'oli City, Heahea City or Malie City, talk to a few passers-by: their lines are voiced, men and women
    in different voices, and no child sounds like Hau.
60. **A line the game fills in.** At a Poké Mart or anywhere a line names an item, a price or a number: that line
    is silent (never a voice with words missing); the ▶ and 0.7x buttons stay grey.
61. **Captains and rivals.** Kiawe, Lana, Mallow, Gladion, Guzma and Plumeria each have their own voice; in a scene
    with two of them they never sound the same.
62. **Two voice files.** `voice-0.bin` and `voice-1.bin` are both on the card (about 1.6 GB together, `jpreader sd`
    copies both): lines from late in the story (Ula'ula) play as well as the opening's.

## Sending back a failure

For each item that fails, send:

- the item number and what happened;
- a photo of the bottom screen with the debug line;
- for a crash: a photo of Luma's exception screen (a red heading over white text), plus the newest
  file in `/luma/dumps/arm11/` on the SD card;
- for a freeze (nothing moves and no exception screen): photos of both screens. A freeze leaves no
  crash dump.

Read a crash dump on the Mac with:

```
.venv/bin/jpreader crash "/Volumes/<card name>/luma/dumps/arm11/crash_dump_<number>.dmp"
```

It names the jpreader function or game address the 3DS stopped in, which is what is needed to fix
it. Do not rebuild between copying jpreader to the card and reading a dump: the names come from the
last build.

Grammar answers come from kokoro, which uses the Max plan's monthly API credit (OpenRouter only once that runs out).
