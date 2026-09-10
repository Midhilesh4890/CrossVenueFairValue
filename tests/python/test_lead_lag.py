from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from fairvaluelab.lead_lag import analyze_lead_lag, sampling_interval_ns


def lead_lag_dataset() -> pd.DataFrame:
    rows = 30
    timestamps = np.arange(rows, dtype=np.uint64) * 5
    first_mid = np.cumsum(np.sin(np.arange(rows)))
    second_mid = np.concatenate(([0.0, 0.0], first_mid[:-2]))
    consolidated = (first_mid + second_mid) / 2.0
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "consolidated_mid": consolidated,
            "consolidated_microprice": consolidated,
            "venue_1_observed": 1,
            "venue_1_fresh": 1,
            "venue_1_mid_minus_consolidated_mid": first_mid - consolidated,
            "venue_1_microprice_minus_consolidated_microprice": first_mid - consolidated,
            "venue_1_ofi_time_window": np.sin(np.arange(rows)),
            "venue_1_multi_level_ofi_time_window": np.sin(np.arange(rows)),
            "venue_1_signed_trade_volume_time_window": np.sin(np.arange(rows)),
            "venue_2_observed": 1,
            "venue_2_fresh": 1,
            "venue_2_mid_minus_consolidated_mid": second_mid - consolidated,
            "venue_2_microprice_minus_consolidated_microprice": second_mid - consolidated,
            "venue_2_ofi_time_window": np.sin(np.arange(rows) - 2),
            "venue_2_multi_level_ofi_time_window": np.sin(np.arange(rows) - 2),
            "venue_2_signed_trade_volume_time_window": np.sin(np.arange(rows) - 2),
        }
    )


def test_lead_lag_orientation_finds_delayed_second_venue() -> None:
    results = analyze_lead_lag(lead_lag_dataset(), (10,))
    row = results.loc[
        (results["source_venue_id"] == 1)
        & (results["response_venue_id"] == 2)
        & (results["source_signal"] == "mid_price_change")
        & (results["freshness_filter"] == "fresh_only")
    ].iloc[0]
    assert row["pearson_correlation"] > 0.99
    assert row["lag_ns"] == 10


def test_lead_lag_requires_clock_compatible_offsets() -> None:
    dataset = lead_lag_dataset()
    assert sampling_interval_ns(dataset) == 5
    with pytest.raises(ValueError, match="exact multiple"):
        analyze_lead_lag(dataset, (7,))
