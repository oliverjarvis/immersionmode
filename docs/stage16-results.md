# Stage 16 results: word splitting

Measured on 2026-10-16.

## What was wrong

SudachiPy learned Japanese from ordinary writing, where 出かける, 野生 and 体力 are written in kanji. Pokémon writes
them in kana for young readers, and SudachiPy breaks them: で + かける, や + せい, た + いりょく. The user found
「でかける　準備を　して！」 split as the particle で plus かける, and じゃない split into じゃ + ない.

## The fixes

**Chunks.** The game puts a space between phrase chunks, and a particle never starts one (the user's observation). A
chunk that seems to start with a particle, auxiliary, conjunction or suffix and holds more is analysed again with
the kanji spelling the game itself uses for the word it starts with (`analyze.Analyzer._repair`). The spellings come
from the game: each pair of kanji-mode and kana-mode lines whose words line up gives the words written in kanji in
one and in kana in the other, kept only where SudachiPy reads the kanji as the game's kana
(`annotate.game_vocabulary`, `analyze.Vocabulary`).

A repair is kept only when the new word is in that vocabulary, is read as the kana on screen, starts the way the
game writes that word in kana (its first three kana match one of the game's own kana spellings of it), and none of
these holds (each was a wrong repair in the research, the first full build or the review):

- it swallows grammar JMdict lists (とか, かも, として);
- the new word is a light verb or a grammar word's reading (なりすぎた is なる, not 鳴る; くらい, not 暗い);
- the chunk ends in ね/よ/な… (また+ねえ, "see you"), or a sentence-ending particle sits inside it (でねでね);
- に/と/を/が + a light verb (１位　になった is に + なる, not 担う);
- a productive suffix (なつき　やすく: -やすい);
- a verb taking です or the old negative (外れ+です is the noun 外れ; 出ね);
- the chunk is a doubled word (Rotom's なカなカ, いろいろ): it is one adverb.

A chunk ends at a space or at punctuation. A run of text after a name or number tag (「{name}が」) continues its
chunk; after a colour or a wait it starts one.

The game's spacing is not a perfect rule: it also spaces before some copulas and particles (「ポケモン　だよね」), so
the repair is only tried, never forced.

**Grammar as one word** (`analyze._merge_grammar`): じゃない (じゃなかった, じゃなくて, and after a verb or adjective:
違うじゃない, すごいじゃない), ではない, かもしれない (かもしれません keeps "polite"), and a verb's なくちゃ (なくっちゃ).
They resolve to JMdict's じゃ無い "is not; isn't it?", ではない and かも知れない. じゃん gets a note in
`particles.tsv`. Forms the game spaces (「じゃ　ない」, 「かも　しれない」) stay two units.

## Effect

1,364 lines changed (kana and kanji, system and story text), with 1,549 new units: about 446 grammar merges and the
rest repaired kana words. The commonest: じゃない 254, やせい 63, ではない 62, とくせい 60, かってに (Rotom's
かっテに) 60, のうりょく 44, でた 34, かせき 33, じごく 30, きあい 29, ではなく 29, たいりょく 27, からて 25,
きせき 24, かもしれない 22, ずつき 20, じゃなくて 20, たいあたり 15, でかける 8.

A sweep of the whole script after the review's fixes left about 20 doubtful repairs among some 4,900 changed units
(under 1%): とお in とおせんぼう (遠い), some だし (出す, where it is だ + し), はで (派手, where it is 歯で),
やから (輩, where it is dialect for だから), なカ outside なカなカ. The review's other 30 or so same-sound cases (鳴る,
暗い, 越す, 着る, 噛む, 運ぶ, 治る…) are gone.

The gold set (`jpreader accuracy`) scores the entry chosen for each word, keyed by its position in the line, so rows
after a changed word moved: 15 were renumbered by their place in the text, and 7 on changed words relabelled by hand
(たいあたり 体当たり, げんし 原始, でた 出る twice, てかげん 手加減, じゃない twice). It
stays at 91.5% overall, 89.3% ambiguous (91.6% / 89.3% before): two rows better, three worse, the three being
Claude's choices for lines asked again because their words changed. The gold set measures the dictionary entry,
not the split.

## Not done

- Move and ability names in hiragana (じごくぐるま, はかいこうせん) whose first piece is a noun or adverb, not a
  particle, are still cut; most of the gold set's 97 "fragment" rows are these. Keeping parts is deliberate where
  they mean something (体 in 体当たり); the rest need the move names in the glossary or a Claude-checked merge.
- Repairs only start at a chunk's first word.
