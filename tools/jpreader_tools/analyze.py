"""Split Japanese text into tappable units with SudachiPy (split mode C)."""
from __future__ import annotations

import re
from collections import Counter, defaultdict
from dataclasses import dataclass, replace

from sudachipy import Dictionary, SplitMode

SKIP = {"空白", "補助記号", "記号"}
TAKES_ENDINGS = {"verb", "adj", "aux"}
AFTER_TE = {"いる", "しまう", "おく", "みる", "くる", "いく", "ある", "くれる", "もらう", "あげる", "やる", "くださる"}
AFTER_STEM = {"なさる", "すぎる"}
POS_GROUP = {"動詞": "verb", "形容詞": "adj", "形状詞": "adj", "名詞": "noun", "代名詞": "noun",
             "副詞": "adv", "助動詞": "aux", "接尾辞": "suffix"}
POTENTIAL = "える"            # ending key for a potential verb (勝てる: can win)
# Stage 16: the game spaces its text into chunks (bunsetsu), and a chunk never starts with a particle. One that seems
# to is a kana word SudachiPy broke (でかける as で + かける): see Analyzer._repair.
FUNCTION_POS = {"助詞", "助動詞", "接続詞", "接尾辞"}
CHUNK_SPACE = frozenset("　 ")
CHUNK_EDGE = CHUNK_SPACE | frozenset("！？!?。、…「」『』（）・")
LIGHT_VERBS = {"なる", "する", "ある", "いる", "いう", "いく", "くる", "みる", "おく", "できる", "やる", "くれる", "もらう",
               "しまう"}
PRODUCTIVE = {"やすい", "にくい", "づらい", "がたい", "っぽい", "らしい", "がち", "たち", "そう", "さ", "み", "め", "ら",
              "ごと", "ぶり", "ずつ"}
HONORIFICS = {"さん", "くん", "ちゃん", "さま", "様", "君"}
SENTENCE_END = {"ね", "よ", "な", "ぞ", "ぜ", "わ", "さ", "の"}
KANA_ONLY = re.compile(r"[ぁ-ゖー]+")
E_ROW = set("えけげせぜてでねへべぺめれ")     # a potential verb ends in one of these plus る


def to_hiragana(text: str) -> str:
    return "".join(chr(ord(ch) - 0x60) if "ァ" <= ch <= "ヶ" else ch for ch in text)


def ending_key(dict_form: str, conj_form: str) -> str:
    if dict_form == "で":
        return "て"
    if conj_form.startswith("仮定形") and dict_form in ("た", "だ"):
        return "たら" if dict_form == "た" else "なら"
    if conj_form.startswith("意志推量形") and dict_form == "ます":
        return "ましょう"
    return dict_form


@dataclass(frozen=True)
class Unit:
    start: int
    end: int
    kind: str
    surface: str
    lemma: str
    reading: str
    pos: str
    endings: tuple[str, ...] = ()
    surface_reading: str = ""
    normalized: str = ""      # SudachiPy's standard spelling of the lemma (いう → 言う, 勝てる → 勝つ)


def _joins(unit: Unit, p0: str, p1: str, lemma: str) -> bool:
    if p0 == "助動詞":
        return True
    if p0 == "助詞":
        return p1 == "接続助詞" and lemma in ("て", "で", "ば")
    if p0 == "動詞":
        return (lemma in AFTER_TE and unit.endings[-1:] == ("て",)) or lemma in AFTER_STEM
    if p0 == "形容詞":
        return p1 == "非自立可能" and lemma == "ない" and unit.pos == "verb"
    return False


class Vocabulary:
    """The words the game writes in kanji in one script and in kana in the other (stage 16, annotate.game_vocabulary):
    (kanji lemma, kana reading) -> how often; and each kana prefix's kanji spellings, the commonest first
    (出かける, でかける: で → 出; 野生, やせい: やせい → 野生)."""

    def __init__(self, words: dict, surfaces: dict | None = None, prefix: int = 3):
        """surfaces: (lemma, reading) -> the kana the game writes it with (でかけた, でかけて). A repair must match one
        of them in its first `prefix` kana: はこぼれ (刃こぼれ) is not 運ぶ, which the game writes はこば, はこぶ….
        Without surfaces nothing is checked."""
        self.words = dict(words)
        self.surfaces = {k: frozenset(v) for k, v in surfaces.items()} if surfaces is not None else None
        self.prefix = prefix
        subs: dict[str, Counter] = defaultdict(Counter)
        for (lemma, reading), n in self.words.items():
            if not reading or not KANA_ONLY.fullmatch(reading):
                continue
            same = 0                                    # the okurigana both spellings end with
            while same < min(len(lemma), len(reading)) and lemma[-1 - same] == reading[-1 - same]:
                same += 1
            kana, kanji = reading[:len(reading) - same], lemma[:len(lemma) - same]
            if kana and kanji:
                subs[kana][kanji] += n
        self._subs = {kana: [kanji for kanji, _n in c.most_common()] for kana, c in subs.items()}
        self.longest = max(map(len, self._subs), default=0)

    def __contains__(self, word) -> bool:
        return word in self.words

    def spellings(self, kana: str) -> list[str]:
        return list(self._subs.get(kana, ()))

    def attested(self, word, kana: str) -> bool:
        """Has the game written word with the kana that kana starts with?"""
        if self.surfaces is None:
            return True
        n = min(len(kana), self.prefix)
        return any(seen[:n] == kana[:n] for seen in self.surfaces.get(word, ()))


def _grammar_unit(start: int, end: int, surface: str, lemma: str, endings: tuple, reading: str) -> Unit:
    return Unit(start, end, "word", surface, lemma, lemma, "adj", endings, reading, lemma)


def _split_janai(u: Unit) -> list[Unit]:
    """A verb or adjective SudachiPy joined じゃない to (違うじゃない: endings だ, ない): the word, then じゃない."""
    for k in range(len(u.endings) - 1):
        cut = u.surface.rfind("じゃな")
        if u.endings[k:k + 2] == ("だ", "ない") and cut > 0:
            tail = len(u.surface) - cut                  # kana: as long in the reading
            return [replace(u, end=u.start + cut, surface=u.surface[:cut], endings=u.endings[:k],
                            surface_reading=u.surface_reading[:-tail]),
                    _grammar_unit(u.start + cut, u.end, u.surface[cut:], "じゃない", u.endings[k + 2:],
                                  u.surface_reading[-tail:])]
    return [u]


def _merge_grammar(units: list) -> list:
    """Grammar a learner reads as one word: じゃない (and すごいじゃない's, which SudachiPy joins to the adjective),
    ではない, かもしれない, and a verb's なくちゃ."""
    out, i = [], 0
    units = [piece for u in units for piece in (_split_janai(u) if u.pos in ("verb", "adj") else [u])]
    while i < len(units):
        u = units[i]
        b = units[i + 1] if i + 1 < len(units) and units[i + 1].start == u.end else None
        c = units[i + 2] if b and i + 2 < len(units) and units[i + 2].start == b.end else None
        if (b and b.kind == "word" and b.lemma == "ない" and u.surface.endswith("じゃ") and
                ((u.surface == "じゃ" and u.lemma == "だ") or (u.surface != "じゃ" and u.endings[-1:] == ("だ",)))):
            if u.surface != "じゃ":
                out.append(replace(u, end=u.end - 2, surface=u.surface[:-2], endings=u.endings[:-1],
                                   surface_reading=u.surface_reading[:-2]))
            out.append(_grammar_unit(u.end - 2, b.end, "じゃ" + b.surface, "じゃない", b.endings,
                                     "じゃ" + b.surface_reading))
            i += 2
        elif u.surface == "で" and b and b.surface == "は" and c and c.kind == "word" and c.lemma == "ない":
            out.append(_grammar_unit(u.start, c.end, "では" + c.surface, "ではない", c.endings, "では" + c.surface_reading))
            i += 3
        elif (u.surface == "か" and u.kind == "particle" and b and b.surface == "も" and c and
              c.lemma in ("しれる", "知れる") and c.endings[:1] in (("ない",), ("ます",))):
            endings = c.endings[1:] if c.endings[:1] == ("ない",) else ("ます",)    # かもしれません: polite
            out.append(_grammar_unit(u.start, c.end, "かも" + c.surface, "かもしれない", endings,
                                     "かも" + c.surface_reading))
            i += 3
        elif (u.pos == "verb" and u.endings[-1:] == ("ない",) and u.surface.endswith(("なく", "なくっ")) and b
              and b.surface == "ちゃ"):
            out.append(replace(u, end=b.end, surface=u.surface + "ちゃ", endings=u.endings + ("ちゃ",),
                               surface_reading=u.surface_reading + "ちゃ"))
            i += 2
        else:
            out.append(u)
            i += 1
    return out


class Analyzer:
    def __init__(self, vocabulary: Vocabulary | None = None, grammar: frozenset = frozenset()):
        """vocabulary: the game's own kanji spellings, for repairing broken kana words (stage 16; none: no repair).
        grammar: kana strings JMdict lists as particles, conjunctions or expressions, which a repair never swallows."""
        self._tokenizer = Dictionary(dict="core").tokenizer(mode=SplitMode.C)
        self._readings: dict[str, str] = {}
        self.vocabulary = vocabulary
        self.grammar = grammar

    def reading_of(self, text: str) -> str:
        if text not in self._readings:
            self._readings[text] = to_hiragana("".join(m.reading_form() for m in self._tokenizer.tokenize(text)))
        return self._readings[text]

    def _potential(self, p0: str, lemma: str, normalized: str) -> bool:
        """勝てる normalizes to 勝つ with a different reading; いう → 言う keeps its reading."""
        reading = self.reading_of(lemma)
        return (p0 == "動詞" and normalized != lemma and reading.endswith("る") and len(reading) >= 2
                and reading[-2] in E_ROW and self.reading_of(normalized) != reading)

    def units(self, text: str, chunk_start: bool = True) -> list[Unit]:
        """chunk_start: text begins a chunk (a run after a tag, 「{name}が」, does not)."""
        text = text.replace("\n", "　")     # same length; analysed as a space
        units, function_starts = self._analyse(text)
        if self.vocabulary is not None and self.vocabulary.longest:
            units = self._repair(text, units, function_starts, chunk_start)
        return units

    def _analyse(self, text: str) -> tuple[list[Unit], set[int]]:
        """The units, and where SudachiPy put a particle, auxiliary, conjunction or suffix."""
        units: list[Unit] = []
        function_starts: set[int] = set()
        current: Unit | None = None
        for m in self._tokenizer.tokenize(text):
            pos = m.part_of_speech()
            p0, p1, form = pos[0], pos[1], pos[5]
            if p0 in SKIP:
                current = None
                continue
            if p0 in FUNCTION_POS:
                function_starts.add(m.begin())
            lemma = m.dictionary_form()
            reading = to_hiragana(m.reading_form())
            if current is not None and current.pos in TAKES_ENDINGS and _joins(current, p0, p1, lemma):
                current = replace(current, end=m.end(), surface=current.surface + m.surface(),
                                  endings=current.endings + (ending_key(lemma, form),),
                                  surface_reading=current.surface_reading + reading)
                units[-1] = current
                continue
            normalized = m.normalized_form()
            current = Unit(m.begin(), m.end(), "particle" if p0 == "助詞" else "word", m.surface(), lemma,
                           self.reading_of(lemma), POS_GROUP.get(p0, "other"),
                           (POTENTIAL,) if self._potential(p0, lemma, normalized) else (), reading, normalized)
            units.append(current)
        return _merge_grammar(units), function_starts

    def _repair(self, text: str, units: list, function_starts: set, chunk_start: bool) -> list:
        """Each chunk that starts with a function word but holds more: re-analysed with the kanji spelling the game
        uses for the word it starts with, when SudachiPy then reads that word as the game's kana (see _substitute)."""
        out, i = [], 0
        while i < len(units):
            u = units[i]
            at_edge = (u.start == 0 and chunk_start) or (u.start > 0 and text[u.start - 1] in CHUNK_EDGE)
            if not at_edge or u.start not in function_starts or u.surface in HONORIFICS:
                out.append(u)
                i += 1
                continue
            end = u.start
            while end < len(text) and (end == u.start or text[end] not in CHUNK_EDGE):
                end += 1
            inside = [x for x in units[i:] if x.end <= end]
            new = None
            if len(inside) >= 2 or end - u.start > len(u.surface):
                rel = [replace(x, start=x.start - u.start, end=x.end - u.start) for x in inside]
                new = self._substitute(text[u.start:end], rel)
            if new is None:
                out.append(u)
                i += 1
                continue
            out += [replace(x, start=x.start + u.start, end=x.end + u.start) for x in new]
            i += len(inside)
        return out

    def _substitute(self, chunk: str, old: list) -> list | None:
        u = old[0]
        bounds = {x.end for x in old}
        kana = to_hiragana(chunk)
        half = len(kana) // 2
        if half >= 2 and len(kana) == 2 * half and kana[:half] == kana[half:]:
            return None                                  # なかなか, いろいろ: a doubled word is one adverb
        for n in range(min(self.vocabulary.longest, len(kana)), 0, -1):
            for kanji in self.vocabulary.spellings(kana[:n]):
                found, _starts = self._analyse(kanji + chunk[n:])
                if not found or found[0].start != 0 or found[0].end < len(kanji):
                    continue
                first, shift = found[0], n - len(kanji)
                mapped = []
                for k, x in enumerate(found):
                    s, e = (0 if k == 0 else x.start + shift), x.end + shift
                    mapped.append(replace(x, start=s, end=e, surface=chunk[s:e],
                                          surface_reading=to_hiragana(chunk[s:e]) if k == 0 else x.surface_reading))
                word = mapped[0]
                swallowed = [x for x in old if x.end <= word.end]
                if ((first.lemma, first.reading) not in self.vocabulary
                        or first.reading in LIGHT_VERBS                        # なりすぎた: なる, not 鳴る
                        or (first.reading in self.grammar and first.pos != "verb")   # くらい, not 暗い
                        or not self.vocabulary.attested((first.lemma, first.reading), to_hiragana(word.surface))
                        or to_hiragana(word.surface) != first.surface_reading   # SudachiPy reads it as the game's kana
                        or (word.end not in bounds and len(word.surface) < 3)   # a short word is whole old units
                        or to_hiragana("".join(x.surface for x in swallowed)) in self.grammar   # とか, かも, として
                        or (len(swallowed) >= 2 and swallowed[-1] is old[-1]
                            and to_hiragana(old[-1].surface).rstrip("ーぇえ") in SENTENCE_END)   # また+ねえ: see you
                        or any(x.kind == "particle" and to_hiragana(x.surface).rstrip("ーぇえ") in SENTENCE_END
                               for x in swallowed[1:-1])                 # でねでね: no word holds a ね
                        or (first.pos == "verb" and first.endings[:1] in (("です",), ("ね",), ("ぬ",)))
                        # 外れ+です is the noun 外れ; 出ね (でねでね, a filler) the old negative a repair never means
                        or (len(swallowed) == 2 and u.surface in ("に", "と", "を", "が")
                            and swallowed[1].lemma in LIGHT_VERBS)                # １位　になった: に + なる
                        or (len(swallowed) == 1 and u.lemma in PRODUCTIVE)       # なつき　やすく: -やすい
                        or (word.end <= u.end and not (word.end == u.end and u.pos == "suffix"))):   # nothing moved
                    continue
                return mapped
        return None
