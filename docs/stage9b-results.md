# Stage 9B results: the right entry for each game word

Measured on 2026-10-12.

## The gold set

- `tests/data/gold-entries.tsv` holds 1,500 word occurrences from kanji-mode game text: 700 ambiguous
  (more than one JMdict candidate) and 800 others, sampled with seed 9.
- Six Claude Opus agents labelled them from the line, its kana version, the game's English line and the
  candidates. The model being measured never labels.
- 237 are labelled 0, "no JMdict entry fits". Most of these are pieces of kana-written compounds that
  the tokenizer cut up (ぼう in ぼうぎょ, じ in じばく), plus cries and stutters.
- The implementer checked 15 labels at random; all were sound.

## Accuracy

| Build | Overall (1,500) | Ambiguous (700) |
|---|---|---|
| Before (stage 9-0) | 76.7% | 61.1% |
| Ranking by matched form (9B task 1) | 76.8% | 61.3% |
| + Haiku 5.5, prompt v1, no thinking (gold lines only) | 81.1% | 70.6% |
| + Haiku 5.5, prompt v2, no thinking (gold lines only) | 83.3% | 74.3% |
| + Haiku 5.5, prompt v2, thinking at medium effort (gold lines only) | 91.5% | 88.9% |
| + Sonnet 5.5, prompt v2, no thinking (gold lines only, for comparison) | 92.4% | 90.0% |
| + Haiku 5.5, prompt v3, thinking (gold lines only) | 92.8% | 91.4% |
| Full build: Haiku 5.5, prompt v3, thinking, one batch | 91.5% | 89.0% |
| **Full build after the review fixes (game spelling for headwords; the 76 leftover lines asked again)** | **91.6%** | **89.3%** |

What changed between the prompts:
- **v2:** each word is shown marked inside its line (【ぼう】ぎょ), with explicit rules for answering 0.
- **v2 also:** short kana words are asked about even when they have a single candidate.
- **v3:** a meaningful part of a compound keeps its entry (体 in 体当たり); 0 is only for a cut inside a
  word.

The plan's targets (98% overall, 95% ambiguous) are **not met**. What remains:
- **Kana compounds cut into pieces.** Move, item and place names in battle and system text are written
  in kana (でんじほう, わるあがき, じごくづき, ふういん). The tokenizer splits them, and a piece longer
  than two characters with one candidate is never asked about. This is segmentation, outside 9B's scope
  (spec 5). A glossary of the game's own move and item names (they pair with the English text, as
  Pokémon names already do) would turn these into single name units with their English names.
- **Haiku's misses on hard cases:** set phrases (元に戻る, ようだ) and particle で versus copula だ.

## The decision batch

- Batch `msgbatch_01G1Sbe7tL45N1joKYDQzXUA`: 1,367 requests, all succeeded, in a few minutes.
- 27,325 kanji-mode lines asked, 27,244 decided. 76 stay undecided, and use the ranking. 193 answers
  were ignored for naming an id that was not offered.
- Cost: 16.2 M input tokens and 4.4 M output tokens at Haiku 5.5's batch prices, **$1.92** from the
  Max plan's monthly API credit. The estimate's upper bound was $3.04.
- A second, tiny batch (4 requests, about $0.01) asked the 76 undecided lines again: 65 were decided, and 2 are left to
  ranking after two tries.
- Decisions are cached in `build/cache/decisions.json`: a rebuild costs nothing.

## Headwords and ids

- Headwords follow the front rule (spec 3): する, それ, できる and ちょっと are written in kana, never as
  為る, 其れ, 出来る or 一寸, and never as a search-only or irregular form.
- `ids.bin` gives each dict.bin entry its JMdict id.
- `build/jpreader-ids.json` lists the 10,373 ids the game uses, for kokoro's dictionary (stage 9A).

## Checks

- pytest: 327 passed. The ROM coverage test counts a decided "none fits" as an answer.
- Release build: payload 131,116 bytes, `ids.bin` 66,380 bytes.
- Emulator end-to-end: stage 4 passed 20/20 and stage 8B passed 8/8 on the 9B build.
- A fresh review found 5 important issues; all are fixed with tests that failed first (333 tests pass).
