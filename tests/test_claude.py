import json

import pytest

from jpreader_tools import claude


class Fake:
    def __init__(self, answers):
        self.answers, self.calls = list(answers), []

    def __call__(self, method, url, key, body=None):
        self.calls.append((method, url, body))
        return self.answers.pop(0)


def test_ask_sends_structured_output_without_thinking(monkeypatch):
    fake = Fake([(200, json.dumps({"content": [{"type": "text", "text": '{"a": 1}'}], "stop_reason": "end_turn"}).encode())])
    monkeypatch.setattr(claude, "_request", fake)
    assert claude.ask("k", "claude-haiku-5-5", "S", "U", {"type": "object"}) == {"a": 1}
    method, url, body = fake.calls[0]
    assert (method, url) == ("POST", "https://api.anthropic.com/v1/messages")
    assert body["thinking"] == {"type": "disabled"} and body["output_config"]["format"]["type"] == "json_schema"
    assert body["system"] == "S" and body["messages"] == [{"role": "user", "content": "U"}]


def test_ask_raises_on_an_error(monkeypatch):
    monkeypatch.setattr(claude, "_request", Fake([(401, b'{"error":{"message":"invalid x-api-key"}}')]))
    with pytest.raises(claude.ClaudeError, match="401"):
        claude.ask("k", "m", "s", "u", {})


def test_batch_submit_status_results(monkeypatch, tmp_path):
    lines = "\n".join(json.dumps(r) for r in [
        {"custom_id": "a", "result": {"type": "succeeded", "message": {"content": [{"type": "text", "text": '{"x": 1}'}]}}},
        {"custom_id": "b", "result": {"type": "expired"}},
    ])
    fake = Fake([(200, b'{"id": "msgbatch_1", "processing_status": "in_progress"}'),
                 (200, b'{"id": "msgbatch_1", "processing_status": "ended", "results_url": "https://r/1"}'),
                 (200, b'{"id": "msgbatch_1", "processing_status": "ended", "results_url": "https://r/1"}'),
                 (200, lines.encode())])
    monkeypatch.setattr(claude, "_request", fake)
    batch = claude.Batch("k", tmp_path)
    assert batch.submit([{"custom_id": "a", "params": {}}, {"custom_id": "b", "params": {}}]) == "msgbatch_1"
    assert batch.status("msgbatch_1") == "ended"
    assert batch.results("msgbatch_1") == {"a": {"x": 1}, "b": None}
    assert fake.calls[0][1] == "https://api.anthropic.com/v1/messages/batches"


def test_estimate():
    assert claude.estimate_usd(1_000_000, 0) == pytest.approx(0.05)
    assert claude.estimate_usd(1_000_000, 1_000_000, batch=False) == pytest.approx(0.60)
