from __future__ import annotations

import argparse
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import pandas as pd

from fairvaluelab.dataset import load_dataset, target_horizons

DEFAULT_HORIZONS_NS = (10_000_000, 50_000_000, 100_000_000, 250_000_000, 1_000_000_000)
DEFAULT_STALENESS_NS = (25_000_000, 50_000_000, 100_000_000, 250_000_000)


def parse_positive_integers(value: str) -> tuple[int, ...]:
    values = tuple(int(item) for item in value.split(","))
    if not values or any(item <= 0 for item in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("values must be unique positive integers")
    return values


def resolve_executable(build_directory: Path, name: str) -> Path:
    candidates = (build_directory / name, build_directory / f"{name}.exe")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"missing {name} executable in {build_directory}")


def load_provenance(capture_directory: Path) -> list[dict[str, Any]]:
    provenance = []
    for path in sorted(capture_directory.glob("*.metadata.json")):
        with path.open(encoding="utf-8") as input_file:
            provenance.append(json.load(input_file))
    if not provenance:
        raise ValueError(f"no provenance metadata found in {capture_directory}")
    return provenance


def portable_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def summarize_dataset(dataset: pd.DataFrame, path: Path, staleness_ns: int) -> dict[str, Any]:
    horizons = target_horizons(dataset)
    valid_counts = pd.to_numeric(dataset["valid_venue_count"], errors="raise")
    distribution = valid_counts.value_counts().sort_index()
    return {
        "dataset_file": portable_path(path),
        "rows": len(dataset),
        "staleness_ns": staleness_ns,
        "prediction_horizons_ns": horizons,
        "missing_target_counts": {
            str(horizon): int(dataset[f"target_timestamp_{horizon}"].isna().sum())
            for horizon in horizons
        },
        "valid_venue_counts": {
            "minimum": int(valid_counts.min()),
            "maximum": int(valid_counts.max()),
            "mean": float(valid_counts.mean()),
            "distribution": {str(int(count)): int(rows) for count, rows in distribution.items()},
        },
        "leakage_validation": "passed",
    }


def build_metadata(
    provenance: list[dict[str, Any]],
    normalized_path: Path,
    clock_ns: int,
    max_target_delay_ns: int,
    variants: list[dict[str, Any]],
) -> dict[str, Any]:
    starts = [item["capture_start_timestamp_ns"] for item in provenance]
    ends = [item["capture_end_timestamp_ns"] for item in provenance]
    return {
        "schema_version": 1,
        "generated_at_utc": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
        "venue_set": [item["venue"] for item in provenance],
        "instruments": {item["venue"]: item["instrument"] for item in provenance},
        "time_period": {
            "start_timestamp_ns": min(starts),
            "end_timestamp_ns": max(ends),
            "start_utc": min(item["capture_start_utc"] for item in provenance),
            "end_utc": max(item["capture_end_utc"] for item in provenance),
        },
        "sources": {item["venue"]: item["source"] for item in provenance},
        "source_event_counts": {item["venue"]: item["event_count"] for item in provenance},
        "source_software_revisions": {
            item["venue"]: item["software_revision"] for item in provenance
        },
        "normalized_file": portable_path(normalized_path),
        "sampling": "clock",
        "sampling_interval_ns": clock_ns,
        "max_target_delay_ns": max_target_delay_ns,
        "variants": variants,
    }


def generate(args: argparse.Namespace) -> dict[str, Any]:
    capture_directory = args.capture_directory.resolve()
    output_directory = args.output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    normalized_path = output_directory / "normalized.csv"
    converter = resolve_executable(args.build_directory.resolve(), "fvl_convert_capture")
    dataset_tool = resolve_executable(args.build_directory.resolve(), "fvl_dataset")
    subprocess.run([converter, capture_directory, normalized_path], check=True)
    variants = []
    horizon_argument = ",".join(str(value) for value in args.horizons_ns)
    for staleness_ns in args.staleness_ns:
        dataset_path = output_directory / f"dataset_staleness_{staleness_ns}.csv"
        subprocess.run(
            [
                dataset_tool,
                "--input",
                normalized_path,
                "--output",
                dataset_path,
                "--sampling",
                "clock",
                "--clock-ns",
                str(args.clock_ns),
                "--max-staleness-ns",
                str(staleness_ns),
                "--horizons-ns",
                horizon_argument,
                "--max-target-delay-ns",
                str(args.max_target_delay_ns),
            ],
            check=True,
        )
        variants.append(summarize_dataset(load_dataset(dataset_path), dataset_path, staleness_ns))
    metadata = build_metadata(
        load_provenance(capture_directory),
        normalized_path,
        args.clock_ns,
        args.max_target_delay_ns,
        variants,
    )
    args.metadata_output.parent.mkdir(parents=True, exist_ok=True)
    with args.metadata_output.open("w", encoding="utf-8") as output_file:
        json.dump(metadata, output_file, indent=2)
        output_file.write("\n")
    return metadata


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate and validate clock-sampled datasets from a real capture"
    )
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("--build-directory", type=Path, default=Path("build"))
    parser.add_argument("--output-directory", type=Path, default=Path("data/generated/real"))
    parser.add_argument(
        "--metadata-output", type=Path, default=Path("research/results/real_dataset.json")
    )
    parser.add_argument("--clock-ns", type=int, default=50_000_000)
    parser.add_argument(
        "--horizons-ns", type=parse_positive_integers, default=DEFAULT_HORIZONS_NS
    )
    parser.add_argument(
        "--staleness-ns", type=parse_positive_integers, default=DEFAULT_STALENESS_NS
    )
    parser.add_argument("--max-target-delay-ns", type=int, default=100_000_000)
    args = parser.parse_args(argv)
    if args.clock_ns <= 0 or args.max_target_delay_ns < 0:
        parser.error("clock-ns must be positive and max-target-delay-ns must be nonnegative")
    metadata = generate(args)
    print(f"generated {len(metadata['variants'])} validated dataset variants")
    print(f"metadata: {args.metadata_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
