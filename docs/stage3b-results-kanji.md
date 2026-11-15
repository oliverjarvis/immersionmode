# Stage 3B results (kanji)

Result: **PASSED**

Written by `jpreader e2e stage3`. Azahar in Old 3DS mode, debug build of the payload.

## Checks

| Check | Result | Detail |
|---|---|---|
| data files open | pass | status 1, error 0x0 |
| drawing time with a card | pass | worst of 10 frames 1.56 ms of CPU time (budget 2.0 ms) |
| units match the build | pass | 20 of 20 messages agree; differing: none |
| lines found | pass | 19 of 19 lines with words placed in the displayed text (100%) |
| words selected by tapping | pass | #1 しました: selected 4/4 at 6/6, entry 15/15, card 1; #2 の: selected 3/3 at 6/6, entry 23/23, card 1; #3 待って: selected 2/2 at 5/5, entry 333/333, card 1 |
| cards read | pass | 3 of 3 cards read |

## Lines by outcome

| Outcome | Messages |
|---|---|
| found | 19 |
| no words | 1 |

## Screenshots

![Card for しました](images/stage3b-kanji-card-1.png)
![Card for の](images/stage3b-kanji-card-2.png)

## Log

```
PASS data files open: status 1, error 0x0
#1 found, 4 units (host: found, 4) | 設定が　終了しました
tap #1 at (116, 14) on しました: #1 しました: selected 4/4 at 6/6, entry 15/15, card 1
PASS drawing time with a card: worst of 10 frames 1.56 ms of CPU time (budget 2.0 ms)
#2 found, 8 units (host: found, 8) | アローラ地方の/ククイ博士から　連絡です
tap #2 at (116, 14) on の: #2 の: selected 3/3 at 6/6, entry 23/23, card 1
#3 found, 2 units (host: found, 2) | ちょっと　待って……
tap #3 at (99, 14) on 待って: #3 待って: selected 2/2 at 5/5, entry 333/333, card 1
#4 found, 2 units (host: found, 2) | やあ　こんばんは！
#5 found, 9 units (host: found, 9) | アローラに　引越しする　日が/いよいよ　ちかづいてきたね！
#6 found, 10 units (host: found, 10) | アローラは　いくつかの　島が/集まって　できている　地方
#7 found, 11 units (host: found, 11) | それが　理由なのか/珍しい　ポケモン　ばかりだぜ！
#8 found, 11 units (host: found, 11) | そう　アローラにも　ポケットモンスター/縮めて　ポケモンが　たくさんいる！
#9 found, 9 units (host: found, 9) | ポケモンは　本当に/不思議な　生き物でね
#10 found, 9 units (host: found, 9) | 草むらや　洞窟　空　海……/いたるところに　いて
#11 found, 11 units (host: found, 11) | ぼくたちは　ポケモンの/力を借りたり　助けあったり
#12 found, 10 units (host: found, 10) | ポケモントレーナー　として/ポケモンを　戦わせたりするんだ！
#13 found, 11 units (host: found, 11) | イワンコに　アブリー！　あとで/遊ぶからね　待っててくれるかい？
#14 found, 13 units (host: found, 13) | さて　みんなに　紹介するから/あらためて　きみのことを　きくぜ！
#15 found, 9 units (host: found, 9) | どの写真が　いいか/教えて　くれるかな？
#16 found, 6 units (host: found, 6) | 選んだので　いいよね？
#17 found, 9 units (host: found, 9) | 名前の　登録も/きみに　おまかせ　するよ！
#18 found, 4 units (host: found, 4) | いきし　で　いいかい？
#19 found, 5 units (host: found, 5) | オーケー！/みんなに　伝えておくよ！
#20 no words, 0 units (host: no words, 0) | いきし……！
PASS units match the build: 20 of 20 messages agree; differing: none
PASS lines found: 19 of 19 lines with words placed in the displayed text (100%)
PASS words selected by tapping: #1 しました: selected 4/4 at 6/6, entry 15/15, card 1; #2 の: selected 3/3 at 6/6, entry 23/23, card 1; #3 待って: selected 2/2 at 5/5, entry 333/333, card 1
PASS cards read: 3 of 3 cards read
```
