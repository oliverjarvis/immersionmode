"""A stand-in for kokoro's jpreader routes, for the emulator: GET /jpreader/words (v1 by front, v2 by JMdict id),
POST /jpreader/word (v1, and v2 with jmdict=), POST /jpreader/explain (with grammar points) and POST /jpreader/grammar
over HTTPS on 127.0.0.1, answering as kokoro does (spec: kokoro integration 2, word identity 6). Its certificate comes
from the test CA that only debug builds trust (tls.ensure_test_ca), so runs never touch the real kokoro."""
from __future__ import annotations

import ssl
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


# kokoro's explanation, its grammar points' statuses filled in from the stand-in's own table (protocol.ts
# formatExplain): a point under each chunk it fits, then those of no chunk. ちょっと has no Bunpro match ("-").
EXPLAIN_TEMPLATE = ('T "Wait a moment."\nC ちょっと\nM a little\nN adverb\nG -\tnone\tちょっと\ta little: softens a request\n'
                    "C 待って\nM wait\nN te-form request\n"
                    "G bunpro:1082\t{1082}\t〜て\tte-form: links to what follows, here a request\n"
                    "G bunpro:31\t{31}\tください\tplease (polite request)\n"
                    "G bunpro:2000\t{2000}\t〜ましょう\tlet's (volitional)\n"
                    "G bunpro:3000\t{3000}\tなさい\tcommand\n")
GRAMMAR_IDS = (1082, 31, 2000, 3000)    # the Bunpro points the stand-in knows; any other id answers 400
GRAMMAR = {31: "known"}                 # the learner's statuses at the start; the rest are "none"


def explain_text(grammar: dict[int, str]) -> str:
    text = EXPLAIN_TEMPLATE
    for point in GRAMMAR_IDS:
        text = text.replace(f"{{{point}}}", grammar.get(point, "none"))
    return text


EXPLAIN_REPLY = explain_text(GRAMMAR)


class StandIn:
    def __init__(self, ca_dir: Path, token: str, words: dict[str, str] | None = None,
                 ids: dict[int, str] | None = None, fallback: dict[str, str] | None = None):
        self.ca_dir = ca_dir
        self.token = token
        self.words: dict[str, str] = dict(words or {})          # v1: by front
        self.ids: dict[int, str] = dict(ids or {})              # v2: by JMdict id
        self.fallback: dict[str, str] = dict(fallback or {})    # v2: kokoro cards with no id, by front
        self.unmet: set[int] = set()                             # v2: Kaishi words not met yet: an add queues them
        self.added: list[dict[str, str]] = []
        self.explained: list[dict[str, str]] = []
        self.grammar: dict[int, str] = dict(GRAMMAR)            # grammar points in reviews, by Bunpro id
        self.grammar_added: list[str] = []
        self.explain_status = 200
        self.log: list[str] = []
        self.lock = threading.Lock()
        self._server: ThreadingHTTPServer | None = None

    @property
    def url(self) -> str:
        assert self._server is not None, "start() first"
        return f"https://localhost:{self._server.server_address[1]}"

    def words_text(self) -> str:
        with self.lock:
            rows = list(self.words.items())
        return f"KOKORO-WORDS 1 {len(rows)}\n" + "".join(f"{front}\t{state}\n" for front, state in rows)

    def words_text_v2(self) -> str:
        """Known and learning words only: an unstudied word is offered the add, as kokoro leaves it out."""
        with self.lock:
            rows = [(str(i), s) for i, s in self.ids.items() if s != "new"]
            rows += [(f"~{front}", s) for front, s in self.fallback.items() if s != "new"]
        return f"KOKORO-WORDS 2 {len(rows)}\n" + "".join(f"{key}\t{state}\n" for key, state in rows)

    def add(self, fields: dict[str, str]) -> str:
        with self.lock:
            self.added.append(fields)
            if "jmdict" in fields:                      # v2: exists, queued (kokoro had it unstudied), or added
                word = int(fields["jmdict"])
                if self.ids.get(word) in ("known", "learning"):
                    return f"OK exists {self.ids[word]}\n"
                if word in self.ids or word in self.unmet:
                    self.unmet.discard(word)
                    self.ids[word] = "new"
                    return "OK queued\n"
                self.ids[word] = "new"
                return "OK added\n"
            front = fields["front"]
            if front in self.words:
                return f"OK exists {self.words[front]}\n"
            self.words[front] = "new"
            return "OK new\n"

    def add_grammar(self, key: str) -> tuple[int, str]:
        """POST /jpreader/grammar: OK new, OK exists <status>, or 400 for a point kokoro does not know."""
        with self.lock:
            self.grammar_added.append(key)
            point = int(key.split(":", 1)[1]) if key.startswith("bunpro:") and key[7:].isdigit() else None
            if point not in GRAMMAR_IDS:
                return 400, "unknown grammar point\n"
            if point in self.grammar:
                return 200, f"OK exists {self.grammar[point]}\n"
            self.grammar[point] = "new"
            return 200, "OK new\n"

    def explain_text(self) -> str:
        with self.lock:
            return explain_text(self.grammar)

    def start(self) -> "StandIn":
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self.ca_dir / "server.pem", self.ca_dir / "server.key")
        stand_in = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args) -> None:
                pass

            def answer(self, status: int, text: str) -> None:
                data = text.encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)
                stand_in.log.append(f"{self.command} {self.path} {status}")

            def authorised(self) -> bool:
                if self.headers.get("Authorization") == f"Bearer {stand_in.token}":
                    return True
                self.answer(401, "kokoro rejected the device token\n")
                return False

            def do_GET(self) -> None:
                if not self.authorised():
                    return
                if self.path == "/jpreader/words?v=2":
                    self.answer(200, stand_in.words_text_v2())
                elif self.path == "/jpreader/words":
                    self.answer(200, stand_in.words_text())
                else:
                    self.answer(404, "no such route\n")

            def do_POST(self) -> None:
                body = self.rfile.read(int(self.headers.get("Content-Length") or 0)).decode("utf-8")
                if not self.authorised():
                    return
                if self.path not in ("/jpreader/word", "/jpreader/explain", "/jpreader/grammar"):
                    self.answer(404, "no such route\n")
                    return
                fields: dict[str, str] = {}
                for line in body.splitlines():
                    if not line:
                        continue
                    if "=" not in line:
                        self.answer(400, "kokoro error: a line without =\n")
                        return
                    key, value = line.split("=", 1)
                    fields[key] = value
                if self.path == "/jpreader/explain":
                    if not fields.get("sentence"):
                        self.answer(400, "kokoro error: sentence is required\n")
                        return
                    stand_in.explained.append(fields)
                    if stand_in.explain_status != 200:
                        self.answer(stand_in.explain_status, "kokoro error: explaining failed\n")
                        return
                    self.answer(200, stand_in.explain_text())
                    return
                if self.path == "/jpreader/grammar":
                    if not fields.get("key", "").strip():
                        self.answer(400, "missing key\n")
                        return
                    self.answer(*stand_in.add_grammar(fields["key"].strip()))
                    return
                if not fields.get("front"):
                    self.answer(400, "kokoro error: front is required\n")
                    return
                self.answer(200, stand_in.add(fields))

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self._server.socket = context.wrap_socket(self._server.socket, server_side=True)
        threading.Thread(target=self._server.serve_forever, daemon=True).start()
        return self

    def stop(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
