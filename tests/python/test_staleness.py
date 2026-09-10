from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

from fairvaluelab.staleness import evaluate_staleness, parse_dataset_spec


def sensitivity_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10_000_000
    signal = np.sin(index / 4.0)
    target = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "valid_venue_count": 1 + index % 2,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_spread_ticks": 2.0 + index % 2,
            "venue_1_imbalance_l1": signal,
            "venue_2_microprice_minus_consolidated_microprice": -signal,
            "venue_2_spread_ticks": 3.0 + index % 2,
            "pair_1_2_mid_difference": signal,
            "target_timestamp_10000000": timestamps + 10_000_000,
            "target_delay_ns_10000000": 0,
            "mid_return_10000000": target,
            "mid_direction_10000000": target.astype(int),
        }
    )


def test_dataset_spec_parsing() -> None:
    assert parse_dataset_spec("25000000=data.csv") == (25_000_000, Path("data.csv"))
    with pytest.raises(argparse.ArgumentTypeError):
        parse_dataset_spec("data.csv")


def test_staleness_results_include_coverage_and_metrics() -> None:
    first = sensitivity_dataset()
    second = sensitivity_dataset()
    second["valid_venue_count"] = 2
    results = evaluate_staleness([(25, first), (50, second)], 1)
    assert len(results) == 2
    assert results.loc[0, "all_venues_valid_fraction"] == 0.5
    assert results.loc[1, "all_venues_valid_fraction"] == 1.0
    assert set(results["evaluation_status"]) == {"completed"}
