from __future__ import annotations

import numpy as np
import pandas as pd

from fairvaluelab.latency import align_delayed_returns, evaluate_latency


def latency_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10
    signal = np.sin(index / 4.0)
    target = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    return pd.DataFrame(
        {
            "sample_kind": "event",
            "sample_timestamp_ns": timestamps,
            "consolidated_mid": 100.0 + np.cumsum(target),
            "valid_venue_count": 2,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_spread_ticks": 2.0 + index % 2,
            "venue_1_imbalance_l1": signal,
            "venue_2_microprice_minus_consolidated_microprice": -signal,
            "venue_2_spread_ticks": 3.0 + index % 2,
            "pair_1_2_mid_difference": signal,
            "target_timestamp_10": timestamps + 10,
            "target_delay_ns_10": 0,
            "mid_return_10": target,
            "mid_direction_10": target.astype(int),
        }
    )


def test_delayed_target_uses_price_observable_after_decision() -> None:
    dataset = latency_dataset(10)
    aligned = align_delayed_returns(dataset, 10, 5, 10)
    assert aligned.loc[0, "decision_state_age_ns"] == 5
    assert aligned.loc[0, "target_timestamp_ns"] == dataset.loc[2, "sample_timestamp_ns"]
    assert aligned.loc[0, "return"] == (
        dataset.loc[2, "consolidated_mid"] - dataset.loc[0, "consolidated_mid"]
    )


def test_latency_study_reports_all_delays() -> None:
    results = evaluate_latency(latency_dataset(), 1, (0, 5), 10)
    assert len(results) == 2
    assert set(results["added_decision_delay_ns"]) == {0, 5}
    assert set(results["analysis_type"]) == {"offline_latency_sensitivity"}
    assert set(results["evaluation_status"]) == {"completed"}
    assert results["test_rows"].gt(0).all()
