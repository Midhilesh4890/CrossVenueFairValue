from __future__ import annotations

import pandas as pd
import pytest

from fairvaluelab.dataset import (
    DatasetValidationError,
    load_dataset,
    summarize_missing_values,
    summarize_target_distributions,
    target_horizons,
    validate_dataset,
    validate_target_leakage,
    validate_timestamp_ordering,
)


def valid_dataset() -> pd.DataFrame:
    return pd.DataFrame(
        {
            "sample_timestamp_ns": [100, 110, 120],
            "venue_1_latest_local_receipt_timestamp_ns": [100, 105, 120],
            "target_timestamp_10": [110, 120, pd.NA],
            "target_delay_ns_10": [0, 0, pd.NA],
            "mid_return_10": [1.0, -1.0, pd.NA],
            "microprice_return_10": [0.5, 0.0, pd.NA],
            "mid_direction_10": [1, -1, pd.NA],
            "microprice_direction_10": [1, 0, pd.NA],
        }
    )


def test_load_and_validate_dataset(tmp_path) -> None:
    path = tmp_path / "dataset.csv"
    valid_dataset().to_csv(path, index=False)
    loaded = load_dataset(path)
    assert len(loaded) == 3
    assert target_horizons(loaded) == [10]


def test_loader_preserves_uint64_timestamp_precision(tmp_path) -> None:
    path = tmp_path / "large_timestamps.csv"
    timestamp = 9_223_372_036_854_775_001
    path.write_text(
        "sample_timestamp_ns,venue_1_latest_local_receipt_timestamp_ns,"
        "target_timestamp_10,target_delay_ns_10\n"
        f"{timestamp},{timestamp},{timestamp + 10},0\n",
        encoding="utf-8",
    )
    loaded = load_dataset(path)
    assert int(loaded.loc[0, "sample_timestamp_ns"]) == timestamp
    assert int(loaded.loc[0, "target_timestamp_10"]) == timestamp + 10


def test_timestamp_ordering_validation() -> None:
    validate_timestamp_ordering(valid_dataset())
    unordered = valid_dataset().iloc[[1, 0, 2]].reset_index(drop=True)
    with pytest.raises(DatasetValidationError, match="nondecreasing"):
        validate_timestamp_ordering(unordered)


def test_target_and_feature_leakage_validation() -> None:
    validate_target_leakage(valid_dataset())

    pre_horizon = valid_dataset()
    pre_horizon.loc[0, "target_timestamp_10"] = 109
    pre_horizon.loc[0, "target_delay_ns_10"] = -1
    with pytest.raises(DatasetValidationError, match="precedes its horizon"):
        validate_target_leakage(pre_horizon)

    future_feature = valid_dataset()
    future_feature.loc[0, "venue_1_latest_local_receipt_timestamp_ns"] = 101
    with pytest.raises(DatasetValidationError, match="exceeds sample timestamp"):
        validate_target_leakage(future_feature)

    wrong_delay = valid_dataset()
    wrong_delay.loc[0, "target_delay_ns_10"] = 1
    with pytest.raises(DatasetValidationError, match="inconsistent"):
        validate_target_leakage(wrong_delay)


def test_empty_dataset_is_invalid() -> None:
    with pytest.raises(DatasetValidationError, match="no rows"):
        validate_dataset(valid_dataset().iloc[0:0])


def test_missing_and_target_summaries() -> None:
    dataset = valid_dataset()
    missing = summarize_missing_values(dataset).set_index("column")
    assert missing.loc["mid_return_10", "missing_count"] == 1
    assert missing.loc["mid_return_10", "missing_fraction"] == pytest.approx(1 / 3)

    targets = summarize_target_distributions(dataset).set_index("target")
    assert targets.loc["mid", "count"] == 2
    assert targets.loc["mid", "down"] == 1
    assert targets.loc["mid", "up"] == 1
    assert targets.loc["microprice", "unchanged"] == 1
