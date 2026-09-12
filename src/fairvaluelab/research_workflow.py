from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from fairvaluelab.research_dataset import resolve_executable

HORIZONS = "10000000,50000000,100000000,250000000,1000000000"
STALENESS_THRESHOLDS = (25_000_000, 50_000_000, 100_000_000, 250_000_000, 500_000_000)


def module_command(module: str, *arguments: object) -> list[str]:
    return [sys.executable, "-m", module, *(str(argument) for argument in arguments)]


def build_commands(
    capture_directory: Path,
    build_directory: Path,
    generated_directory: Path,
    results_directory: Path,
    figures_directory: Path,
    dataset_executable: Path,
    benchmark_events: int,
    benchmark_repetitions: int,
) -> list[list[str]]:
    normalized = generated_directory / "normalized.csv"
    primary_dataset = generated_directory / "dataset_staleness_100000000.csv"
    event_dataset = generated_directory / "dataset_event_100000000.csv"
    lead_lag_dataset = generated_directory / "dataset_lead_lag_5000000.csv"
    baseline_results = results_directory / "baseline_results.csv"
    benchmark_results = results_directory / "benchmark_results.json"
    commands = [
        module_command(
            "fairvaluelab.data_quality",
            capture_directory,
            "--output",
            results_directory / "data_quality.json",
        ),
        module_command(
            "fairvaluelab.research_dataset",
            capture_directory,
            "--build-directory",
            build_directory,
            "--output-directory",
            generated_directory,
            "--metadata-output",
            results_directory / "real_dataset.json",
            "--staleness-ns",
            ",".join(str(value) for value in STALENESS_THRESHOLDS),
        ),
        [
            str(dataset_executable),
            "--input",
            str(normalized),
            "--output",
            str(event_dataset),
            "--sampling",
            "event",
            "--max-staleness-ns",
            "100000000",
            "--horizons-ns",
            HORIZONS,
            "--max-target-delay-ns",
            "100000000",
        ],
        [
            str(dataset_executable),
            "--input",
            str(normalized),
            "--output",
            str(lead_lag_dataset),
            "--sampling",
            "clock",
            "--clock-ns",
            "5000000",
            "--max-staleness-ns",
            "100000000",
            "--horizons-ns",
            HORIZONS,
            "--max-target-delay-ns",
            "100000000",
        ],
        module_command("fairvaluelab.dataset", primary_dataset),
        module_command("fairvaluelab.dataset", event_dataset),
        module_command("fairvaluelab.dataset", lead_lag_dataset),
        module_command(
            "fairvaluelab.baseline",
            "--dataset",
            primary_dataset,
            "--primary-venue",
            1,
            "--output",
            baseline_results,
        ),
        module_command(
            "fairvaluelab.cross_venue_study",
            "--dataset",
            primary_dataset,
            "--output",
            results_directory / "cross_venue_results.csv",
            "--primary-venue",
            1,
        ),
        module_command(
            "fairvaluelab.lead_lag",
            "--dataset",
            lead_lag_dataset,
            "--output",
            results_directory / "lead_lag_results.csv",
        ),
        module_command(
            "fairvaluelab.reference_study",
            "--dataset",
            primary_dataset,
            "--output",
            results_directory / "microprice_results.csv",
        ),
        module_command(
            "fairvaluelab.ablation",
            "--dataset",
            primary_dataset,
            "--output",
            results_directory / "ablation_results.csv",
            "--primary-venue",
            1,
        ),
    ]
    staleness_command = module_command("fairvaluelab.staleness")
    for threshold in STALENESS_THRESHOLDS:
        staleness_command.extend(
            [
                "--dataset",
                f"{threshold}={generated_directory / f'dataset_staleness_{threshold}.csv'}",
            ]
        )
    staleness_command.extend(
        ["--output", str(results_directory / "staleness_results.csv"), "--primary-venue", "1"]
    )
    commands.extend(
        [
            staleness_command,
            module_command(
                "fairvaluelab.latency",
                "--dataset",
                event_dataset,
                "--output",
                results_directory / "latency_results.csv",
                "--primary-venue",
                1,
            ),
            module_command(
                "fairvaluelab.benchmark_report",
                "--build-directory",
                build_directory,
                "--output",
                benchmark_results,
                "--events",
                benchmark_events,
                "--repetitions",
                benchmark_repetitions,
            ),
            module_command(
                "fairvaluelab.latency_power",
                "--dataset",
                primary_dataset,
                "--baseline-results",
                baseline_results,
                "--benchmark-results",
                benchmark_results,
                "--output",
                results_directory / "latency_power_results.csv",
                "--primary-venue",
                1,
            ),
            module_command(
                "fairvaluelab.regime",
                "--dataset",
                primary_dataset,
                "--output",
                results_directory / "regime_results.csv",
                "--primary-venue",
                1,
            ),
            module_command(
                "fairvaluelab.visualizations",
                "--results-directory",
                results_directory,
                "--output-directory",
                figures_directory,
            ),
        ]
    )
    return commands


def run_commands(commands: list[list[str]], dry_run: bool) -> None:
    for index, command in enumerate(commands, start=1):
        print(f"[{index}/{len(commands)}] {subprocess.list2cmdline(command)}", flush=True)
        if not dry_run:
            subprocess.run(command, check=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Reproduce the empirical research pipeline")
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("--build-directory", type=Path, default=Path("build"))
    parser.add_argument("--generated-directory", type=Path, default=Path("data/generated/research"))
    parser.add_argument("--results-directory", type=Path, default=Path("research/results"))
    parser.add_argument("--figures-directory", type=Path, default=Path("research/figures"))
    parser.add_argument("--benchmark-events", type=int, default=5_000_000)
    parser.add_argument("--benchmark-repetitions", type=int, default=5)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.benchmark_events <= 0 or args.benchmark_repetitions <= 0:
        parser.error("benchmark events and repetitions must be positive")
    dataset_executable = (
        args.build_directory / "fvl_dataset"
        if args.dry_run
        else resolve_executable(args.build_directory.resolve(), "fvl_dataset")
    )
    commands = build_commands(
        args.capture_directory,
        args.build_directory,
        args.generated_directory,
        args.results_directory,
        args.figures_directory,
        dataset_executable,
        args.benchmark_events,
        args.benchmark_repetitions,
    )
    run_commands(commands, args.dry_run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
