from __future__ import annotations

import numpy as np
import pandas as pd

from fairvaluelab.ablation import ablation_feature_groups, evaluate_ablation


def ablation_dataset(rows: int = 100) -> pd.DataFrame:
    index = np.arange(rows)
    timestamps = 1_000_000_000 + index * 10_000_000
    signal = np.sin(index / 4.0)
    target = np.where(signal > 0.25, 1.0, np.where(signal < -0.25, -1.0, 0.0))
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "venue_1_latest_local_receipt_timestamp_ns": timestamps,
            "venue_1_spread_ticks": 2.0 + index % 2,
            "venue_1_bid_depth": 10.0 + index,
            "venue_1_ask_depth": 20.0 + index,
            "venue_1_imbalance_l1": signal,
            "venue_1_imbalance_l3": signal,
            "venue_1_imbalance_l5": signal,
            "venue_1_ofi_event_window": signal,
            "venue_1_ofi_time_window": signal,
            "venue_1_multi_level_ofi_event_window": signal,
            "venue_1_multi_level_ofi_time_window": signal,
            "venue_1_signed_trade_volume_event_window": signal,
            "venue_1_signed_trade_volume_time_window": signal,
            "venue_1_mid_minus_consolidated_mid": signal,
            "venue_1_microprice_minus_consolidated_microprice": signal,
            "venue_1_age_ns": index,
            "venue_1_last_mid_move": np.sign(signal),
            "venue_2_mid_minus_consolidated_mid": -signal,
            "venue_2_microprice_minus_consolidated_microprice": -signal,
            "venue_2_age_ns": index,
            "venue_2_last_mid_move": -np.sign(signal),
            "pair_1_2_mid_difference": signal,
            "pair_1_2_receipt_timestamp_difference_ns": index,
            "pair_1_2_last_mid_move_difference": np.sign(signal),
            "target_timestamp_10000000": timestamps + 10_000_000,
            "target_delay_ns_10000000": 0,
            "mid_return_10000000": target,
        }
    )


def test_ablation_groups_are_cumulative() -> None:
    groups = ablation_feature_groups(ablation_dataset(), 1)
    sizes = [len(columns) for columns in groups.values()]
    assert list(groups) == [
        "top_of_book",
        "plus_depth",
        "plus_imbalance",
        "plus_ofi",
        "plus_multi_level_ofi",
        "plus_trade_flow",
        "plus_cross_venue_basis",
        "plus_pairwise_features",
        "plus_lead_lag_features",
    ]
    assert sizes == sorted(sizes)


def test_ablation_uses_one_fixed_model_for_all_groups() -> None:
    results = evaluate_ablation(ablation_dataset(), 1)
    assert len(results) == 9
    assert set(results["model"]) == {"ridge_alpha_1"}
    assert results["feature_count"].is_monotonic_increasing
    assert results["mae"].notna().all()
