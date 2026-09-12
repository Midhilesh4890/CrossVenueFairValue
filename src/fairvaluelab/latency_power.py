from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from statistics import median

import pandas as pd
from sklearn.linear_model import Ridge

from fairvaluelab.baseline import (
    _pipeline,
    _usable_features,
    baseline_feature_groups,
    chronological_split,
)
from fairvaluelab.dataset import load_dataset, target_horizons


def feature_path_latency(benchmark_report: dict[str, object]) -> float:
    results = benchmark_report.get("results")
    if not isinstance(results, list):
        raise ValueError("benchmark report has no results")
    for result in results:
        if isinstance(result, dict) and result.get("benchmark") == "feature_and_cross_venue":
            return float(result["mean_ns_per_event"])
    raise ValueError("benchmark report lacks feature_and_cross_venue result")


def measure_prediction_latency(
    model: object, sample: pd.DataFrame, repetitions: int, trials: int
) -> float:
    if repetitions <= 0 or trials <= 0:
        raise ValueError("repetitions and trials must be positive")
    for _ in range(20):
        model.predict(sample)
    averages = []
    for _ in range(trials):
        start = time.perf_counter_ns()
        for _ in range(repetitions):
            model.predict(sample)
        averages.append((time.perf_counter_ns() - start) / repetitions)
    return float(median(averages))


def evaluate_latency_power(
    dataset: pd.DataFrame,
    baseline_results: pd.DataFrame,
    benchmark_report: dict[str, object],
    primary_venue_id: int,
    repetitions: int,
    trials: int,
) -> pd.DataFrame:
    split = chronological_split(dataset)
    development = pd.concat([split.train, split.validation], axis=0)
    groups = baseline_feature_groups(dataset, primary_venue_id)
    feature_latency_ns = feature_path_latency(benchmark_report)
    records: list[dict[str, int | float | str | bool]] = []
    for horizon in target_horizons(dataset):
        target = f"mid_return_{horizon}"
        train_mask = development[target].notna()
        test_mask = split.test[target].notna()
        for group_name, configured_columns in groups.items():
            columns = _usable_features(development, configured_columns)
            model = _pipeline(Ridge(alpha=1.0))
            model.fit(development.loc[train_mask, columns], development.loc[train_mask, target])
            sample = split.test.loc[test_mask, columns].iloc[[0]]
            inference_latency_ns = measure_prediction_latency(
                model, sample, repetitions, trials
            )
            metrics = baseline_results.loc[
                (baseline_results["horizon_ns"] == horizon)
                & (baseline_results["features"] == group_name)
            ]
            if len(metrics) != 1:
                raise ValueError(f"missing baseline result for {horizon} {group_name}")
            metric = metrics.iloc[0]
            quality_defined = pd.notna(metric["ic"])
            records.append(
                {
                    "model": "ridge_alpha_1",
                    "feature_set": group_name,
                    "horizon_ns": horizon,
                    "feature_count": len(columns),
                    "mae": float(metric["mae"]),
                    "ic": float(metric["ic"]),
                    "feature_path_latency_ns": feature_latency_ns,
                    "inference_latency_ns": inference_latency_ns,
                    "effective_processing_latency_ns": (
                        feature_latency_ns + inference_latency_ns
                    ),
                    "inference_repetitions_per_trial": repetitions,
                    "inference_trials": trials,
                    "inference_runtime": "python_sklearn_pipeline",
                    "inference_methodology": "median_of_single_row_timed_run_averages",
                    "feature_latency_scope": "full_feature_and_cross_venue_cpp_path",
                    "machine_specific": True,
                    "complexity_value_assessable": bool(quality_defined),
                    "conclusion": (
                        "comparison_available"
                        if quality_defined
                        else "insufficient_target_variation"
                    ),
                }
            )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Combine predictive quality and measured latency")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--baseline-results", required=True, type=Path)
    parser.add_argument("--benchmark-results", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    parser.add_argument("--repetitions", type=int, default=500)
    parser.add_argument("--trials", type=int, default=5)
    args = parser.parse_args(argv)
    with args.benchmark_results.open(encoding="utf-8") as input_file:
        benchmark_report = json.load(input_file)
    results = evaluate_latency_power(
        load_dataset(args.dataset),
        pd.read_csv(args.baseline_results),
        benchmark_report,
        args.primary_venue,
        args.repetitions,
        args.trials,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(results.to_string(index=False, float_format=lambda value: f"{value:.3f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
