import argparse
import json
from pathlib import Path

import pandas as pd
import pytest

from fairvaluelab.research_dataset import (
    build_metadata,
    load_provenance,
    parse_positive_integers,
    resolve_executable,
    summarize_dataset,
)


def test_parse_positive_integers_rejects_invalid_values() -> None:
    assert parse_positive_integers("10,50") == (10, 50)
    with pytest.raises(argparse.ArgumentTypeError):
        parse_positive_integers("10,10")
    with pytest.raises(argparse.ArgumentTypeError):
        parse_positive_integers("0")


def test_load_provenance_and_build_metadata(tmp_path: Path) -> None:
    provenance = {
        "venue": "alpha",
        "instrument": "BTC/USD",
        "source": {"websocket": "wss://example.test"},
        "event_count": 12,
        "capture_start_timestamp_ns": 100,
        "capture_end_timestamp_ns": 200,
        "capture_start_utc": "2026-01-01T00:00:00Z",
        "capture_end_utc": "2026-01-01T00:00:01Z",
        "software_revision": "abc123",
    }
    (tmp_path / "alpha.metadata.json").write_text(json.dumps(provenance), encoding="utf-8")
    loaded = load_provenance(tmp_path)
    metadata = build_metadata(loaded, Path("normalized.csv"), 50, 100, [])
    assert metadata["venue_set"] == ["alpha"]
    assert metadata["instruments"] == {"alpha": "BTC/USD"}
    assert metadata["time_period"]["start_timestamp_ns"] == 100
    assert metadata["sampling_interval_ns"] == 50
    assert metadata["max_target_delay_ns"] == 100


def test_summarize_dataset_records_targets_venues_and_validation() -> None:
    dataset = pd.DataFrame(
        {
            "valid_venue_count": [1, 2, 2],
            "target_timestamp_10": pd.array([20, 30, None], dtype="UInt64"),
        }
    )
    summary = summarize_dataset(dataset, Path("dataset.csv"), 25)
    assert summary["rows"] == 3
    assert summary["missing_target_counts"] == {"10": 1}
    assert summary["valid_venue_counts"]["distribution"] == {"1": 1, "2": 2}
    assert summary["leakage_validation"] == "passed"


def test_resolve_executable_accepts_windows_suffix(tmp_path: Path) -> None:
    executable = tmp_path / "fvl_dataset.exe"
    executable.touch()
    assert resolve_executable(tmp_path, "fvl_dataset") == executable
