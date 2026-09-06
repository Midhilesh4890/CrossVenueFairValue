from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from fairvaluelab.baseline import (
    baseline_feature_groups,
    chronological_split,
    evaluate_dataset,
    evaluate_horizon,
    main,
)


def research_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10_000_000
    signal = np.sin(index / 4.0)
    mid_return = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_spread_ticks": 2.0 + (index % 3),
            "venue_1_imbalance_l1": signal * 0.5,
            "venue_1_ofi_event_window": np.cos(index / 5.0),
            "venue_1_signed_trade_volume_event_window": (index % 7) - 3.0,
            "venue_2_microprice_minus_consolidated_microprice": -signal,
            "venue_2_spread_ticks": 3.0 + (index % 2),
            "venue_2_imbalance_l1": -signal * 0.4,
            "pair_1_2_mid_difference": signal * 2.0,
            "pair_1_2_microprice_difference": signal * 1.5,
            "pair_1_2_both_fresh": 1,
            "target_timestamp_10000000": timestamps + 10_000_000,
            "target_delay_ns_10000000": 0,
            "mid_return_10000000": mid_return,
            "microprice_return_10000000": mid_return * 0.8,
            "mid_direction_10000000": mid_return.astype(int),
            "microprice_direction_10000000": mid_return.astype(int),
        }
    )


def test_feature_groups_are_nested_and_exclude_freshness_flags() -> None:
    groups = baseline_feature_groups(research_dataset(), primary_venue_id=1)
    assert groups["microprice_deviation"] == [
        "venue_1_microprice_minus_consolidated_microprice"
    ]
    assert "venue_1_spread_ticks" in groups["local_microstructure"]
    assert (
        "venue_1_microprice_minus_consolidated_microprice"
        not in groups["local_microstructure"]
    )
    assert "pair_1_2_mid_difference" in groups["local_plus_cross_venue"]
    assert "venue_2_imbalance_l1" in groups["local_plus_cross_venue"]
    assert "pair_1_2_both_fresh" not in groups["local_plus_cross_venue"]


def test_chronological_split_purges_boundary_crossing_targets() -> None:
    dataset = research_dataset()
    split = chronological_split(dataset)
    assert split.validation_start_ns == int(dataset.loc[70, "sample_timestamp_ns"])
    assert split.test_start_ns == int(dataset.loc[85, "sample_timestamp_ns"])
    assert split.purged_train_rows == 1
    assert split.purged_validation_rows == 1
    assert split.train["sample_timestamp_ns"].max() < split.validation_start_ns
    assert split.validation["sample_timestamp_ns"].min() >= split.validation_start_ns
    assert split.validation["sample_timestamp_ns"].max() < split.test_start_ns
    assert split.test["sample_timestamp_ns"].min() >= split.test_start_ns
    assert split.train["target_timestamp_10000000"].max() < split.validation_start_ns
    assert split.validation["target_timestamp_10000000"].max() < split.test_start_ns


def test_chronological_split_keeps_equal_timestamps_together() -> None:
    dataset = research_dataset(20)
    dataset.loc[13:14, "sample_timestamp_ns"] = dataset.loc[14, "sample_timestamp_ns"]
    dataset.loc[13:14, "venue_1_latest_local_receipt_timestamp_ns"] = dataset.loc[
        13:14, "sample_timestamp_ns"
    ]
    dataset.loc[13:14, "target_timestamp_10000000"] = (
        dataset.loc[13:14, "sample_timestamp_ns"] + 10_000_000
    )
    split = chronological_split(dataset)
    duplicate_timestamp = int(dataset.loc[14, "sample_timestamp_ns"])
    containing_partitions = sum(
        duplicate_timestamp in set(frame["sample_timestamp_ns"])
        for frame in (split.train, split.validation, split.test)
    )
    assert containing_partitions == 1


def test_chronological_split_rejects_invalid_configuration() -> None:
    dataset = research_dataset()
    with pytest.raises(ValueError, match="train_fraction"):
        chronological_split(dataset, train_fraction=0.0)
    with pytest.raises(ValueError, match="leave a test"):
        chronological_split(dataset, train_fraction=0.8, validation_fraction=0.2)


def test_baseline_evaluation_reports_all_feature_groups() -> None:
    split = chronological_split(research_dataset())
    development = pd.concat([split.train, split.validation])
    results = evaluate_horizon(development, split.test, 10_000_000)
    assert list(results["features"]) == [
        "microprice_deviation",
        "local_microstructure",
        "local_plus_cross_venue",
    ]
    assert results["mae"].notna().all()
    assert results["accuracy"].between(0.0, 1.0).all()


def test_dataset_evaluation_and_cli(tmp_path, capsys) -> None:
    path = tmp_path / "research.csv"
    research_dataset().to_csv(path, index=False)
    split, results = evaluate_dataset(research_dataset())
    assert len(results) == 3
    assert len(split.test) == 15

    assert main(["--dataset", str(path), "--primary-venue", "1"]) == 0
    output = capsys.readouterr().out
    assert "10ms" in output
    assert "local_plus_cross_venue" in output
    assert "purged: train=1, validation=1" in output
