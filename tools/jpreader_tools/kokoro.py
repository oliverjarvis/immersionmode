"""Link jpreader to kokoro: make a device token in the user's kokoro account and keep it in .env.

kokoro (~/Development/kokoro) is the user's study app. Its Convex backend serves the jpreader
routes on its site URL to a device token (spec: jpreader and kokoro integration, section 1). The
token is made by kokoro's internal `deviceTokens:create` through the Convex CLI in the kokoro
checkout, which already knows the deployment, and is written to .env without being printed.
"""
from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

TOKEN_PATTERN = re.compile(r"kkr_[A-Za-z0-9_-]+")


class LinkError(Exception):
    pass


def _env_value(path: Path, key: str) -> str | None:
    if not path.exists():
        return None
    for line in path.read_text().splitlines():
        if line.startswith(f"{key}="):
            return line.split("=", 1)[1].strip().strip('"') or None
    return None


def set_env(path: Path, values: dict[str, str]) -> None:
    """Writes each key=value into a .env file, replacing earlier lines for those keys and keeping the rest."""
    lines = path.read_text().splitlines() if path.exists() else []
    kept = [line for line in lines if line.split("=", 1)[0] not in values]
    path.write_text("".join(f"{line}\n" for line in kept) + "".join(f"{k}={v}\n" for k, v in values.items()))


def link(kokoro_dir: Path, email: str, env_path: Path, runner=subprocess.run) -> None:
    site = _env_value(kokoro_dir / ".env.local", "CONVEX_SITE_URL")
    if not site:
        raise LinkError(f"no CONVEX_SITE_URL in {kokoro_dir / '.env.local'}: run `npx convex dev` there once")
    args = json.dumps({"email": email, "name": "3DS"})
    done = runner(["npx", "convex", "run", "deviceTokens:create", args], cwd=kokoro_dir, capture_output=True, text=True)
    if done.returncode != 0:
        message = TOKEN_PATTERN.sub("kkr_…", (done.stderr or done.stdout or "").strip())
        raise LinkError(f"kokoro could not make a token: {message}")
    lines = [line for line in done.stdout.splitlines() if line.strip()]
    try:
        token = json.loads(lines[-1])
    except (IndexError, json.JSONDecodeError):
        token = None
    if not isinstance(token, str) or not TOKEN_PATTERN.fullmatch(token):
        raise LinkError("kokoro answered without a token: is the jpreader API deployed?")
    set_env(env_path, {"KOKORO_URL": site.rstrip("/"), "KOKORO_TOKEN": token})
