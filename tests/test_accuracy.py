import os

import pytest

from jpreader_tools import accuracy


def test_score_counts_matches():
    gold = [accuracy.GoldRow(41, 0, 0, 0, "きみ", "きみ", 200, True), accuracy.GoldRow(41, 0, 0, 1, "猫", "猫", 300, False)]
    chosen = {(41, 0, 0, 0): 200, (41, 0, 0, 1): 301}
    result = accuracy.score_rows(gold, chosen)
    assert result["overall"] == 0.5 and result["ambiguous"] == 1.0 and len(result["wrong"]) == 1


def test_gold_round_trip(tmp_path):
    rows = [accuracy.GoldRow(41, 3, 7, 2, "きみ", "きみ", 1, True, "you, said to the player")]
    path = tmp_path / "gold.tsv"
    accuracy.write_gold(path, rows)
    assert accuracy.read_gold(path) == rows
