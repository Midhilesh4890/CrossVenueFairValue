from __future__ import annotations

import argparse
import json
import platform
import re
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from statistics import mean, median
from typing import Any


def resolve_executable(build_directory: Path, name: str) -> Path:
    for candidate in (build_directory / name, build_directory / f"{name}.exe"):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"missing {name} executable in {build_directory}")


def parse_output(output: str) -> dict[str, float | int]:
    values: dict[str, float | int] = {}
    for line in output.splitlines():
        key, separator, value = line.partition(":")
        if not separator:
            continue
        normalized = key.strip().lower().replace(" ", "_").replace("/", "_per_")
        text = value.strip()
        try:
            values[normalized] = float(text) if "." in text else int(text)
        except ValueError:
            continue
    return values


def compiler_metadata(build_directory: Path) -> tuple[str, str, str]:
    cache = (build_directory / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    compiler_match = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.+)$", cache, re.MULTILINE)
    config_match = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.+)$", cache, re.MULTILINE)
    if compiler_match is None:
        raise ValueError("CMake cache does not identify the C++ compiler")
    compiler_path = Path(compiler_match.group(1).strip())
    version_output = subprocess.run(
        [compiler_path, "--version"], check=True, capture_output=True, text=True
    ).stdout.splitlines()[0]
    return compiler_path.as_posix(), version_output, config_match.group(1) if config_match else ""


def run_benchmark(executable: Path, event_count: int, repetitions: int) -> list[dict[str, Any]]:
    command = [executable, "--events", str(event_count)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    measurements = []
    for _ in range(repetitions):
        completed = subprocess.run(command, check=True, capture_output=True, text=True)
        measurements.append(parse_output(completed.stdout))
    return measurements


def summarize_measurements(
    name: str, measurements: list[dict[str, Any]], event_count: int
) -> dict[str, Any]:
    if name == "order_book_update":
        latency_key = "average_nanoseconds_per_update"
        rate_key = "updates_per_second"
        checksum_key = "result_checksum"
    else:
        latency_key = "average_ns_per_event"
        rate_key = "events_per_second"
        checksum_key = "checksum"
    latencies = [float(measurement[latency_key]) for measurement in measurements]
    rates = [float(measurement[rate_key]) for measurement in measurements]
    checksums = {float(measurement[checksum_key]) for measurement in measurements}
    if len(checksums) != 1:
        raise ValueError(f"{name} checksum changed between repetitions")
    return {
        "benchmark": name,
        "event_count": event_count,
        "repetitions": len(measurements),
        "mean_ns_per_event": mean(latencies),
        "median_run_ns_per_event": median(latencies),
        "minimum_run_ns_per_event": min(latencies),
        "maximum_run_ns_per_event": max(latencies),
        "mean_events_per_second": mean(rates),
        "checksum": checksums.pop(),
    }


def build_report(
    build_directory: Path,
    event_count: int,
    repetitions: int,
    cpu: str,
    revision: str,
) -> dict[str, Any]:
    compiler_path, compiler_version, configuration = compiler_metadata(build_directory)
    benchmarks = {
        "order_book_update": resolve_executable(build_directory, "fvl_order_book_benchmark"),
        "feature_and_cross_venue": resolve_executable(
            build_directory, "fvl_cross_venue_benchmark"
        ),
    }
    results = [
        summarize_measurements(
            name, run_benchmark(executable, event_count, repetitions), event_count
        )
        for name, executable in benchmarks.items()
    ]
    return {
        "schema_version": 1,
        "generated_at_utc": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
        "machine_specific": True,
        "cpu": cpu,
        "os": platform.platform(),
        "compiler_path": compiler_path,
        "compiler_version": compiler_version,
        "build_configuration": configuration,
        "software_revision": revision,
        "methodology": "one_unrecorded_warmup_then_repeated_end_to_end_timed_runs",
        "latency_percentiles": "not_measured",
        "results": results,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run and record machine-specific C++ benchmarks")
    parser.add_argument("--build-directory", type=Path, default=Path("build"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--events", type=int, default=5_000_000)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--cpu", default=platform.processor() or "unknown")
    args = parser.parse_args(argv)
    if args.events <= 0 or args.repetitions <= 0:
        parser.error("events and repetitions must be positive")
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()
    report = build_report(
        args.build_directory.resolve(), args.events, args.repetitions, args.cpu, revision
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output_file:
        json.dump(report, output_file, indent=2)
        output_file.write("\n")
    for result in report["results"]:
        print(
            f"{result['benchmark']}: mean={result['mean_ns_per_event']:.3f} ns/event, "
            f"throughput={result['mean_events_per_second']:.3f} events/s"
        )
    print(f"results: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
