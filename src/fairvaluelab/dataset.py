"""Loading and validation helpers for generated cross-venue research datasets."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import pandas as pd

_TARGET_TIMESTAMP = re.compile(r"^target_timestamp_(\d+)$")
_VENUE_RECEIPT_TIMESTAMP = re.compile(
    r"^venue_\d+_latest_local_receipt_timestamp_ns$"
)


class DatasetValidationError(ValueError):
    """Raised when a generated dataset violates its temporal schema."""


def target_horizons(dataset: pd.DataFrame) -> list[int]:
    """Return sorted target horizons encoded in the dataset columns."""
    horizons = {
        int(match.group(1))
        for column in dataset.columns
        if (match := _TARGET_TIMESTAMP.fullmatch(column)) is not None
    }
    return sorted(horizons)


def load_dataset(path: str | Path, *, validate: bool = True) -> pd.DataFrame:
    """Load a generated CSV dataset and optionally enforce temporal invariants."""
    columns = pd.read_csv(path, nrows=0).columns
    unsigned_columns = {
        column: "UInt64"
        for column in columns
        if column == "sample_timestamp_ns"
        or _TARGET_TIMESTAMP.fullmatch(column) is not None
        or column.startswith("target_delay_ns_")
        or column.endswith("_age_ns")
        or _VENUE_RECEIPT_TIMESTAMP.fullmatch(column) is not None
        or column.endswith("_latest_exchange_timestamp_ns")
    }
    dataset = pd.read_csv(path, dtype=unsigned_columns)
    if validate:
        validate_dataset(dataset)
    return dataset


def validate_timestamp_ordering(dataset: pd.DataFrame) -> None:
    """Require non-null, integral, nondecreasing sample receipt timestamps."""
    if "sample_timestamp_ns" not in dataset.columns:
        raise DatasetValidationError("missing sample_timestamp_ns column")
    timestamps = pd.to_numeric(dataset["sample_timestamp_ns"], errors="coerce")
    if timestamps.isna().any():
        raise DatasetValidationError("sample timestamps must be numeric and non-null")
    if ((timestamps % 1) != 0).any():
        raise DatasetValidationError("sample timestamps must be integral nanoseconds")
    if not timestamps.is_monotonic_increasing:
        raise DatasetValidationError("sample timestamps must be nondecreasing")


def validate_target_leakage(dataset: pd.DataFrame) -> None:
    """Check feature and target timestamps against each row's sample timestamp."""
    validate_timestamp_ordering(dataset)
    sample_timestamps = dataset["sample_timestamp_ns"]

    for column in dataset.columns:
        if _VENUE_RECEIPT_TIMESTAMP.fullmatch(column) is None:
            continue
        for row_index, (sample_timestamp, receipt_timestamp) in enumerate(
            zip(sample_timestamps, dataset[column], strict=True)
        ):
            if pd.isna(receipt_timestamp):
                continue
            if int(receipt_timestamp) > int(sample_timestamp):
                raise DatasetValidationError(
                    f"{column} exceeds sample timestamp at row {row_index}"
                )

    for horizon in target_horizons(dataset):
        target_column = f"target_timestamp_{horizon}"
        delay_column = f"target_delay_ns_{horizon}"
        if delay_column not in dataset.columns:
            raise DatasetValidationError(f"missing {delay_column} column")
        for row_index, (sample_timestamp, target_timestamp, target_delay) in enumerate(
            zip(
                sample_timestamps,
                dataset[target_column],
                dataset[delay_column],
                strict=True,
            )
        ):
            if pd.isna(target_timestamp):
                if not pd.isna(target_delay):
                    raise DatasetValidationError(
                        f"{delay_column} is defined without a target at row {row_index}"
                    )
                continue
            threshold = int(sample_timestamp) + horizon
            if int(target_timestamp) < threshold:
                raise DatasetValidationError(
                    f"{target_column} precedes its horizon at row {row_index}"
                )
            expected_delay = int(target_timestamp) - threshold
            if pd.isna(target_delay) or int(target_delay) != expected_delay:
                raise DatasetValidationError(
                    f"{delay_column} is inconsistent at row {row_index}"
                )


def validate_dataset(dataset: pd.DataFrame) -> None:
    """Validate ordering and leakage invariants for a research dataset."""
    if dataset.empty:
        raise DatasetValidationError("dataset contains no rows")
    validate_target_leakage(dataset)


def summarize_missing_values(dataset: pd.DataFrame) -> pd.DataFrame:
    """Return missing counts and fractions for every column."""
    missing_count = dataset.isna().sum()
    return pd.DataFrame(
        {
            "column": dataset.columns,
            "missing_count": missing_count.to_numpy(),
            "missing_fraction": (missing_count / len(dataset)).to_numpy(),
        }
    )


def summarize_target_distributions(dataset: pd.DataFrame) -> pd.DataFrame:
    """Summarize return and direction distributions for every encoded horizon."""
    records: list[dict[str, int | float | str]] = []
    for horizon in target_horizons(dataset):
        for target_name in ("mid", "microprice"):
            return_column = f"{target_name}_return_{horizon}"
            direction_column = f"{target_name}_direction_{horizon}"
            if return_column not in dataset.columns or direction_column not in dataset.columns:
                continue
            returns = pd.to_numeric(dataset[return_column], errors="coerce")
            directions = pd.to_numeric(dataset[direction_column], errors="coerce")
            defined_returns = returns.dropna()
            records.append(
                {
                    "horizon_ns": horizon,
                    "target": target_name,
                    "count": int(defined_returns.size),
                    "missing": int(returns.isna().sum()),
                    "mean": float(defined_returns.mean())
                    if not defined_returns.empty
                    else float("nan"),
                    "std": float(defined_returns.std(ddof=0))
                    if not defined_returns.empty
                    else float("nan"),
                    "minimum": float(defined_returns.min())
                    if not defined_returns.empty
                    else float("nan"),
                    "maximum": float(defined_returns.max())
                    if not defined_returns.empty
                    else float("nan"),
                    "down": int((directions == -1).sum()),
                    "unchanged": int((directions == 0).sum()),
                    "up": int((directions == 1).sum()),
                }
            )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    """Validate a dataset and print compact audit summaries."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=Path)
    args = parser.parse_args(argv)
    dataset = load_dataset(args.dataset)
    missing = summarize_missing_values(dataset)
    targets = summarize_target_distributions(dataset)
    print(f"rows: {len(dataset)}")
    print(f"horizons_ns: {target_horizons(dataset)}")
    print("\nmissing values:")
    print(missing.to_string(index=False))
    print("\ntarget distributions:")
    print(targets.to_string(index=False) if not targets.empty else "no target columns")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
