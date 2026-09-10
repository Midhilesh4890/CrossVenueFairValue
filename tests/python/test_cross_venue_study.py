from __future__ import annotations

import numpy as np
import pandas as pd

from fairvaluelab.cross_venue_study import block_bootstrap_deltas, compare_cross_venue


def research_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10_000_000
    signal = np.sin(index / 4.0)
    target = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_spread_ticks": 2.0 + (index % 3),
            "venue_1_imbalance_l1": signal * 0.5,
            "venue_2_microprice_minus_consolidated_microprice": -signal,
            "venue_2_imbalance_l1": -signal * 0.4,
            "pair_1_2_mid_difference": signal * 2.0,
            "target_timestamp_10000000": timestamps + 10_000_000,
            "target_delay_ns_10000000": 0,
            "mid_return_10000000": target,
            "mid_direction_10000000": target.astype(int),
        }
    )


def test_block_bootstrap_is_deterministic_and_paired() -> None:
    actual = np.arange(20, dtype=float)
    local = actual + 2.0
    cross = actual + 1.0
    first_mae, first_ic = block_bootstrap_deltas(actual, local, cross, 5, 100, 7)
    second_mae, second_ic = block_bootstrap_deltas(actual, local, cross, 5, 100, 7)
    np.testing.assert_array_equal(first_mae, second_mae)
    np.testing.assert_array_equal(first_ic, second_ic)
    np.testing.assert_allclose(first_mae, -1.0)


def test_cross_venue_comparison_reports_deltas_and_uncertainty() -> None:
    results = compare_cross_venue(research_dataset(), 1, 5, 100, 0)
    assert len(results) == 1
    assert results.loc[0, "horizon_ns"] == 10_000_000
    assert results.loc[0, "cross_venue_feature_count"] > results.loc[0, "local_feature_count"]
    assert np.isclose(
        results.loc[0, "delta_mae"],
        results.loc[0, "cross_venue_mae"] - results.loc[0, "local_mae"],
    )
    assert results.loc[0, "bootstrap_replicates"] == 100
    assert results.loc[0, "target_standard_deviation"] > 0.0
