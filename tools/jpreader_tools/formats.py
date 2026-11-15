"""annot.bin (units of every Japanese line) and dict.bin (card entries and ending labels)."""
from __future__ import annotations

import hashlib
import collections
import struct
import unicodedata
from dataclasses import dataclass

from .dictionary import CardEntry
from .lines import Segment

ANNOT_MAGIC = b"JPRA"
DICT_MAGIC = b"JPRW"
ANNOT_VERSION = 2
DICT_VERSION = 3         # 2: header carries the stamp shared with annot.bin; 3: each entry its JMdict id
MAX_SEGMENTS = 64        # per line record: the payload's placement table
MAX_UNITS = 256          # per line record: the payload's unit table
ENTRY_MAX = 512          # bytes the payload reads for one dict entry
LABEL_MAX = 128          # bytes the payload reads for one label
DICT_HEADER = 28         # "JPRW", version, entry count, label count, entry table, label table, stamp
NO_ENTRY = 0xFFFFFFFF
NO_LABEL = 0xFFFF
UNIT_KINDS = {"word": 1, "particle": 2, "name": 3}
ENTRY_KINDS = {"dictionary": 0, "particle": 1, "name": 2, "lemma": 3}


class FormatError(Exception):
    pass


@dataclass(frozen=True)
class UnitRecord:
    start: int
    length: int
    kind: str
    entry: int | None
    label: int | None


@dataclass(frozen=True)
class LineRecord:
    segments: tuple[Segment, ...]
    units: tuple[UnitRecord, ...]


@dataclass(frozen=True)
class AnnotFile:
    total: int           # the text file header's total length: how the payload finds memory-built messages
    lines: list


def _record_bytes(record: LineRecord) -> bytes:
    if len(record.segments) > MAX_SEGMENTS:
        raise FormatError(f"a line has {len(record.segments)} segments; the payload holds {MAX_SEGMENTS}")
    if len(record.units) > MAX_UNITS:
        raise FormatError(f"a line has {len(record.units)} units; the payload holds {MAX_UNITS}")
    if any(b.start < a.start + a.length for a, b in zip(record.units, record.units[1:])):
        raise FormatError("units overlap or are out of order")   # the panel walks them in order
    out = bytearray(struct.pack("<HH", len(record.segments), len(record.units)))
    for s in record.segments:
        out += struct.pack("<BBHHH", s.kind, 0, s.length, s.first, s.last)
    for u in record.units:
        out += struct.pack("<HHIHBB", u.start, u.length, NO_ENTRY if u.entry is None else u.entry,
                           NO_LABEL if u.label is None else u.label, UNIT_KINDS[u.kind], 0)
    return bytes(out)


def _align(out: bytearray) -> None:
    out += bytes(-len(out) % 4)


def write_annot(archives, stamp: int = 0) -> bytes:
    out = bytearray(ANNOT_MAGIC + struct.pack("<III", ANNOT_VERSION, len(archives), stamp))
    table = len(out)
    out += bytes(8 * len(archives))
    pending = []
    for a, (arc, files) in enumerate(sorted(archives.items())):
        files_at = len(out)
        struct.pack_into("<HHI", out, table + 8 * a, arc, len(files), files_at)
        out += bytes(12 * len(files))
        for f, file in enumerate(files):
            lines_at = len(out)
            struct.pack_into("<III", out, files_at + 12 * f, lines_at, len(file.lines), file.total)
            out += bytes(4 * len(file.lines))
            pending += [(lines_at + 4 * n, _record_bytes(r)) for n, r in enumerate(file.lines) if r is not None]
    shared: dict[bytes, int] = {}
    for where, data in pending:
        if data not in shared:
            _align(out)
            shared[data] = len(out)
            out += data
        struct.pack_into("<I", out, where, shared[data])
    return bytes(out)


def read_annot(blob: bytes):
    if blob[:4] != ANNOT_MAGIC:
        raise FormatError("not an annot.bin file")
    _version, count, _zero = struct.unpack_from("<III", blob, 4)
    kinds = {v: k for k, v in UNIT_KINDS.items()}
    archives = {}
    for a in range(count):
        arc, file_count, files_at = struct.unpack_from("<HHI", blob, 16 + 8 * a)
        files = []
        for f in range(file_count):
            lines_at, line_count, total = struct.unpack_from("<III", blob, files_at + 12 * f)
            lines = []
            for n in range(line_count):
                at = struct.unpack_from("<I", blob, lines_at + 4 * n)[0]
                if not at:
                    lines.append(None)
                    continue
                segments_n, units_n = struct.unpack_from("<HH", blob, at)
                segments = tuple(Segment(k, length, first, last) for k, _z, length, first, last in
                                 (struct.unpack_from("<BBHHH", blob, at + 4 + 8 * i) for i in range(segments_n)))
                base = at + 4 + 8 * segments_n
                units = []
                for i in range(units_n):
                    start, length, entry, label, kind, _z = struct.unpack_from("<HHIHBB", blob, base + 12 * i)
                    units.append(UnitRecord(start, length, kinds[kind], None if entry == NO_ENTRY else entry,
                                            None if label == NO_LABEL else label))
                lines.append(LineRecord(segments, tuple(units)))
            files.append(AnnotFile(total, lines))
        archives[arc] = files
    return archives


def _string(text: str) -> bytes:
    data = text.encode("utf-8")
    return struct.pack("<H", len(data)) + data


def write_dict(entries, labels) -> bytes:
    out = bytearray(DICT_MAGIC + struct.pack("<IIIIII", DICT_VERSION, len(entries), len(labels), 0, 0, 0))
    entries_at = len(out)
    out += bytes(4 * len(entries))
    labels_at = len(out)
    out += bytes(4 * len(labels))
    struct.pack_into("<II", out, 16, entries_at, labels_at)
    for i, entry in enumerate(entries):
        _align(out)
        start = len(out)
        struct.pack_into("<I", out, entries_at + 4 * i, start)
        out += struct.pack("<BBHI", ENTRY_KINDS[entry.kind], len(entry.senses), 0,
                           entry.jmdict_id if entry.kind == "dictionary" else 0)
        for text in (entry.headword, entry.reading, entry.pos, *entry.senses):
            out += _string(text)
        if len(out) - start > ENTRY_MAX:
            raise FormatError(f"dict entry {i} is {len(out) - start} bytes; the payload reads {ENTRY_MAX}")
    for i, label in enumerate(labels):
        data = _string(label)
        if len(data) > LABEL_MAX:
            raise FormatError(f"label {i} is {len(data)} bytes; the payload reads {LABEL_MAX}")
        struct.pack_into("<I", out, labels_at + 4 * i, len(out))
        out += data
    stamp = int.from_bytes(hashlib.sha256(out[DICT_HEADER:]).digest()[:4], "little") or 1
    struct.pack_into("<I", out, 24, stamp)
    return bytes(out)


IDS_MAGIC = b"JPRI"
IDS_VERSION = 2


def write_ids(entries, stamp: int, forms: dict[int, list[str]]) -> bytes:
    """ids.bin (stage 9C), with dict.bin's stamp: each entry's JMdict id (0 for names, particles and lemmas),
    then (entry, hash) pairs sorted by entry: `headword_hash` of every spelling of a dictionary entry's word
    (from `forms`, by id) and of its own headword. The 3DS marks kokoro's words by id from the first part,
    and kokoro cards that have no id by spelling from the second."""
    ids, pairs = [], set()
    for i, entry in enumerate(entries):
        dictionary = entry.kind == "dictionary"
        ids.append(entry.jmdict_id if dictionary else 0)
        if dictionary:
            for form in (entry.headword, *forms.get(entry.jmdict_id, ())):
                pairs.add((i, headword_hash(form)))
    out = bytearray(IDS_MAGIC + struct.pack("<IIII", IDS_VERSION, len(ids), stamp, len(pairs)))
    out += struct.pack(f"<{len(ids)}I", *ids)
    for entry, hashed in sorted(pairs):
        out += struct.pack("<II", entry, hashed)
    return bytes(out)


def read_ids(blob: bytes) -> tuple[int, list[int], list[tuple[int, int]]]:
    if blob[:4] != IDS_MAGIC:
        raise FormatError("not an ids.bin file")
    version, count, stamp, npairs = struct.unpack_from("<IIII", blob, 4)
    if version != IDS_VERSION:
        raise FormatError(f"ids.bin version {version}; this reads {IDS_VERSION}")
    ids = list(struct.unpack_from(f"<{count}I", blob, 20))
    flat = struct.unpack_from(f"<{2 * npairs}I", blob, 20 + 4 * count)
    return stamp, ids, list(zip(flat[0::2], flat[1::2]))


def headword_hash(text: str) -> int:
    """FNV-1a (32 bits) of a headword as kokoro's word list sends a front: NFC and trimmed. The payload
    hashes the list's lines the same way (core/kokoro.c), so equal words meet."""
    h = 0x811C9DC5
    for byte in unicodedata.normalize("NFC", text.strip()).encode("utf-8"):
        h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return h


def paired_files(archives, entries, labels) -> tuple[bytes, bytes]:
    """annot.bin and dict.bin carrying the same stamp: unit entries are indices into this dict.bin
    only, so the payload refuses a dict.bin from another build."""
    dictionary = write_dict(entries, labels)
    return write_annot(archives, struct.unpack_from("<I", dictionary, 24)[0]), dictionary


def _read_string(blob: bytes, at: int) -> tuple[str, int]:
    length = struct.unpack_from("<H", blob, at)[0]
    return blob[at + 2:at + 2 + length].decode("utf-8"), at + 2 + length


def read_dict(blob: bytes):
    if blob[:4] != DICT_MAGIC:
        raise FormatError("not a dict.bin file")
    _version, entry_count, label_count, entries_at, labels_at, _stamp = struct.unpack_from("<IIIIII", blob, 4)
    kinds = {v: k for k, v in ENTRY_KINDS.items()}
    entries = []
    for i in range(entry_count):
        at = struct.unpack_from("<I", blob, entries_at + 4 * i)[0]
        kind, sense_count, _zero, jmdict_id = struct.unpack_from("<BBHI", blob, at)
        at += 8
        texts = []
        for _ in range(3 + sense_count):
            text, at = _read_string(blob, at)
            texts.append(text)
        entries.append(CardEntry(kinds[kind], texts[0], texts[1], texts[2], tuple(texts[3:]), jmdict_id))
    labels = []
    for i in range(label_count):
        at = struct.unpack_from("<I", blob, labels_at + 4 * i)[0]
        labels.append(_read_string(blob, at)[0])
    return entries, labels


# names.bin (stage 13): names the peek shows in English beside the game's Japanese, from the cart's own text. Sections
# of UTF-16 strings, each a list indexed as the game indexes them (a move id, a type id):
NAMES_MAGIC = b"JPRN"
NAMES_VERSION = 1
NAMES_MOVES = 0               # English move names, by move id (system text file 118, arc 32)
NAMES_TYPES_KANA = 1          # type names by type id (file 112), kana and kanji game text, and English
NAMES_TYPES_KANJI = 2
NAMES_TYPES_EN = 3
NAMES_LABELS_KANA = 4         # a battle's command labels, baked into its layout: たたかう, バッグ, ポケモン, にげる (file 32)
NAMES_LABELS_KANJI = 5
NAMES_LABELS_EN = 6
NAMES_SECTIONS = 7


def write_names(sections) -> bytes:
    """Header: magic, version, section count, then (first, count) a section; then an offset a string; then the
    strings, each a u16 length and its UTF-16 units."""
    head = NAMES_MAGIC + struct.pack("<II", NAMES_VERSION, len(sections))
    total = sum(len(s) for s in sections)
    table, first = b"", 0
    for s in sections:
        table += struct.pack("<II", first, len(s))
        first += len(s)
    strings, offsets = b"", []
    base = len(head) + len(table) + 4 * total
    for s in sections:
        for units in s:
            offsets.append(base + len(strings))
            strings += struct.pack(f"<H{len(units)}H", len(units), *units)
    return head + table + struct.pack(f"<{total}I", *offsets) + strings


def read_names(blob: bytes):
    if blob[:4] != NAMES_MAGIC:
        raise ValueError("not a names.bin")
    _version, count = struct.unpack_from("<II", blob, 4)
    spans = [struct.unpack_from("<II", blob, 12 + 8 * k) for k in range(count)]
    total = sum(n for _f, n in spans)
    offsets = struct.unpack_from(f"<{total}I", blob, 12 + 8 * count)
    out = []
    for first, n in spans:
        section = []
        for at in offsets[first:first + n]:
            (length,) = struct.unpack_from("<H", blob, at)
            section.append(list(struct.unpack_from(f"<{length}H", blob, at + 2)))
        out.append(section)
    return out


STRINGS_MAGIC = b"JPRS"
STRINGS_VERSION = 2
STRINGS_HEADER = 24
STRINGS_ENTRY = 12
STRINGS_BUCKET_MAX = 32         # platform/screen.c BUCKET_READ: entries the payload reads of a bucket
SCREEN_KEY_MAX = 200           # platform/screen.c TEXT_MAX: a pane's text is hashed up to this many units
_KEY_SKIP = {0x0020, 0x3000, 0x000A, 0x000D}


def screen_key(units) -> list[int]:
    """The text without the game's tags, spaces and line breaks (core/strings.c: jpr_text_key)."""
    out, k, n = [], 0, len(units)
    while k < n:
        c = units[k]
        if c == 0x10:
            if k + 1 >= n or k + 2 + units[k + 1] > n:
                break
            k += 2 + units[k + 1]
            continue
        k += 1
        if c not in _KEY_SKIP:
            out.append(c)
    return out


def screen_hash(key) -> int:
    """FNV-1a over the key's units, little-endian bytes (core/strings.c: jpr_key_hash)."""
    h = 0x811C9DC5
    for u in key:
        for b in (u & 0xFF, u >> 8):
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def write_strings(entries, buckets: int = 4096) -> bytes:
    """strings.bin (stage 14; version 2, stage 15): text seen on a screen → where it is in the system text, its
    English and a literal translation.

    entries: (key units, file, line, English units, literal units); the first of a key wins. Header: magic, version,
    bucket count, entry count, entries' offset, strings' offset; then bucket b's first entry index (buckets + 1 of
    them); then the entries by bucket (hash & (buckets - 1)), each u32 hash, u16 file, u16 line, u32 offset of its
    strings (0: neither); then the strings: at an entry's offset a u16 length and the English units, straight after
    them a u16 length and the literal's units (either length may be 0)."""
    if buckets & (buckets - 1):
        raise ValueError("bucket count must be a power of two")
    seen, rows = set(), []
    for key, file, line, english, literal in entries:
        h = screen_hash(key)
        if h in seen:
            continue
        seen.add(h)
        rows.append((h, file, line, (tuple(english), tuple(literal))))
    rows.sort(key=lambda r: (r[0] & (buckets - 1), r[0]))
    fullest = max(collections.Counter(r[0] & (buckets - 1) for r in rows).values(), default=0)
    if fullest > STRINGS_BUCKET_MAX:
        raise ValueError(f"a strings.bin bucket holds {fullest} entries; the payload reads {STRINGS_BUCKET_MAX}")
    entries_at = STRINGS_HEADER + 4 * (buckets + 1)
    strings_at = entries_at + STRINGS_ENTRY * len(rows)
    strings, where = bytearray(), {}
    table = bytearray()
    for h, file, line, pair in rows:
        offset = 0
        if pair[0] or pair[1]:
            if pair not in where:
                where[pair] = strings_at + len(strings)
                for units in pair:
                    strings += struct.pack(f"<H{len(units)}H", len(units), *units)
            offset = where[pair]
        table += struct.pack("<IHHI", h, file, line, offset)
    starts, k = [], 0
    for b in range(buckets + 1):
        while k < len(rows) and (rows[k][0] & (buckets - 1)) < b:
            k += 1
        starts.append(k)
    head = STRINGS_MAGIC + struct.pack("<IIIII", STRINGS_VERSION, buckets, len(rows), entries_at, strings_at)
    return head + struct.pack(f"<{buckets + 1}I", *starts) + bytes(table) + bytes(strings)


def read_strings(blob: bytes) -> dict[int, tuple[int, int, str | None, str | None]]:
    """hash -> (file, line, English, literal); None for a string that is not there."""
    if blob[:4] != STRINGS_MAGIC:
        raise ValueError("not a strings.bin")
    version, _buckets, count, entries_at, _strings_at = struct.unpack_from("<IIIII", blob, 4)
    if version != STRINGS_VERSION:
        raise ValueError(f"strings.bin version {version}; this reads {STRINGS_VERSION}")
    out = {}
    for k in range(count):
        h, file, line, offset = struct.unpack_from("<IHHI", blob, entries_at + STRINGS_ENTRY * k)
        found: list = [None, None]
        for n in range(2 if offset else 0):
            (length,) = struct.unpack_from("<H", blob, offset)
            found[n] = "".join(map(chr, struct.unpack_from(f"<{length}H", blob, offset + 2))) or None
            offset += 2 + 2 * length
        out[h] = (file, line, found[0], found[1])
    return out
