from __future__ import annotations

import argparse
import re
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.metrics import accuracy_score, recall_score, roc_auc_score

from fairvaluelab.dataset import load_dataset, target_horizons

_VENUE_COLUMN = re.compile(r"^venue_(\d+)_")


def _information_coefficient(actual: np.ndarray, predicted: np.ndarray) -> float:
    if actual.size < 2 or np.std(actual) == 0.0 or np.std(predicted) == 0.0:
        return float("nan")
    return float(np.corrcoef(actual, predicted)[0, 1])


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
    nonzero_actual = actual_direction[nonzero]
    auc = float("nan")
    if np.unique(nonzero_actual).size == 2:
        auc = float(roc_auc_score(nonzero_actual > 0, predicted[nonzero]))
    return accuracy, balanced, auc


def reference_series(dataset: pd.DataFrame) -> dict[str, pd.Series]:
    references = {
        "consolidated_midpoint": dataset["consolidated_mid"],
        "consolidated_microprice": dataset["consolidated_microprice"],
    }
    venue_ids = sorted(
        {
            int(match.group(1))
            for column in dataset.columns
            if (match := _VENUE_COLUMN.match(column)) is not None
        }
    )
    for venue_id in venue_ids:
        references[f"venue_{venue_id}_midpoint"] = (
            dataset["consolidated_mid"]
            + dataset[f"venue_{venue_id}_mid_minus_consolidated_mid"]
        )
        references[f"venue_{venue_id}_microprice"] = (
            dataset["consolidated_microprice"]
            + dataset[f"venue_{venue_id}_microprice_minus_consolidated_microprice"]
        )
    return references


def evaluate_references(dataset: pd.DataFrame) -> pd.DataFrame:
    references = reference_series(dataset)
    records: list[dict[str, int | float | str]] = []
    targets = {
        "consolidated_midpoint": ("consolidated_mid", "mid_return"),
        "consolidated_microprice": ("consolidated_microprice", "microprice_return"),
    }
    for horizon in target_horizons(dataset):
        for target_name, (anchor_column, return_prefix) in targets.items():
            actual_column = f"{return_prefix}_{horizon}"
            for reference_name, reference in references.items():
                frame = pd.concat(
                    [dataset[actual_column], reference, dataset[anchor_column]], axis=1
                ).dropna()
                actual = frame.iloc[:, 0].to_numpy(dtype=float)
                prediction = (frame.iloc[:, 1] - frame.iloc[:, 2]).to_numpy(dtype=float)
                accuracy, balanced_accuracy, roc_auc = _direction_metrics(actual, prediction)
                records.append(
                    {
                        "horizon_ns": horizon,
                        "target_reference": target_name,
                        "predictor_reference": reference_name,
                        "rows": actual.size,
                        "mae_ticks": float(np.mean(np.abs(actual - prediction))),
                        "ic": _information_coefficient(actual, prediction),
                        "direction_accuracy": accuracy,
                        "balanced_accuracy": balanced_accuracy,
                        "roc_auc_nonzero": roc_auc,
                        "target_standard_deviation_ticks": float(np.std(actual)),
                        "evaluation_scope": "full_capture_no_fitted_parameters",
                    }
                )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare midpoint and microprice references")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    results = evaluate_references(load_dataset(args.dataset))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    print(f"rows: {len(results)}")
    print(results.to_string(index=False, float_format=lambda value: f"{value:.6f}"))
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
