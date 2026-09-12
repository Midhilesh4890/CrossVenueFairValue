from __future__ import annotations

import pandas as pd

from fairvaluelab.latency_power import feature_path_latency, measure_prediction_latency


class ConstantModel:
    def predict(self, sample: pd.DataFrame) -> list[float]:
        return [float(len(sample.columns))]


def test_feature_path_latency_reads_measured_benchmark() -> None:
    report = {
        "results": [
            {"benchmark": "order_book_update", "mean_ns_per_event": 10.0},
            {"benchmark": "feature_and_cross_venue", "mean_ns_per_event": 20.0},
        ]
    }
    assert feature_path_latency(report) == 20.0


def test_prediction_latency_is_measured_for_single_row() -> None:
    sample = pd.DataFrame({"value": [1.0]})
    latency = measure_prediction_latency(ConstantModel(), sample, 10, 2)
    assert latency > 0.0
