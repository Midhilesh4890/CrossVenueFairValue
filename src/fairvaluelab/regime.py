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
    chronological_split,
)
from fairvaluelab.dataset import load_dataset, target_horizons


def regime_features(dataset: pd.DataFrame, primary_venue_id: int) -> pd.DataFrame:
    prefix = f"venue_{primary_venue_id}_"
    required = [
        f"{prefix}spread_ticks",
        f"{prefix}bid_depth",
        f"{prefix}ask_depth",
        f"{prefix}imbalance_l1",
        f"{prefix}age_ns",
        f"{prefix}signed_trade_volume_time_window",
        "consolidated_mid",
    ]
    missing = [column for column in required if column not in dataset.columns]
    if missing:
        raise ValueError(f"dataset is missing regime columns: {', '.join(missing)}")
    features = pd.DataFrame(index=dataset.index)
    features["spread"] = dataset[f"{prefix}spread_ticks"]
    features["volatility"] = dataset["consolidated_mid"].diff().rolling(20, min_periods=2).std()
    features["depth"] = dataset[f"{prefix}bid_depth"] + dataset[f"{prefix}ask_depth"]
    features["trade_activity"] = dataset[
        f"{prefix}signed_trade_volume_time_window"
    ].abs()
    features["imbalance"] = dataset[f"{prefix}imbalance_l1"].abs()
    features["venue_age"] = dataset[f"{prefix}age_ns"]
    return features


def analyze_regimes(dataset: pd.DataFrame, primary_venue_id: int) -> pd.DataFrame:
    split = chronological_split(dataset)
    development = pd.concat([split.train, split.validation], axis=0)
    groups = baseline_feature_groups(development, primary_venue_id)
    local_columns = _usable_features(development, groups["local_microstructure"])
    cross_columns = _usable_features(development, groups["local_plus_cross_venue"])
    features = regime_features(dataset, primary_venue_id)
    thresholds = features.loc[split.train.index].median(skipna=True)
    records: list[dict[str, int | float | str | bool]] = []
    for horizon in target_horizons(dataset):
        target = f"mid_return_{horizon}"
        train_mask = development[target].notna()
        test_target = split.test[target].notna()
        predictions: dict[str, pd.Series] = {}
        for name, columns in (
            ("local", local_columns),
            ("local_plus_cross_venue", cross_columns),
        ):
            model = _pipeline(Ridge(alpha=1.0))
            model.fit(development.loc[train_mask, columns], development.loc[train_mask, target])
            predicted = model.predict(split.test.loc[test_target, columns])
            predictions[name] = pd.Series(predicted, index=split.test.index[test_target])
        for regime_name in features.columns:
            threshold = float(thresholds[regime_name])
            if not np.isfinite(threshold):
                continue
            test_values = features.loc[split.test.index, regime_name]
            for band, condition in (
                ("low", test_values <= threshold),
                ("high", test_values > threshold),
            ):
                indices = split.test.index[test_target & condition & test_values.notna()]
                if len(indices) == 0:
                    continue
                actual = split.test.loc[indices, target].to_numpy(dtype=float)
                local_prediction = predictions["local"].loc[indices].to_numpy(dtype=float)
                cross_prediction = predictions["local_plus_cross_venue"].loc[indices].to_numpy(
                    dtype=float
                )
                local_mae = float(mean_absolute_error(actual, local_prediction))
                cross_mae = float(mean_absolute_error(actual, cross_prediction))
                local_ic = _information_coefficient(actual, local_prediction)
                cross_ic = _information_coefficient(actual, cross_prediction)
                target_variation = float(np.std(actual))
                records.append(
                    {
                        "horizon_ns": horizon,
                        "regime": regime_name,
                        "band": band,
                        "threshold": threshold,
                        "threshold_source": "training_partition_median",
                        "test_rows": len(indices),
                        "target_standard_deviation": target_variation,
                        "local_mae": local_mae,
                        "cross_venue_mae": cross_mae,
                        "delta_mae": cross_mae - local_mae,
                        "local_ic": local_ic,
                        "cross_venue_ic": cross_ic,
                        "delta_ic": cross_ic - local_ic,
                        "cross_venue_mae_improved": cross_mae < local_mae,
                        "conclusion": (
                            "insufficient_target_variation"
                            if target_variation == 0.0
                            else "cross_venue_mae_improved"
                            if cross_mae < local_mae
                            else "cross_venue_mae_not_improved"
                        ),
                    }
                )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Evaluate cross-venue models by market regime")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    args = parser.parse_args(argv)
    results = analyze_regimes(load_dataset(args.dataset), args.primary_venue)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
