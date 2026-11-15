import subprocess

import pytest

from jpreader_tools import build, kokoro, package

TOKEN = "kkr_" + "A" * 43


def fake_runner(stdout=f'"{TOKEN}"\n', returncode=0, stderr=""):
    calls = []

    def run(args, **kwargs):
        calls.append((args, kwargs))
        return subprocess.CompletedProcess(args, returncode, stdout=stdout, stderr=stderr)

    return run, calls


def kokoro_checkout(tmp_path):
    folder = tmp_path / "kokoro"
    folder.mkdir()
    (folder / ".env.local").write_text("CONVEX_DEPLOYMENT=dev:x\nCONVEX_SITE_URL=https://happy-otter-1.convex.site\n")
    return folder


def test_link_makes_a_token_and_saves_it_without_printing_it(tmp_path, capsys):
    env = tmp_path / ".env"
    env.write_text("JPREADER_ROM=/rom.cci\nOPENROUTER_API_KEY=sk-or-v1-x\nKOKORO_TOKEN=kkr_old\n")
    run, calls = fake_runner()
    kokoro.link(kokoro_checkout(tmp_path), "me@example.com", env, runner=run)

    args, kwargs = calls[0]
    assert args[:4] == ["npx", "convex", "run", "deviceTokens:create"]
    assert '"email": "me@example.com"' in args[4] and '"name": "3DS"' in args[4]
    assert kwargs["cwd"] == tmp_path / "kokoro"
    text = env.read_text()
    assert "JPREADER_ROM=/rom.cci\n" in text and "OPENROUTER_API_KEY=sk-or-v1-x\n" in text
    assert f"KOKORO_TOKEN={TOKEN}\n" in text and "kkr_old" not in text
    assert "KOKORO_URL=https://happy-otter-1.convex.site\n" in text
    assert TOKEN not in capsys.readouterr().out


def test_link_reports_the_cli_error_without_any_token(tmp_path):
    run, _ = fake_runner(stdout="", returncode=1, stderr=f"Error: No kokoro account for that email {TOKEN}")
    with pytest.raises(kokoro.LinkError) as error:
        kokoro.link(kokoro_checkout(tmp_path), "nobody@example.com", tmp_path / ".env", runner=run)
    assert "No kokoro account" in str(error.value) and TOKEN not in str(error.value)


def test_link_needs_the_site_url(tmp_path):
    folder = tmp_path / "kokoro"
    folder.mkdir()
    (folder / ".env.local").write_text("CONVEX_DEPLOYMENT=dev:x\n")
    run, calls = fake_runner()
    with pytest.raises(kokoro.LinkError, match="CONVEX_SITE_URL"):
        kokoro.link(folder, "me@example.com", tmp_path / ".env", runner=run)
    assert not calls                                    # nothing made when it could not be saved


def test_config_text_carries_kokoro_only_when_linked():
    plain = package.config_text(None, None)
    assert "kokoro" not in plain
    linked = package.config_text("https://a.convex.site", TOKEN)
    assert f"kokoro_url=https://a.convex.site\nkokoro_token={TOKEN}\n" in linked


def test_build_reads_the_kokoro_settings(tmp_path, monkeypatch):
    monkeypatch.delenv("KOKORO_URL", raising=False)
    monkeypatch.delenv("KOKORO_TOKEN", raising=False)
    (tmp_path / ".env").write_text(f"KOKORO_URL=https://a.convex.site\nKOKORO_TOKEN={TOKEN}\n")
    assert build.read_env(tmp_path, "KOKORO_URL") == "https://a.convex.site"
    assert build.read_env(tmp_path, "KOKORO_TOKEN") == TOKEN
    assert build.read_env(tmp_path, "MISSING") is None
