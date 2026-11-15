# Stage 1 results

Result: **PASSED**

Written by `jpreader e2e stage1`. Azahar in Old 3DS mode, debug build of the payload.

## Memory

| Figure | Value |
|---|---|
| Application memory | 80.0 MB |
| Used when the payload started | 6.5 MB |
| Used after the socket buffer was claimed | 6.8 MB |
| Peak used during the run | 76.5 MB |
| Free at the peak | 3.5 MB |
| Socket buffer address | 0x0ff00000 (result 0x0) |
| Result of the protection call | 0xffff8001 |

## Capture

| Figure | Value |
|---|---|
| Game language byte | 0 |
| Lines validated | 20 |
| Lines that failed validation | 0 |
| Talk windows seen to end | 4 |
| Soak time | 600 s |

Where the validated lines came from: archive 30: 15, memory: 5

## Log

```
payload started; application memory 80.0 MB, used 6.8 MB
the game is polling input
language byte before any input: 0
#1 OK  archive 30 file 55 line 6 | せっていが　しゅうりょう　しました
#2 OK  archive 30 file 55 line 7 | アローラちほうの/ククイはかせ　から　れんらくです
#3 OK  archive 30 file 38 line 1 | ちょっと　まって……
#4 OK  archive 30 file 38 line 23 | やあ　こんばんは！
#5 OK  archive 30 file 38 line 2 | アローラに　ひっこしする　ひが/いよいよ　ちかづいてきたね！
#6 OK  archive 30 file 38 line 3 | アローラは　いくつかの　しまが/あつまって　できている　ちほう
#7 OK  archive 30 file 38 line 4 | それが　りゆう　なのか/めずらしい　ポケモン　ばかりだぜ！
#8 OK  archive 30 file 38 line 17 | そう　アローラにも　ポケットモンスター/ちぢめて　ポケモンが　たくさんいる！
#9 OK  archive 30 file 38 line 18 | ポケモンは　ほんとうに/ふしぎな　いきもの　でね
#10 OK  archive 30 file 38 line 19 | くさむらや　どうくつ　そら　うみ……/いたるところに　いて
#11 OK  archive 30 file 38 line 20 | ぼくたちは　ポケモンの/ちからをかりたり　たすけあったり
#12 OK  archive 30 file 38 line 21 | ポケモントレーナー　として/ポケモンを　たたかわせたりするんだ！
#13 OK  archive 30 file 38 line 24 | イワンコに　アブリー！　あとで/あそぶからね　まっててくれるかい？
#14 OK  archive 30 file 38 line 5 | さて　みんなに　しょうかい　するから/あらためて　きみのことを　きくぜ！
#15 OK  archive 30 file 38 line 6 | どの　しゃしんが　いいか/おしえて　くれるかな？
#16 OK  archive 30 file 38 line 7 | えらんだので　いいよね？
#17 OK  archive 30 file 38 line 8 | なまえの　とうろくも/きみに　おまかせ　するよ！
name prompt seen; typing a name
#18 OK  archive 30 file 38 line 9 | いきし　で　いいかい？
#19 OK  archive 30 file 38 line 10 | オーケー！/みんなに　つたえておくよ！
#20 OK  archive 30 file 38 line 11 | いきし……！
20 consecutive lines validated; 4 talk window(s) ended
soak of 600 s completed; peak memory used 76.5 MB
```

## Findings for stage 2

- **Memory.** The game sizes its heap from whatever is free when it starts, so it absorbed the 768 KB the payload takes at start-up and still left 3.5 MB free at its peak (76.5 of 80 MB used). Anything the payload needs later must come out of that 3.5 MB or, better, be claimed at start-up like the socket buffer.
- **Where dialog comes from.** In the opening, Kukui's call and the start-up notices come from system text (archive 30, kana), not story text (40/41). Five lines came from message data the game built in memory (archive −1); they validated through the text-file header, so origin tracking holds for both kinds. No line had an unknown origin. During the soak, field dialog with Mum's Meowth (「ぬにゃ！」) was also captured and validated, through memory-built message data.
- **Rotom.** Not reached: the Rotom Dex is given much later, so whether its bottom-screen lines go through `TalkWindow` is still open for stage 2.
- **Bottom screen during dialog.** Black in the opening scenes (no Rotom Dex yet).
- **Manual mode.** Not needed. The driver types a name (いきし) when Kukui asks for one; A presses alone type 「ああああああ」, which the game refuses.
- **Layout change.** The first run hung 3 s in: the game maps its CRO modules at `heap address − 0x08000000 + 0x6D4000`, so the payload could not sit past `0x6D4000`. The payload now runs at `0x0FEA0000` and is staged in the game's bss pages (spec section 6.1). The protection call (SVC 0x70) is not implemented by Azahar (result `0xffff8001` is the untouched handle); it matters only on hardware.
