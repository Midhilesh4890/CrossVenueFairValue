from __future__ import annotations

import numpy as np
import pandas as pd

from fairvaluelab.regime import analyze_regimes


def regime_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10_000_000
    signal = np.sin(index / 4.0)
    target = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    dataset = pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_spread_ticks": 2.0 + index % 3,
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
    dataset["consolidated_mid"] = 100.0 + np.sin(index / 5.0)
    dataset["venue_1_bid_depth"] = 10.0 + index % 5
    dataset["venue_1_ask_depth"] = 12.0 + index % 7
    dataset["venue_1_age_ns"] = index * 1_000
    dataset["venue_1_signed_trade_volume_time_window"] = np.sin(index / 3.0) * 10.0
    return dataset


def test_regime_analysis_uses_training_thresholds_and_reports_deltas() -> None:
    dataset = regime_dataset()
    results = analyze_regimes(dataset, 1)
    assert set(results["regime"]) == {
        "spread",
        "volatility",
        "depth",
        "trade_activity",
        "imbalance",
        "venue_age",
    }
    assert set(results["band"]) == {"low", "high"}
    assert (results["threshold_source"] == "training_partition_median").all()
    venue_age = results.loc[results["regime"] == "venue_age"]
    assert (venue_age["threshold"] == 34_000.0).all()
    np.testing.assert_allclose(
        results["delta_mae"], results["cross_venue_mae"] - results["local_mae"]
    )
