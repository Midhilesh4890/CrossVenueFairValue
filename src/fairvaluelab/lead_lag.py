from __future__ import annotations

import argparse
import re
from pathlib import Path

import numpy as np
import pandas as pd

from fairvaluelab.dataset import load_dataset

DEFAULT_LAGS_NS = (10_000_000, 25_000_000, 50_000_000, 100_000_000, 250_000_000, 500_000_000)
_VENUE_COLUMN = re.compile(r"^venue_(\d+)_")


def parse_positive_integers(value: str) -> tuple[int, ...]:
    values = tuple(int(item) for item in value.split(","))
    if not values or any(item <= 0 for item in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("values must be unique positive integers")
    return values


def venue_ids(dataset: pd.DataFrame) -> list[int]:
    return sorted(
        {
            int(match.group(1))
            for column in dataset.columns
            if (match := _VENUE_COLUMN.match(column)) is not None
        }
    )


def sampling_interval_ns(dataset: pd.DataFrame) -> int:
    timestamps = dataset["sample_timestamp_ns"].to_numpy(dtype=np.uint64)
    differences = np.diff(timestamps)
    if differences.size == 0 or np.any(differences != differences[0]):
        raise ValueError("lead-lag analysis requires a fixed clock-sampled dataset")
    return int(differences[0])


def venue_signals(dataset: pd.DataFrame, venue_id: int) -> dict[str, pd.Series]:
    prefix = f"venue_{venue_id}_"
    mid = dataset["consolidated_mid"] + dataset[f"{prefix}mid_minus_consolidated_mid"]
    microprice = (
        dataset["consolidated_microprice"]
        + dataset[f"{prefix}microprice_minus_consolidated_microprice"]
    )
    return {
        "mid_price_change": mid.diff(),
        "microprice_change": microprice.diff(),
        "ofi": dataset[f"{prefix}ofi_time_window"],
        "multi_level_ofi": dataset[f"{prefix}multi_level_ofi_time_window"],
        "signed_trade_flow": dataset[f"{prefix}signed_trade_volume_time_window"],
    }


def _correlation(source: pd.Series, response: pd.Series, mask: pd.Series) -> tuple[int, float]:
    paired = pd.concat([source[mask], response[mask]], axis=1).dropna()
    if len(paired) < 2 or paired.iloc[:, 0].std() == 0.0 or paired.iloc[:, 1].std() == 0.0:
        return len(paired), float("nan")
    return len(paired), float(paired.iloc[:, 0].corr(paired.iloc[:, 1]))


def analyze_lead_lag(dataset: pd.DataFrame, lags_ns: tuple[int, ...]) -> pd.DataFrame:
    interval_ns = sampling_interval_ns(dataset)
    if any(lag % interval_ns != 0 for lag in lags_ns):
        raise ValueError("every lag must be an exact multiple of the sampling interval")
    venues = venue_ids(dataset)
    if len(venues) < 2:
        raise ValueError("at least two venues are required")
    signals = {venue: venue_signals(dataset, venue) for venue in venues}
    response_names = {
        "mid_price_change": "mid_price_change",
        "microprice_change": "microprice_change",
        "ofi": "mid_price_change",
        "multi_level_ofi": "mid_price_change",
        "signed_trade_flow": "mid_price_change",
    }
    records: list[dict[str, int | float | str]] = []
    for source_venue in venues:
        for response_venue in venues:
            if source_venue == response_venue:
                continue
            for lag_ns in lags_ns:
                steps = lag_ns // interval_ns
                for source_name, response_name in response_names.items():
                    source = signals[source_venue][source_name]
                    response = signals[response_venue][response_name].shift(-steps)
                    source_valid = dataset[f"venue_{source_venue}_fresh"] == 1
                    response_valid = dataset[f"venue_{response_venue}_fresh"].shift(-steps) == 1
                    observations, correlation = _correlation(
                        source, response, source_valid & response_valid
                    )
                    records.append(
                        {
                            "source_venue_id": source_venue,
                            "response_venue_id": response_venue,
                            "source_signal": source_name,
                            "response_signal": response_name,
                            "lag_ns": lag_ns,
                            "freshness_filter": "fresh_only",
                            "observations": observations,
                            "pearson_correlation": correlation,
                            "sampling_interval_ns": interval_ns,
                        }
                    )
    return pd.DataFrame.from_records(records)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Measure cross-venue lead-lag associations")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--lags-ns", type=parse_positive_integers, default=DEFAULT_LAGS_NS)
    args = parser.parse_args(argv)
    results = analyze_lead_lag(load_dataset(args.dataset), args.lags_ns)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results.to_csv(args.output, index=False)
    defined = results[results["pearson_correlation"].notna()]
    print(f"rows: {len(results)}")
    print(f"defined correlations: {len(defined)}")
    if not defined.empty:
        strongest = defined.loc[defined["pearson_correlation"].abs().idxmax()]
        print(
            "strongest association: "
            f"venue {strongest['source_venue_id']} -> venue {strongest['response_venue_id']}, "
            f"{strongest['source_signal']} -> {strongest['response_signal']}, "
            f"lag_ns={strongest['lag_ns']}, correlation={strongest['pearson_correlation']:.6f}, "
            f"filter={strongest['freshness_filter']}"
        )
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
