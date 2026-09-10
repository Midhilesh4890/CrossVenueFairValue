from __future__ import annotations

import numpy as np
import pandas as pd

from fairvaluelab.reference_study import evaluate_references, reference_series


def study_dataset() -> pd.DataFrame:
    timestamps = np.arange(20, dtype=np.uint64) * 10
    midpoint = 100.0 + np.arange(20)
    microprice = midpoint + np.where(np.arange(20) % 2 == 0, 0.5, -0.5)
    future_return = np.where(np.arange(20) % 2 == 0, 0.5, -0.5)
    return pd.DataFrame(
        {
            "sample_timestamp_ns": timestamps,
            "consolidated_mid": midpoint,
            "consolidated_microprice": microprice,
            "venue_1_mid_minus_consolidated_mid": 0.25,
            "venue_1_microprice_minus_consolidated_microprice": 0.1,
            "target_timestamp_10": timestamps + 10,
            "target_delay_ns_10": 0,
            "mid_return_10": future_return,
            "microprice_return_10": future_return,
        }
    )


def test_reference_series_reconstructs_venue_prices() -> None:
    dataset = study_dataset()
    references = reference_series(dataset)
    np.testing.assert_allclose(
        references["venue_1_midpoint"], dataset["consolidated_mid"] + 0.25
    )
    np.testing.assert_allclose(
        references["venue_1_microprice"], dataset["consolidated_microprice"] + 0.1
    )


def test_reference_study_reports_each_target_and_predictor() -> None:
    results = evaluate_references(study_dataset())
    assert len(results) == 8
    assert set(results["target_reference"]) == {
        "consolidated_midpoint",
        "consolidated_microprice",
    }
    midpoint = results.loc[
        (results["target_reference"] == "consolidated_midpoint")
        & (results["predictor_reference"] == "consolidated_midpoint")
    ].iloc[0]
    assert midpoint["mae_ticks"] == 0.5
    assert midpoint["direction_accuracy"] == 0.0
