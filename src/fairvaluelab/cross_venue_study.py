from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.linear_model import Ridge
from sklearn.metrics import mean_absolute_error

from fairvaluelab.baseline import (
    _information_coefficient,
    _pipeline,
    _usable_features,
    baseline_feature_groups,
    evaluate_dataset,
)
from fairvaluelab.dataset import load_dataset, target_horizons


def block_bootstrap_deltas(
    actual: np.ndarray,
    local_prediction: np.ndarray,
    cross_prediction: np.ndarray,
    block_rows: int,
    replicates: int,
    seed: int,
) -> tuple[np.ndarray, np.ndarray]:
    if block_rows <= 0 or replicates <= 0:
        raise ValueError("block rows and replicates must be positive")
    row_count = actual.size
    if row_count < 2:
        return np.array([], dtype=float), np.array([], dtype=float)
    starts = np.arange(0, row_count, block_rows)
    blocks = [np.arange(start, min(start + block_rows, row_count)) for start in starts]
    generator = np.random.default_rng(seed)
    mae_deltas = np.empty(replicates)
    ic_deltas = np.full(replicates, np.nan)
    for replicate in range(replicates):
        selections = generator.integers(0, len(blocks), size=len(blocks))
        indices = np.concatenate([blocks[index] for index in selections])[:row_count]
        sampled_actual = actual[indices]
        sampled_local = local_prediction[indices]
        sampled_cross = cross_prediction[indices]
        mae_deltas[replicate] = mean_absolute_error(
            sampled_actual, sampled_cross
        ) - mean_absolute_error(sampled_actual, sampled_local)
        local_ic = _information_coefficient(sampled_actual, sampled_local)
        cross_ic = _information_coefficient(sampled_actual, sampled_cross)
        if np.isfinite(local_ic) and np.isfinite(cross_ic):
            ic_deltas[replicate] = cross_ic - local_ic
    return mae_deltas, ic_deltas[np.isfinite(ic_deltas)]


def _interval(values: np.ndarray) -> tuple[float, float]:
    if values.size == 0:
        return float("nan"), float("nan")
    lower, upper = np.quantile(values, [0.025, 0.975])
    return float(lower), float(upper)


def compare_cross_venue(
    dataset: pd.DataFrame,
    primary_venue_id: int,
    block_rows: int,
    replicates: int,
    seed: int,
) -> pd.DataFrame:
    split, baseline = evaluate_dataset(dataset, primary_venue_id=primary_venue_id)
    development = pd.concat([split.train, split.validation], axis=0)
    groups = baseline_feature_groups(development, primary_venue_id)
    local_columns = _usable_features(development, groups["local_microstructure"])
    cross_columns = _usable_features(development, groups["local_plus_cross_venue"])
    records: list[dict[str, int | float | str | bool]] = []
    for horizon in target_horizons(dataset):
        target = f"mid_return_{horizon}"
        train_mask = development[target].notna()
        test_mask = split.test[target].notna()
        actual = split.test.loc[test_mask, target].to_numpy(dtype=float)
        predictions = []
        for columns in (local_columns, cross_columns):
            model = _pipeline(Ridge(alpha=1.0))
            model.fit(development.loc[train_mask, columns], development.loc[train_mask, target])
            predictions.append(model.predict(split.test.loc[test_mask, columns]))
        local_prediction, cross_prediction = predictions
        local_ic = _information_coefficient(actual, local_prediction)
        cross_ic = _information_coefficient(actual, cross_prediction)
        mae_deltas, ic_deltas = block_bootstrap_deltas(
            actual,
            local_prediction,
            cross_prediction,
            block_rows,
            replicates,
            seed + horizon,
        )
        mae_lower, mae_upper = _interval(mae_deltas)
        ic_lower, ic_upper = _interval(ic_deltas)
        local_metrics = baseline.loc[
            (baseline["horizon_ns"] == horizon)
            & (baseline["features"] == "local_microstructure")
        ].iloc[0]
        cross_metrics = baseline.loc[
            (baseline["horizon_ns"] == horizon)
            & (baseline["features"] == "local_plus_cross_venue")
        ].iloc[0]
        delta_mae = float(mean_absolute_error(actual, cross_prediction)) - float(
            mean_absolute_error(actual, local_prediction)
        )
        delta_ic = cross_ic - local_ic
        target_standard_deviation = float(np.std(actual))
        supported = target_standard_deviation > 0.0 and (
            (mae_upper < 0.0) or (np.isfinite(ic_lower) and ic_lower > 0.0)
        )
        conclusion = (
            "insufficient_target_variation"
            if target_standard_deviation == 0.0
            else "improvement_supported"
            if supported
            else "improvement_not_supported"
        )
        records.append(
            {
                "horizon_ns": horizon,
                "test_rows": actual.size,
                "target_standard_deviation": target_standard_deviation,
                "local_feature_count": len(local_columns),
                "cross_venue_feature_count": len(cross_columns),
                "local_mae": float(mean_absolute_error(actual, local_prediction)),
                "cross_venue_mae": float(mean_absolute_error(actual, cross_prediction)),
                "delta_mae": delta_mae,
                "delta_mae_ci_lower": mae_lower,
                "delta_mae_ci_upper": mae_upper,
                "local_ic": local_ic,
                "cross_venue_ic": cross_ic,
                "delta_ic": delta_ic,
                "delta_ic_ci_lower": ic_lower,
                "delta_ic_ci_upper": ic_upper,
                "valid_ic_bootstrap_replicates": ic_deltas.size,
                "local_accuracy": float(local_metrics["accuracy"]),
                "cross_venue_accuracy": float(cross_metrics["accuracy"]),
                "local_balanced_accuracy": float(local_metrics["balanced_accuracy"]),
                "cross_venue_balanced_accuracy": float(cross_metrics["balanced_accuracy"]),
                "local_roc_auc_nonzero": float(local_metrics["roc_auc_nonzero"]),
                "cross_venue_roc_auc_nonzero": float(cross_metrics["roc_auc_nonzero"]),
                "bootstrap_block_rows": block_rows,
                "bootstrap_replicates": replicates,
                "cross_venue_improvement_supported": supported,
                "conclusion": conclusion,
            }
        )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare local and cross-venue predictive value")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    parser.add_argument("--block-rows", type=int, default=10)
    parser.add_argument("--bootstrap-replicates", type=int, default=2_000)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args(argv)
    results = compare_cross_venue(
        load_dataset(args.dataset),
        args.primary_venue,
        args.block_rows,
        args.bootstrap_replicates,
        args.seed,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
