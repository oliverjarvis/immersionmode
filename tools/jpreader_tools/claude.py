"""The build's Claude calls: ordinary Messages requests and Message Batches, on the Max plan's API credit
(ANTHROPIC_API_KEY in .env, never printed). Structured output keeps every answer to a schema."""
from __future__ import annotations

import json
import urllib.error
import urllib.request
from pathlib import Path

API = "https://api.anthropic.com/v1"
VERSION = "2023-06-01"
MODEL = "claude-haiku-5-5"
PRICE_PER_M = {"input": 0.10, "output": 0.50}     # Haiku 5.5 ordinary; batches cost half


class ClaudeError(Exception):
    pass


def read_key(root: Path) -> str | None:
    from .build import read_env
    return read_env(root, "ANTHROPIC_API_KEY")


def _request(method: str, url: str, key: str, body: dict | None = None) -> tuple[int, bytes]:
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(url, data=data, method=method, headers={
        "x-api-key": key, "anthropic-version": VERSION, "content-type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def thinking_off(model: str) -> dict:
    """Sonnet 5.5 turns up-front thinking off with "between_tools"; Haiku 5.5 with "disabled"."""
    return {"type": "between_tools"} if "sonnet" in model else {"type": "disabled"}


def params(model: str, system: str, user: str, schema: dict, max_tokens: int = 2000,
           effort: str | None = None) -> dict:
    """A Messages request held to schema. With effort, the model thinks first (adaptive thinking at that
    effort; thinking counts toward max_tokens); without, thinking is off."""
    request = {"model": model, "max_tokens": max_tokens, "system": system,
               "messages": [{"role": "user", "content": user}],
               "output_config": {"format": {"type": "json_schema", "schema": schema}}}
    if effort:
        request["output_config"]["effort"] = effort
    else:
        request["thinking"] = thinking_off(model)
    return request


def _text(message: dict) -> str:
    return next((block["text"] for block in message.get("content", []) if block.get("type") == "text"), "")


def ask(key: str, model: str, system: str, user: str, schema: dict, max_tokens: int = 2000,
        effort: str | None = None) -> dict:
    status, data = _request("POST", f"{API}/messages", key, params(model, system, user, schema, max_tokens, effort))
    if status != 200:
        raise ClaudeError(f"Anthropic returned {status}: {data[:200].decode(errors='replace')}")
    return json.loads(_text(json.loads(data)))


def estimate_usd(input_tokens: int, output_tokens: int, batch: bool = True) -> float:
    cost = (input_tokens * PRICE_PER_M["input"] + output_tokens * PRICE_PER_M["output"]) / 1_000_000
    return cost / 2 if batch else cost


class Batch:
    def __init__(self, key: str, folder: Path):
        self.key, self.folder = key, folder

    def _get(self, url: str) -> bytes:
        status, data = _request("GET", url, self.key)
        if status != 200:
            raise ClaudeError(f"Anthropic returned {status}: {data[:200].decode(errors='replace')}")
        return data

    def submit(self, requests: list[dict]) -> str:
        status, data = _request("POST", f"{API}/messages/batches", self.key, {"requests": requests})
        if status != 200:
            raise ClaudeError(f"Anthropic returned {status}: {data[:200].decode(errors='replace')}")
        return json.loads(data)["id"]

    def status(self, batch_id: str) -> str:
        return json.loads(self._get(f"{API}/messages/batches/{batch_id}"))["processing_status"]

    def results(self, batch_id: str) -> dict[str, dict | None]:
        info = json.loads(self._get(f"{API}/messages/batches/{batch_id}"))
        out: dict[str, dict | None] = {}
        for line in self._get(info["results_url"]).decode().splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            result = row["result"]
            try:
                out[row["custom_id"]] = json.loads(_text(result["message"])) if result["type"] == "succeeded" else None
            except (KeyError, json.JSONDecodeError):
                out[row["custom_id"]] = None
        return out
