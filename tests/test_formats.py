import struct
import unicodedata

import pytest

from jpreader_tools import formats
from jpreader_tools.dictionary import CardEntry
from jpreader_tools.lines import Segment

SEGS = (Segment(0, 5, ord("可"), ord("を")), Segment(1, 3, 0, 0))
LINE = formats.LineRecord(SEGS, (formats.UnitRecord(0, 3, "word", 7, None),
                                 formats.UnitRecord(3, 1, "particle", 2, 4)))
OTHER = formats.LineRecord((Segment(0, 2, ord("や"), ord("あ")),), (formats.UnitRecord(0, 2, "name", None, None),))


def test_annot_round_trip():
    archives = {41: [formats.AnnotFile(0x1234, [LINE, None, OTHER]), formats.AnnotFile(0, [])],
                30: [formats.AnnotFile(99, [OTHER])]}
    blob = formats.write_annot(archives)
    assert blob[:4] == b"JPRA" and struct.unpack_from("<I", blob, 4)[0] == 2
    assert formats.read_annot(blob) == archives


def test_file_table_carries_the_header_total():
    blob = formats.write_annot({41: [formats.AnnotFile(0xABCDE, [LINE])]})
    files_at = struct.unpack_from("<I", blob, 16 + 4)[0]
    assert struct.unpack_from("<III", blob, files_at)[1:] == (1, 0xABCDE)


def test_identical_records_are_shared():
    one = formats.write_annot({41: [formats.AnnotFile(0, [LINE])]})
    many = formats.write_annot({41: [formats.AnnotFile(0, [LINE] * 1000)]})
    assert len(many) - len(one) == 999 * 4              # only the line table grows


def test_records_the_payload_cannot_hold_are_refused():
    units = tuple(formats.UnitRecord(i, 1, "word", None, None) for i in range(257))
    big = formats.LineRecord((Segment(0, 300, 0x3042, 0x3042),), units)
    with pytest.raises(formats.FormatError, match="units"):
        formats.write_annot({41: [formats.AnnotFile(0, [big])]})
    many = formats.LineRecord(tuple(Segment(1, 3, 0, 0) for _ in range(65)), ())
    with pytest.raises(formats.FormatError, match="segments"):
        formats.write_annot({41: [formats.AnnotFile(0, [many])]})


def test_units_must_be_in_order():
    backwards = formats.LineRecord((Segment(0, 4, 0x3042, 0x3042),),
                                   (formats.UnitRecord(2, 2, "word", None, None), formats.UnitRecord(0, 2, "word", None, None)))
    with pytest.raises(formats.FormatError, match="order"):
        formats.write_annot({41: [formats.AnnotFile(0, [backwards])]})


def test_annot_rejects_other_files():
    with pytest.raises(formats.FormatError):
        formats.read_annot(b"JPRF" + bytes(12))


def test_dict_round_trip():
    entries = [CardEntry("dictionary", "秘める", "ひめる", "verb (ichidan)", ("to hide", "to keep secret"), 1453328),
               CardEntry("particle", "を", "", "particle", ("marks the direct object",)),
               CardEntry("name", "ナマコブシ", "", "name", ("Pyukumuku",)),
               CardEntry("lemma", "ゼンリョク", "ぜんりょく", "", ())]
    labels = ["-te iru: ongoing action or state", "past"]
    blob = formats.write_dict(entries, labels)
    assert blob[:4] == b"JPRW"
    assert formats.read_dict(blob) == (entries, labels)


def test_dict_rejects_other_files():
    with pytest.raises(formats.FormatError):
        formats.read_dict(b"JPRA" + bytes(20))


def test_entries_the_payload_cannot_read_are_refused():
    long = CardEntry("dictionary", "長", "ながい", "noun", ("x" * 200, "y" * 200, "z" * 200))
    with pytest.raises(formats.FormatError, match="entry"):
        formats.write_dict([long], [])
    with pytest.raises(formats.FormatError, match="label"):
        formats.write_dict([], ["l" * 200])


def test_annot_and_dict_carry_the_same_stamp():
    entries = [CardEntry("particle", "を", "", "particle", ("marks the direct object",))]
    annot, dictionary = formats.paired_files({41: [formats.AnnotFile(0, [LINE])]}, entries, ["past"])
    assert struct.unpack_from("<I", dictionary, 4)[0] == 3                    # dict.bin version 3: entries carry ids
    stamp = struct.unpack_from("<I", dictionary, 24)[0]
    assert stamp and struct.unpack_from("<I", annot, 12)[0] == stamp
    assert formats.read_dict(dictionary) == (entries, ["past"])
    other = formats.paired_files({41: [formats.AnnotFile(0, [LINE])]}, entries, ["future"])[1]
    assert struct.unpack_from("<I", other, 24)[0] != stamp                    # another dict.bin, another stamp


def test_headword_hash_matches_the_payload():
    assert formats.headword_hash("") == 0x811C9DC5
    assert formats.headword_hash("a") == 0xE40C292C          # the payload's test_kokoro.c checks the same values
    assert formats.headword_hash("foobar") == 0xBF9CF968
    assert formats.headword_hash(" が ") == formats.headword_hash(unicodedata.normalize("NFD", "が"))


def test_ids_bin_round_trip():
    from jpreader_tools.dictionary import CardEntry
    entries = [CardEntry("dictionary", "それ", "それ", "pronoun", ("that",), 1006970),
               CardEntry("particle", "は", "", "particle", ("topic",)),
               CardEntry("dictionary", "ポケモン", "ポケモン", "noun", ("Pokémon",))]
    blob = formats.write_ids(entries, 0xABCD, {1006970: ["其れ", "其", "それ"]})
    magic, version, count, stamp, npairs = struct.unpack_from("<4sIIII", blob)
    assert (magic, version, count, stamp) == (b"JPRI", 2, 3, 0xABCD)
    got_stamp, ids, pairs = formats.read_ids(blob)
    assert (got_stamp, ids) == (0xABCD, [1006970, 0, 0])
    # Every spelling of entry 0's word (kokoro's card may say 其れ), and an id-less word by its headword;
    # never a particle. Sorted by entry, then hash, without repeats.
    h = formats.headword_hash
    assert pairs == sorted({(0, h("其れ")), (0, h("其")), (0, h("それ")), (2, h("ポケモン"))})
    assert npairs == 4 and len(blob) == 20 + 4 * 3 + 8 * 4

def test_names_round_trip():
    sections = [[[0x50, 0x6F]], [[0x30CE], [0x304B, 0x304F]], []]
    blob = formats.write_names(sections)
    assert blob[:4] == formats.NAMES_MAGIC
    assert formats.read_names(blob) == sections


@pytest.mark.rom
def test_names_from_the_game(rom):
    from jpreader_tools.build import names_bin
    from jpreader_tools.text import render
    names = formats.read_names(names_bin(rom))
    text = lambda section, i: render(names[section][i])
    assert text(formats.NAMES_MOVES, 1) == "Pound" and text(formats.NAMES_MOVES, 33) == "Tackle"
    assert len(names[formats.NAMES_MOVES]) == 729
    assert [text(formats.NAMES_TYPES_EN, i) for i in (0, 9)] == ["Normal", "Fire"]
    assert [text(formats.NAMES_TYPES_KANA, i) for i in (0, 9)] == ["ノーマル", "ほのお"]
    assert [text(formats.NAMES_LABELS_KANA, i) for i in range(4)] == ["たたかう", "バッグ", "ポケモン", "にげる"]
    assert [text(formats.NAMES_LABELS_EN, i) for i in range(4)] == ["FIGHT", "BAG", "POKÉMON", "RUN"]


def test_the_screen_key_is_the_payloads():
    # payload/host_tests/test_strings.c checks the same key and hash
    text = [0x0A, 0x305F, 0x305F, 0x304B, 0x3046, 0x3000, 0x10, 0x02, 0xFF00, 0x01, 0x306D, 0x0A, 0x20]
    assert formats.screen_key(text) == [0x305F, 0x305F, 0x304B, 0x3046, 0x306D]
    assert formats.screen_hash([0x305F, 0x305F, 0x304B, 0x3046]) == 0xF7462340
    assert formats.screen_key([0x3042, 0x10, 0x05, 0x0100]) == [0x3042]


def test_strings_round_trip_and_lookup():
    fight = [0x305F, 0x305F, 0x304B, 0x3046]
    bag = [0x30D0, 0x30C3, 0x30B0]
    u = lambda s: [ord(c) for c in s]
    entries = [(fight, 32, 4, u("FIGHT"), u("fight")), (bag, 32, 3, u("BAG"), []),
               ([0x3042], 7, 9, [], u("ah")), ([0x3044], 7, 10, [], [])]
    blob = formats.write_strings(entries, buckets=4)
    assert blob[:4] == formats.STRINGS_MAGIC
    assert struct.unpack_from("<I", blob, 4)[0] == formats.STRINGS_VERSION == 2
    table = formats.read_strings(blob)
    assert table[formats.screen_hash(fight)] == (32, 4, "FIGHT", "fight")
    assert table[formats.screen_hash(bag)] == (32, 3, "BAG", None)
    assert table[formats.screen_hash([0x3042])] == (7, 9, None, "ah")       # a literal without an English line
    assert table[formats.screen_hash([0x3044])] == (7, 10, None, None)      # neither: no strings at all


def test_strings_lay_the_literal_after_the_english():
    """core: the payload reads the English at the entry's offset, then the literal straight after it."""
    key = [0x3042]
    blob = formats.write_strings([(key, 1, 2, [ord("A")], [ord("b"), ord("c")])], buckets=1)
    entries_at = struct.unpack_from("<I", blob, 16)[0]
    offset = struct.unpack_from("<IHHI", blob, entries_at)[3]
    assert struct.unpack_from("<HHHHH", blob, offset) == (1, ord("A"), 2, ord("b"), ord("c"))


def test_strings_refuse_a_bucket_the_payload_cannot_read_whole():
    keys = [[0x3042 + k] for k in range(formats.STRINGS_BUCKET_MAX + 1)]
    with pytest.raises(ValueError, match="bucket"):
        formats.write_strings([(k, 1, 1, [], []) for k in keys], buckets=1)
    formats.write_strings([(k, 1, 1, [], []) for k in keys[:-1]], buckets=1)


def test_strings_of_another_version_are_refused():
    blob = bytearray(formats.write_strings([([0x3042], 1, 2, [], [])], buckets=1))
    struct.pack_into("<I", blob, 4, 1)
    with pytest.raises(ValueError, match="version"):
        formats.read_strings(bytes(blob))


def test_strings_keep_the_first_of_a_key():
    a = [0x3042]
    blob = formats.write_strings([(a, 1, 2, [ord("A")], []), (a, 5, 6, [ord("B")], [])], buckets=2)
    assert formats.read_strings(blob)[formats.screen_hash(a)] == (1, 2, "A", None)


@pytest.mark.rom
def test_strings_from_the_game(rom):
    from jpreader_tools.build import strings_bin
    table = formats.read_strings(strings_bin(rom))
    key = lambda s: formats.screen_hash(formats.screen_key([ord(c) for c in s]))
    assert table[key("たたかう")][2] == "FIGHT"
    assert table[key("はたく")][:3] == (118, 1, "Pound")
    assert table[key("モンスターボール")][2] == "Poké Ball"
    assert table[key("ピカチュウ")][2] == "Pikachu"
    assert len(table) > 20000
