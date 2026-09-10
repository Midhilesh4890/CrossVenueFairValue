from __future__ import annotations

import argparse
import re
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
    chronological_split,
)
from fairvaluelab.dataset import load_dataset, target_horizons

_VENUE_COLUMN = re.compile(r"^venue_(\d+)_(.+)$")


def ablation_feature_groups(dataset: pd.DataFrame, primary_venue_id: int) -> dict[str, list[str]]:
    prefix = f"venue_{primary_venue_id}_"
    additions = [
        ("top_of_book", [f"{prefix}spread_ticks"]),
        ("plus_depth", [f"{prefix}bid_depth", f"{prefix}ask_depth"]),
        (
            "plus_imbalance",
            [f"{prefix}imbalance_l1", f"{prefix}imbalance_l3", f"{prefix}imbalance_l5"],
        ),
        ("plus_ofi", [f"{prefix}ofi_event_window", f"{prefix}ofi_time_window"]),
        (
            "plus_multi_level_ofi",
            [f"{prefix}multi_level_ofi_event_window", f"{prefix}multi_level_ofi_time_window"],
        ),
        (
            "plus_trade_flow",
            [
                f"{prefix}signed_trade_volume_event_window",
                f"{prefix}signed_trade_volume_time_window",
            ],
        ),
    ]
    cross_basis = [
        column
        for column in dataset.columns
        if (match := _VENUE_COLUMN.fullmatch(column)) is not None
        and match.group(2)
        in {"age_ns", "mid_minus_consolidated_mid", "microprice_minus_consolidated_microprice"}
    ]
    pairwise = [
        column
        for column in dataset.columns
        if column.startswith("pair_")
        and not column.endswith(
            (
                "_both_fresh",
                "_receipt_timestamp_difference_ns",
                "_exchange_timestamp_difference_ns",
                "_last_mid_move_difference",
            )
        )
    ]
    lead_lag = [
        column
        for column in dataset.columns
        if column.startswith("pair_")
        and column.endswith(
            (
                "_receipt_timestamp_difference_ns",
                "_exchange_timestamp_difference_ns",
                "_last_mid_move_difference",
            )
        )
    ]
    lead_lag.extend(
        column
        for column in dataset.columns
        if column.startswith("venue_") and column.endswith("_last_mid_move")
    )
    additions.extend(
        [
            ("plus_cross_venue_basis", cross_basis),
            ("plus_pairwise_features", pairwise),
            ("plus_lead_lag_features", lead_lag),
        ]
    )
    groups: dict[str, list[str]] = {}
    cumulative: list[str] = []
    for name, columns in additions:
        cumulative = list(dict.fromkeys([*cumulative, *columns]))
        groups[name] = cumulative
    return groups


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


def evaluate_ablation(dataset: pd.DataFrame, primary_venue_id: int) -> pd.DataFrame:
    split = chronological_split(dataset)
    development = pd.concat([split.train, split.validation], axis=0)
    groups = ablation_feature_groups(dataset, primary_venue_id)
    records: list[dict[str, int | float | str]] = []
    for horizon in target_horizons(dataset):
        target = f"mid_return_{horizon}"
        train_mask = development[target].notna()
        test_mask = split.test[target].notna()
        actual = split.test.loc[test_mask, target].to_numpy(dtype=float)
        previous_mae = float("nan")
        previous_ic = float("nan")
        for group_name, configured_columns in groups.items():
            columns = _usable_features(development, configured_columns)
            if not columns:
                raise ValueError(f"feature group {group_name} has no usable training columns")
            model = _pipeline(Ridge(alpha=1.0))
            model.fit(development.loc[train_mask, columns], development.loc[train_mask, target])
            predicted = model.predict(split.test.loc[test_mask, columns])
            mae = float(mean_absolute_error(actual, predicted))
            ic = _information_coefficient(actual, predicted)
            accuracy, balanced_accuracy, roc_auc = _direction_metrics(actual, predicted)
            records.append(
                {
                    "horizon_ns": horizon,
                    "feature_set": group_name,
                    "feature_count": len(columns),
                    "development_rows": int(train_mask.sum()),
                    "test_rows": int(test_mask.sum()),
                    "mae": mae,
                    "marginal_mae": mae - previous_mae,
                    "r2": float(r2_score(actual, predicted)),
                    "ic": ic,
                    "marginal_ic": ic - previous_ic,
                    "direction_accuracy": accuracy,
                    "balanced_accuracy": balanced_accuracy,
                    "roc_auc_nonzero": roc_auc,
                    "model": "ridge_alpha_1",
                    "purged_train_rows": split.purged_train_rows,
                    "purged_validation_rows": split.purged_validation_rows,
                }
            )
            previous_mae = mae
            previous_ic = ic
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run cumulative fixed-model feature ablations")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    args = parser.parse_args(argv)
    results = evaluate_ablation(load_dataset(args.dataset), args.primary_venue)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(f"rows: {len(results)}")
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
