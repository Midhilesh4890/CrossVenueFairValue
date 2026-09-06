"""Simple statistical baselines for cross-venue fair-value datasets."""

from __future__ import annotations

import argparse
import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression, Ridge
from sklearn.metrics import (
    accuracy_score,
    mean_absolute_error,
    r2_score,
    recall_score,
    roc_auc_score,
)
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

from fairvaluelab.dataset import load_dataset, target_horizons, validate_dataset

_VENUE_COLUMN = re.compile(r"^venue_(\d+)_(.+)$")
_LOCAL_SUFFIXES = {
    "spread_ticks",
    "imbalance_l1",
    "imbalance_l3",
    "imbalance_l5",
    "bid_depth",
    "ask_depth",
    "ofi_event_window",
    "ofi_time_window",
    "multi_level_ofi_event_window",
    "multi_level_ofi_time_window",
    "signed_trade_volume_event_window",
    "signed_trade_volume_time_window",
    "last_mid_move",
}
_CROSS_VENUE_SUFFIXES = _LOCAL_SUFFIXES | {
    "age_ns",
    "mid_minus_consolidated_mid",
    "microprice_minus_consolidated_microprice",
}


@dataclass(frozen=True)
class ChronologicalSplit:
    """Chronological partitions and their leakage-safe time boundaries."""

    train: pd.DataFrame
    validation: pd.DataFrame
    test: pd.DataFrame
    validation_start_ns: int
    test_start_ns: int
    purged_train_rows: int
    purged_validation_rows: int


def _before_target_boundary(dataset: pd.DataFrame, boundary_ns: int) -> pd.Series:
    target_columns = [
        f"target_timestamp_{horizon}" for horizon in target_horizons(dataset)
    ]
    if not target_columns:
        raise ValueError("dataset has no future target timestamps")
    safe = pd.Series(True, index=dataset.index)
    for column in target_columns:
        target = dataset[column]
        safe &= target.isna() | (target < boundary_ns)
    return safe


def chronological_split(
    dataset: pd.DataFrame,
    *,
    train_fraction: float = 0.70,
    validation_fraction: float = 0.15,
) -> ChronologicalSplit:
    """Split by sample time and purge targets that cross a later partition boundary.

    Timestamp groups are never divided. Training targets must occur strictly before the
    validation boundary, and validation targets strictly before the test boundary.
    """
    validate_dataset(dataset)
    if not 0.0 < train_fraction < 1.0:
        raise ValueError("train_fraction must be between zero and one")
    if not 0.0 < validation_fraction < 1.0:
        raise ValueError("validation_fraction must be between zero and one")
    if train_fraction + validation_fraction >= 1.0:
        raise ValueError("train and validation fractions must leave a test partition")
    if len(dataset) < 3:
        raise ValueError("at least three rows are required for chronological splitting")

    train_index = max(1, min(len(dataset) - 2, int(len(dataset) * train_fraction)))
    test_index = max(
        train_index + 1,
        min(len(dataset) - 1, int(len(dataset) * (train_fraction + validation_fraction))),
    )
    sample_timestamps = dataset["sample_timestamp_ns"]
    validation_start_ns = int(sample_timestamps.iloc[train_index])
    test_start_ns = int(sample_timestamps.iloc[test_index])
    if validation_start_ns >= test_start_ns:
        raise ValueError("timestamp groups do not permit three chronological partitions")

    raw_train = dataset.loc[sample_timestamps < validation_start_ns]
    raw_validation = dataset.loc[
        (sample_timestamps >= validation_start_ns) & (sample_timestamps < test_start_ns)
    ]
    test = dataset.loc[sample_timestamps >= test_start_ns]
    train = raw_train.loc[_before_target_boundary(raw_train, validation_start_ns)]
    validation = raw_validation.loc[
        _before_target_boundary(raw_validation, test_start_ns)
    ]
    if train.empty or validation.empty or test.empty:
        raise ValueError("chronological purging produced an empty partition")

    return ChronologicalSplit(
        train=train.copy(),
        validation=validation.copy(),
        test=test.copy(),
        validation_start_ns=validation_start_ns,
        test_start_ns=test_start_ns,
        purged_train_rows=len(raw_train) - len(train),
        purged_validation_rows=len(raw_validation) - len(validation),
    )


def baseline_feature_groups(
    dataset: pd.DataFrame, primary_venue_id: int | None = None
) -> dict[str, list[str]]:
    """Build interpretable local and cross-venue feature groups."""
    venue_ids = sorted(
        {
            int(match.group(1))
            for column in dataset.columns
            if (match := _VENUE_COLUMN.fullmatch(column)) is not None
        }
    )
    if not venue_ids:
        raise ValueError("dataset has no per-venue feature columns")
    primary = venue_ids[0] if primary_venue_id is None else primary_venue_id
    if primary not in venue_ids:
        raise ValueError(f"venue {primary} is not present in the dataset")

    deviation = f"venue_{primary}_microprice_minus_consolidated_microprice"
    if deviation not in dataset.columns:
        raise ValueError(f"dataset is missing {deviation}")
    local = [
        column
        for column in dataset.columns
        if (match := _VENUE_COLUMN.fullmatch(column)) is not None
        and int(match.group(1)) == primary
        and match.group(2) in _LOCAL_SUFFIXES
    ]
    cross = [
        column
        for column in dataset.columns
        if column.startswith("pair_") and not column.endswith("_both_fresh")
    ]
    cross.extend(
        column
        for column in dataset.columns
        if (match := _VENUE_COLUMN.fullmatch(column)) is not None
        and match.group(2) in _CROSS_VENUE_SUFFIXES
    )
    return {
        "microprice_deviation": [deviation],
        "local_microstructure": local,
        "local_plus_cross_venue": list(dict.fromkeys([*local, *cross])),
    }


def _pipeline(model: Ridge | LogisticRegression) -> Pipeline:
    return Pipeline(
        [
            ("imputer", SimpleImputer(strategy="median", keep_empty_features=True)),
            ("scaler", StandardScaler()),
            ("model", model),
        ]
    )


def _usable_features(train: pd.DataFrame, columns: Sequence[str]) -> list[str]:
    return [
        column
        for column in columns
        if column in train.columns and train[column].notna().any()
    ]


def _information_coefficient(actual: np.ndarray, predicted: np.ndarray) -> float:
    if actual.size < 2 or np.std(actual) == 0.0 or np.std(predicted) == 0.0:
        return float("nan")
    return float(np.corrcoef(actual, predicted)[0, 1])


def evaluate_horizon(
    train: pd.DataFrame,
    test: pd.DataFrame,
    horizon_ns: int,
    *,
    primary_venue_id: int | None = None,
) -> pd.DataFrame:
    """Fit Ridge and logistic baselines for one horizon using fixed train/test frames."""
    regression_target = f"mid_return_{horizon_ns}"
    direction_target = f"mid_direction_{horizon_ns}"
    for column in (regression_target, direction_target):
        if column not in train.columns or column not in test.columns:
            raise ValueError(f"missing target column: {column}")

    records: list[dict[str, int | float | str]] = []
    for group_name, configured_columns in baseline_feature_groups(
        train, primary_venue_id
    ).items():
        columns = _usable_features(train, configured_columns)
        if not columns:
            raise ValueError(f"feature group {group_name} has no usable training columns")

        regression_train = train[regression_target].notna()
        regression_test = test[regression_target].notna()
        if not regression_train.any() or not regression_test.any():
            raise ValueError(f"horizon {horizon_ns} has no usable regression rows")
        ridge = _pipeline(Ridge(alpha=1.0))
        ridge.fit(
            train.loc[regression_train, columns],
            train.loc[regression_train, regression_target],
        )
        actual = test.loc[regression_test, regression_target].to_numpy(dtype=float)
        predicted = ridge.predict(test.loc[regression_test, columns])

        classification_train = train[direction_target].notna()
        classification_test = test[direction_target].notna()
        train_direction = train.loc[classification_train, direction_target].astype(int)
        test_direction = test.loc[classification_test, direction_target].astype(int)
        accuracy = float("nan")
        balanced_accuracy = float("nan")
        if train_direction.nunique() >= 2 and not test_direction.empty:
            logistic = _pipeline(LogisticRegression(max_iter=1_000, random_state=0))
            logistic.fit(train.loc[classification_train, columns], train_direction)
            direction_prediction = logistic.predict(test.loc[classification_test, columns])
            accuracy = float(accuracy_score(test_direction, direction_prediction))
            if test_direction.nunique() >= 2:
                balanced_accuracy = float(
                    recall_score(
                        test_direction,
                        direction_prediction,
                        labels=np.unique(test_direction),
                        average="macro",
                        zero_division=0,
                    )
                )

        roc_auc = float("nan")
        binary_train = classification_train & (train[direction_target] != 0)
        binary_test = classification_test & (test[direction_target] != 0)
        binary_train_target = (train.loc[binary_train, direction_target] > 0).astype(int)
        binary_test_target = (test.loc[binary_test, direction_target] > 0).astype(int)
        if binary_train_target.nunique() == 2 and binary_test_target.nunique() == 2:
            binary_logistic = _pipeline(LogisticRegression(max_iter=1_000, random_state=0))
            binary_logistic.fit(train.loc[binary_train, columns], binary_train_target)
            probabilities = binary_logistic.predict_proba(test.loc[binary_test, columns])[:, 1]
            roc_auc = float(roc_auc_score(binary_test_target, probabilities))

        records.append(
            {
                "horizon_ns": horizon_ns,
                "features": group_name,
                "feature_count": len(columns),
                "train_rows": int(regression_train.sum()),
                "test_rows": int(regression_test.sum()),
                "mae": float(mean_absolute_error(actual, predicted)),
                "r2": float(r2_score(actual, predicted)) if actual.size >= 2 else float("nan"),
                "ic": _information_coefficient(actual, predicted),
                "accuracy": accuracy,
                "balanced_accuracy": balanced_accuracy,
                "roc_auc_nonzero": roc_auc,
            }
        )
    return pd.DataFrame.from_records(records)


def evaluate_dataset(
    dataset: pd.DataFrame, *, primary_venue_id: int | None = None
) -> tuple[ChronologicalSplit, pd.DataFrame]:
    """Evaluate every horizon with fixed models and a purged chronological split."""
    split = chronological_split(dataset)
    development = pd.concat([split.train, split.validation], axis=0)
    results = [
        evaluate_horizon(
            development,
            split.test,
            horizon,
            primary_venue_id=primary_venue_id,
        )
        for horizon in target_horizons(dataset)
    ]
    return split, pd.concat(results, ignore_index=True)


def _format_horizon(horizon_ns: int) -> str:
    if horizon_ns % 1_000_000_000 == 0:
        return f"{horizon_ns // 1_000_000_000}s"
    if horizon_ns % 1_000_000 == 0:
        return f"{horizon_ns // 1_000_000}ms"
    return f"{horizon_ns}ns"


def main(argv: list[str] | None = None) -> int:
    """Run fixed statistical baselines on a generated research dataset."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int)
    args = parser.parse_args(argv)

    dataset = load_dataset(args.dataset)
    split, results = evaluate_dataset(
        dataset, primary_venue_id=args.primary_venue
    )
    display = results.copy()
    display["horizon"] = display["horizon_ns"].map(_format_horizon)
    columns = [
        "horizon",
        "features",
        "mae",
        "r2",
        "ic",
        "accuracy",
        "balanced_accuracy",
        "roc_auc_nonzero",
    ]
    print(
        "rows: "
        f"train={len(split.train)}, validation={len(split.validation)}, "
        f"test={len(split.test)}; purged: train={split.purged_train_rows}, "
        f"validation={split.purged_validation_rows}"
    )
    print(display[columns].to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
