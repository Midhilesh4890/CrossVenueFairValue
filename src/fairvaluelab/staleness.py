from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd

from fairvaluelab.baseline import chronological_split, evaluate_horizon
from fairvaluelab.dataset import load_dataset, target_horizons


def parse_dataset_spec(value: str) -> tuple[int, Path]:
    threshold, separator, path = value.partition("=")
    if not separator or not path:
        raise argparse.ArgumentTypeError("dataset must use STALENESS_NS=PATH")
    try:
        staleness_ns = int(threshold)
    except ValueError as error:
        raise argparse.ArgumentTypeError("staleness threshold must be an integer") from error
    if staleness_ns <= 0:
        raise argparse.ArgumentTypeError("staleness threshold must be positive")
    return staleness_ns, Path(path)


def evaluate_staleness(
    variants: list[tuple[int, pd.DataFrame]], primary_venue_id: int
) -> pd.DataFrame:
    records: list[dict[str, int | float | str]] = []
    for staleness_ns, dataset in sorted(variants, key=lambda item: item[0]):
        valid_counts = pd.to_numeric(dataset["valid_venue_count"], errors="raise")
        maximum_venues = int(valid_counts.max())
        split = chronological_split(dataset)
        development = pd.concat([split.train, split.validation], axis=0)
        for horizon in target_horizons(dataset):
            target_rows = int(dataset[f"target_timestamp_{horizon}"].notna().sum())
            record: dict[str, int | float | str] = {
                "staleness_ns": staleness_ns,
                "horizon_ns": horizon,
                "dataset_rows": len(dataset),
                "target_rows": target_rows,
                "target_coverage": target_rows / len(dataset),
                "mean_valid_venue_count": float(valid_counts.mean()),
                "all_venues_valid_rows": int((valid_counts == maximum_venues).sum()),
                "all_venues_valid_fraction": float((valid_counts == maximum_venues).mean()),
                "maximum_venue_count": maximum_venues,
                "purged_train_rows": split.purged_train_rows,
                "purged_validation_rows": split.purged_validation_rows,
            }
            try:
                metrics = evaluate_horizon(
                    development,
                    split.test,
                    horizon,
                    primary_venue_id=primary_venue_id,
                )
                cross = metrics.loc[metrics["features"] == "local_plus_cross_venue"].iloc[0]
                test_target = split.test[f"mid_return_{horizon}"].dropna()
                test_rows = int(cross["test_rows"])
                target_unique_values = int(test_target.nunique())
                status = (
                    "insufficient_test_rows"
                    if test_rows < 10
                    else "insufficient_target_variation"
                    if target_unique_values < 2
                    else "completed"
                )
                record.update(
                    {
                        "feature_count": int(cross["feature_count"]),
                        "test_rows": test_rows,
                        "test_target_unique_values": target_unique_values,
                        "mae": float(cross["mae"]),
                        "ic": float(cross["ic"]),
                        "direction_accuracy": float(cross["accuracy"]),
                        "balanced_accuracy": float(cross["balanced_accuracy"]),
                        "roc_auc_nonzero": float(cross["roc_auc_nonzero"]),
                        "evaluation_status": status,
                    }
                )
            except ValueError:
                record.update(
                    {
                        "feature_count": 0,
                        "test_rows": 0,
                        "test_target_unique_values": 0,
                        "mae": float("nan"),
                        "ic": float("nan"),
                        "direction_accuracy": float("nan"),
                        "balanced_accuracy": float("nan"),
                        "roc_auc_nonzero": float("nan"),
                        "evaluation_status": "insufficient_usable_rows",
                    }
                )
            records.append(record)
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Measure sensitivity to venue staleness")
    parser.add_argument("--dataset", action="append", required=True, type=parse_dataset_spec)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--primary-venue", type=int, default=1)
    args = parser.parse_args(argv)
    thresholds = [threshold for threshold, _ in args.dataset]
    if len(thresholds) != len(set(thresholds)):
        parser.error("staleness thresholds must be unique")
    variants = [(threshold, load_dataset(path)) for threshold, path in args.dataset]
    results = evaluate_staleness(variants, args.primary_venue)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
