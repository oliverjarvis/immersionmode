# Stage 3A results

## Coverage

| Archive | Content words | With an entry | Coverage |
|---|---|---|---|
| 30 (system text, kana) | 118225 | 115286 | 97.5% |
| 31 (system text, kanji) | 116255 | 113652 | 97.8% |
| 40 (story text, kana) | 103828 | 101274 | 97.5% |
| 41 (story text, kanji) | 103035 | 100500 | 97.5% |
| **All** | 441343 | 430712 | **97.6%** |

Coverage counts words and names written in Japanese script. Kana lines lined up with their kanji
line: 50,705; analysed on their own: 2,494. Card entries: 15,754 (including dictionary-form-only cards for words with no match); ending labels: 46.

## Hand check of 50 story lines

The sample is `docs/stage3a-sample.md` (seed 1, story lines with at least three units).

- Units checked: 603. About 560 correct (about 93%).
- Problems in the final sample, about 43:
  - Boundaries, about 17: words the game spells in kana that the analyser splits wrongly, for example
    つめたいいわ (the item つめたいいわ, "Icy Rock"), しょうがない, さいみんじゅつ, はいってない (入って), and
    baby talk (べのめのん).
  - Wrong entry, about 24: a kana word given the wrong word of the same sound, for example ふむ (an
    interjection) as 踏む, いし (stone) as 意志, うち (while) as 家, いま (now) as 居間, ほう (oh?) as 報, and
    なれる (can become) as 慣れる. Also 人 and 目 in ３人目 given their noun senses rather than counter and
    ordinal.
  - Glossary, 2: レポート glossed "SAVE" (a menu label), and a lone ン treated as a name.
- Fixed during the review (each with a test that failed first):
  - Kana-spelled words picked the wrong word of the same sound (いう as 結う, くる as 繰る, みる as 看る):
    lookups now try SudachiPy's normalized form first (`test_normalized_forms_settle_kana_spellings`,
    `test_normalized_form_picks_the_right_homonym`).
  - Numbers were looked up by sound (３ → 酸, "acid"): lookup by sound is now only for kana
    (`test_no_lookup_by_sound_for_numbers_and_latin`).
  - Potential verbs (勝てる, 来れる, 戦える) had no entry: they now resolve to the base verb with the
    label "potential: can …" (`test_potential_forms_get_their_base_verb`, `test_potential_labels`);
    する is not mistaken for one (`test_suru_and_ordinary_verbs_are_not_potential`).
  - Stretched particles (よー, ねー) now get the particle note (`test_stretched_particles`).
  - さん right after an inserted name was read as 三 or 酸; it is now the honorific suffix
    (`test_honorific_after_a_name_tag_is_a_suffix`).
- Left as they are: the boundary and same-sound errors above are one-offs, mostly in kana-written
  speech where the analyser has little to go on. Coverage before the fixes was 97.5%; after, 98.3%.

## Files

annot.bin 8.4 MB, dict.bin 1.2 MB, plus NOTICE.txt (JMdict and Unifont credits).

## After the branch review

The review found three faults the coverage figure could not see; each was fixed with a failing test:

- The glossary took every short katakana line, so the name-entry keyboard and menu labels became
  "names" (コ → "J", レポート → "SAVE", メス → "Cal"). It now takes lines only from the name lists,
  found by a known name each contains, and needs at least two characters and an English value with
  Latin letters (1,953 keys).
- Particles with no note fell through to unrelated nouns (い → 胃 "stomach", 166 times in the
  story). A particle now resolves only to a JMdict particle, auxiliary, expression or conjunction.
- Words with no match had no card at all; they now get a card with the dictionary form and reading
  (spec 7.3, step 4), counted as uncovered.

Coverage is now 97.6%: lower than before because the glossary no longer counts junk as covered.

In game (stage 3B): `docs/stage3b-results-kana.md` and `docs/stage3b-results-kanji.md`.
