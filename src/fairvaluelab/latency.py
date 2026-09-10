from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.linear_model import Ridge
from sklearn.metrics import (
    accuracy_score,
    mean_absolute_error,
    r2_score,
    recall_score,
    roc_auc_score,
)

from fairvaluelab.baseline import (
    _information_coefficient,
    _pipeline,
    _usable_features,
    baseline_feature_groups,
    chronological_split,
)
from fairvaluelab.dataset import load_dataset, target_horizons

DEFAULT_DELAYS_NS = (
    0,
    5_000,
    10_000,
    25_000,
    50_000,
    100_000,
    250_000,
    500_000,
    1_000_000,
    5_000_000,
)


def parse_nonnegative_integers(value: str) -> tuple[int, ...]:
    values = tuple(int(item) for item in value.split(","))
    if not values or any(item < 0 for item in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("values must be unique nonnegative integers")
    return values


def align_delayed_returns(
    dataset: pd.DataFrame,
    horizon_ns: int,
    delay_ns: int,
    max_observation_delay_ns: int,
) -> pd.DataFrame:
    timestamps = dataset["sample_timestamp_ns"].to_numpy(dtype=np.int64)
    mids = pd.to_numeric(dataset["consolidated_mid"], errors="coerce").to_numpy(dtype=float)
    valid_positions = np.flatnonzero(np.isfinite(mids))
    valid_timestamps = timestamps[valid_positions]
    actual = np.full(len(dataset), np.nan)
    target_timestamps = np.zeros(len(dataset), dtype=np.int64)
    decision_state_ages = np.full(len(dataset), np.nan)
    target_delays = np.full(len(dataset), np.nan)
    for row, timestamp in enumerate(timestamps):
        decision_threshold = timestamp + delay_ns
        decision_position = (
            int(np.searchsorted(valid_timestamps, decision_threshold, side="right")) - 1
        )
        if decision_position < 0:
            continue
        anchor_row = valid_positions[decision_position]
        decision_state_age = decision_threshold - timestamps[anchor_row]
        if decision_state_age > max_observation_delay_ns:
            continue
        target_threshold = decision_threshold + horizon_ns
        target_position = int(np.searchsorted(valid_timestamps, target_threshold))
        if target_position == len(valid_positions):
            continue
        target_row = valid_positions[target_position]
        target_observation_delay = timestamps[target_row] - target_threshold
        if target_observation_delay > max_observation_delay_ns:
            continue
        actual[row] = mids[target_row] - mids[anchor_row]
        target_timestamps[row] = timestamps[target_row]
        decision_state_ages[row] = decision_state_age
        target_delays[row] = target_observation_delay
    return pd.DataFrame(
        {
            "return": actual,
            "target_timestamp_ns": target_timestamps,
            "decision_state_age_ns": decision_state_ages,
            "target_observation_delay_ns": target_delays,
        },
        index=dataset.index,
    )


def _direction_metrics(actual: np.ndarray, predicted: np.ndarray) -> tuple[float, float, float]:
    actual_direction = np.sign(actual)
    predicted_direction = np.sign(predicted)
    accuracy = float(accuracy_score(actual_direction, predicted_direction))
    balanced = float("nan")
    if np.unique(actual_direction).size >= 2:
        balanced = float(
            recall_score(
                actual_direction,
                predicted_direction,
                labels=np.unique(actual_direction),
                average="macro",
                zero_division=0,
            )
        )
    nonzero = actual_direction != 0
    auc = float("nan")
    if np.unique(actual_direction[nonzero]).size == 2:
        auc = float(roc_auc_score(actual_direction[nonzero] > 0, predicted[nonzero]))
    return accuracy, balanced, auc


def evaluate_latency(
    dataset: pd.DataFrame,
    primary_venue_id: int,
    delays_ns: tuple[int, ...],
    max_observation_delay_ns: int,
) -> pd.DataFrame:
    if set(dataset["sample_kind"]) != {"event"}:
        raise ValueError("offline latency sensitivity requires an event-sampled dataset")
    split = chronological_split(dataset)
    development_indices = split.train.index.union(split.validation.index)
    features = baseline_feature_groups(dataset, primary_venue_id)["local_plus_cross_venue"]
    columns = _usable_features(dataset.loc[development_indices], features)
    records: list[dict[str, int | float | str]] = []
    for horizon in target_horizons(dataset):
        for delay_ns in delays_ns:
            aligned = align_delayed_returns(
                dataset, horizon, delay_ns, max_observation_delay_ns
            )
            train_mask = aligned.index.isin(development_indices) & aligned["return"].notna()
            train_mask &= aligned["target_timestamp_ns"] < split.test_start_ns
            test_mask = aligned.index.isin(split.test.index) & aligned["return"].notna()
            if train_mask.sum() == 0 or test_mask.sum() == 0:
                raise ValueError(f"no aligned rows for horizon {horizon} and delay {delay_ns}")
            model = _pipeline(Ridge(alpha=1.0))
            model.fit(dataset.loc[train_mask, columns], aligned.loc[train_mask, "return"])
            actual = aligned.loc[test_mask, "return"].to_numpy(dtype=float)
            predicted = model.predict(dataset.loc[test_mask, columns])
            accuracy, balanced_accuracy, roc_auc = _direction_metrics(actual, predicted)
            target_standard_deviation = float(np.std(actual))
            records.append(
                {
                    "horizon_ns": horizon,
                    "added_decision_delay_ns": delay_ns,
                    "feature_count": len(columns),
                    "train_rows": int(train_mask.sum()),
                    "test_rows": int(test_mask.sum()),
                    "mae": float(mean_absolute_error(actual, predicted)),
                    "r2": float(r2_score(actual, predicted)),
                    "ic": _information_coefficient(actual, predicted),
                    "direction_accuracy": accuracy,
                    "balanced_accuracy": balanced_accuracy,
                    "roc_auc_nonzero": roc_auc,
                    "target_standard_deviation": target_standard_deviation,
                    "mean_decision_state_age_ns": float(
                        aligned.loc[test_mask, "decision_state_age_ns"].mean()
                    ),
                    "p95_decision_state_age_ns": float(
                        aligned.loc[test_mask, "decision_state_age_ns"].quantile(0.95)
                    ),
                    "max_observation_delay_ns": max_observation_delay_ns,
                    "analysis_type": "offline_latency_sensitivity",
                    "evaluation_status": (
                        "insufficient_target_variation"
                        if target_standard_deviation == 0.0
                        else "completed"
                    ),
                }
            )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Measure offline predictive latency sensitivity")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    parser.add_argument("--delays-ns", type=parse_nonnegative_integers, default=DEFAULT_DELAYS_NS)
    parser.add_argument("--max-observation-delay-ns", type=int, default=100_000_000)
    args = parser.parse_args(argv)
    if args.max_observation_delay_ns < 0:
        parser.error("max-observation-delay-ns must be nonnegative")
    results = evaluate_latency(
        load_dataset(args.dataset),
        args.primary_venue,
        args.delays_ns,
        args.max_observation_delay_ns,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(f"rows: {len(results)}")
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
