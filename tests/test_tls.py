import subprocess
import base64
import hashlib
from pathlib import Path

import pytest

from jpreader_tools import tls

ROOT = Path(__file__).resolve().parent.parent


def _der(pem: Path) -> bytes:
    lines = pem.read_text().strip().splitlines()
    return base64.b64decode("".join(lines[1:-1]))


def test_certificates_are_the_pinned_roots():
    files = tls.certificate_files(ROOT)
    assert sorted(p.name for p in files) == sorted(tls.CERT_FINGERPRINTS)
    for path in files:
        assert hashlib.sha256(_der(path)).hexdigest() == tls.CERT_FINGERPRINTS[path.name]


def test_fetch_bearssl_checks_the_digest(tmp_path):
    (tmp_path / tls.BEARSSL_ARCHIVE).write_bytes(b"not a tarball")
    with pytest.raises(tls.TlsBuildError, match="SHA-256"):
        tls.fetch_bearssl(tmp_path)
    assert not (tmp_path / tls.BEARSSL_ARCHIVE).exists()


def test_trust_anchors_header():
    src = tls.fetch_bearssl(ROOT / "build" / "cache")
    header = tls.trust_anchors_header(tls.build_brssl(src), tls.certificate_files(ROOT))
    assert "#define TAs_NUM   7" in header or "#define TAs_NUM 7" in header
    assert header.count("BR_KEYTYPE_RSA") == 4 and header.count("BR_KEYTYPE_EC") == 3   # GTS R1, R2, GlobalSign, ISRG X1 are RSA


def test_only_debug_builds_trust_the_test_ca(tmp_path):
    certs = tmp_path / "payload" / "certs"
    certs.mkdir(parents=True)
    (certs / "root.pem").write_text("x")
    assert tls.trusted_certificates(tmp_path, debug=False) == [certs / "root.pem"]
    trusted = tls.trusted_certificates(tmp_path, debug=True)
    assert trusted[:-1] == [certs / "root.pem"]
    assert trusted[-1] == tmp_path / "build" / "test-ca" / "ca.pem" and trusted[-1].exists()


def test_the_test_ca_signs_a_localhost_certificate_once(tmp_path):
    folder = tls.ensure_test_ca(tmp_path)
    verified = subprocess.run(["openssl", "verify", "-CAfile", str(folder / "ca.pem"), str(folder / "server.pem")],
                              capture_output=True, text=True)
    assert verified.returncode == 0, verified.stderr
    text = subprocess.run(["openssl", "x509", "-in", str(folder / "server.pem"), "-noout", "-text"],
                          capture_output=True, text=True).stdout
    assert "DNS:localhost" in text
    first = (folder / "ca.pem").read_bytes()
    assert tls.ensure_test_ca(tmp_path) == folder and (folder / "ca.pem").read_bytes() == first
