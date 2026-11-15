import ssl
import urllib.error
import urllib.request

from jpreader_tools import standin, tls


def test_standin_serves_words_and_takes_adds(tmp_path):
    ca = tls.ensure_test_ca(tmp_path)
    server = standin.StandIn(ca, "kkr_t", {"猫": "known"}).start()
    context = ssl.create_default_context(cafile=str(ca / "ca.pem"))

    def call(method, path, token="kkr_t", body=None):
        request = urllib.request.Request(server.url + path, data=body, method=method,
                                         headers={"Authorization": f"Bearer {token}"})
        try:
            with urllib.request.urlopen(request, context=context, timeout=10) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    try:
        assert server.url.startswith("https://localhost:")
        assert call("GET", "/jpreader/words") == (200, "KOKORO-WORDS 1 1\n猫\tknown\n")
        assert call("GET", "/jpreader/words", token="kkr_wrong")[0] == 401
        assert call("POST", "/jpreader/word", body="front=犬\nreading=いぬ\n".encode()) == (200, "OK new\n")
        assert call("POST", "/jpreader/word", body="front=猫\n".encode()) == (200, "OK exists known\n")
        assert call("POST", "/jpreader/word", body=b"nonsense") [0] == 400
        assert server.added[0] == {"front": "犬", "reading": "いぬ"} and server.words["犬"] == "new"
        assert call("GET", "/jpreader/words")[1] == "KOKORO-WORDS 1 2\n猫\tknown\n犬\tnew\n"
    finally:
        server.stop()


def test_standin_explains_and_can_fail(tmp_path):
    ca = tls.ensure_test_ca(tmp_path)
    server = standin.StandIn(ca, "kkr_t").start()
    context = ssl.create_default_context(cafile=str(ca / "ca.pem"))

    def post(path, body):
        request = urllib.request.Request(server.url + path, data=body.encode(), method="POST",
                                         headers={"Authorization": "Bearer kkr_t"})
        try:
            with urllib.request.urlopen(request, context=context, timeout=10) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    try:
        assert post("/jpreader/explain", "sentence=待って\ncontext=ちょっと 待って\n") == (200, standin.EXPLAIN_REPLY)
        assert server.explained[-1] == {"sentence": "待って", "context": "ちょっと 待って"}
        assert "\nG " in standin.EXPLAIN_REPLY                      # carries a grammar point, as kokoro's does
        server.explain_status = 500
        assert post("/jpreader/explain", "sentence=x\n") == (500, "kokoro error: explaining failed\n")
        assert post("/jpreader/explain", "context=x\n")[0] == 400
    finally:
        server.stop()


def test_standin_speaks_v2_by_id(tmp_path):
    ca = tls.ensure_test_ca(tmp_path)
    server = standin.StandIn(ca, "kkr_t", ids={1467640: "known", 1250050: "learning", 1000001: "new"},
                             fallback={"其れ": "known"}).start()
    server.unmet.add(1591110)
    context = ssl.create_default_context(cafile=str(ca / "ca.pem"))

    def call(method, path, body=None):
        request = urllib.request.Request(server.url + path, data=body, method=method,
                                         headers={"Authorization": "Bearer kkr_t"})
        with urllib.request.urlopen(request, context=context, timeout=10) as response:
            return response.status, response.read().decode()

    try:
        # Unstudied words are left out, as kokoro leaves them out.
        assert call("GET", "/jpreader/words?v=2") == (200, "KOKORO-WORDS 2 3\n1467640\tknown\n1250050\tlearning\n~其れ\tknown\n")
        assert call("POST", "/jpreader/word", "jmdict=1467640\nfront=猫\n".encode()) == (200, "OK exists known\n")
        assert call("POST", "/jpreader/word", "jmdict=1591110\nfront=聞く\n".encode()) == (200, "OK queued\n")
        assert call("POST", "/jpreader/word", "jmdict=1000001\nfront=語\n".encode()) == (200, "OK queued\n")
        assert call("POST", "/jpreader/word", "jmdict=1234567\nfront=新\n".encode()) == (200, "OK added\n")
        assert call("POST", "/jpreader/word", "jmdict=1234567\nfront=新\n".encode()) == (200, "OK queued\n")
        assert server.ids[1234567] == "new" and 1591110 not in server.unmet
        assert call("POST", "/jpreader/word", "front=犬\n".encode()) == (200, "OK new\n")      # a v1 body, as before
    finally:
        server.stop()


def test_standin_explains_with_grammar_points_and_takes_grammar_adds(tmp_path):
    ca = tls.ensure_test_ca(tmp_path)
    server = standin.StandIn(ca, "kkr_t").start()
    context = ssl.create_default_context(cafile=str(ca / "ca.pem"))

    def post(path, body, token="kkr_t"):
        request = urllib.request.Request(server.url + path, data=body.encode(), method="POST",
                                         headers={"Authorization": f"Bearer {token}"})
        try:
            with urllib.request.urlopen(request, context=context, timeout=10) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    try:
        points = [line for line in standin.EXPLAIN_REPLY.splitlines() if line.startswith("G ")]
        assert [p.split("\t")[:2] for p in points] == [["G -", "none"], ["G bunpro:1082", "none"], ["G bunpro:31", "known"],
                                                       ["G bunpro:2000", "none"], ["G bunpro:3000", "none"]]
        assert all(len(p.split("\t")) == 4 for p in points)                 # kokoro's four fields
        assert post("/jpreader/grammar", "key=bunpro:1082\n") == (200, "OK new\n")
        assert post("/jpreader/grammar", "key=bunpro:1082\n") == (200, "OK exists new\n")
        assert post("/jpreader/grammar", "key=bunpro:31\n") == (200, "OK exists known\n")
        assert post("/jpreader/grammar", "key=bunpro:999999\n")[0] == 400     # an id kokoro does not know
        assert post("/jpreader/grammar", "key=nonsense\n")[0] == 400
        assert post("/jpreader/grammar", "front=x\n")[0] == 400               # no key
        assert post("/jpreader/grammar", "key=bunpro:2000\n", token="kkr_wrong")[0] == 401
        assert server.grammar_added == ["bunpro:1082", "bunpro:1082", "bunpro:31", "bunpro:999999", "nonsense"]
        # the next explanation shows kokoro's statuses as they are now
        reply = post("/jpreader/explain", "sentence=待って\n")[1]
        assert "G bunpro:1082\tnew\t" in reply and "G bunpro:2000\tnone\t" in reply
    finally:
        server.stop()
