from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pandas as pd


def _finish(figure: plt.Figure, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.tight_layout()
    figure.savefig(output, dpi=160, bbox_inches="tight")
    plt.close(figure)


def plot_cross_venue_mae(results: pd.DataFrame, output: Path) -> None:
    ordered = results.sort_values("horizon_ns")
    horizons_ms = ordered["horizon_ns"] / 1_000_000
    figure, axis = plt.subplots(figsize=(7.2, 4.5))
    axis.plot(horizons_ms, ordered["local_mae"], marker="o", label="Local")
    axis.plot(
        horizons_ms,
        ordered["cross_venue_mae"],
        marker="s",
        label="Local + cross-venue",
    )
    axis.set_xscale("log")
    axis.set_xlabel("Forecast horizon (ms)")
    axis.set_ylabel("MAE (ticks)")
    axis.set_title("Held-out Ridge MAE by forecast horizon")
    axis.grid(alpha=0.25)
    axis.legend()
    axis.text(
        0.01,
        0.98,
        "Descriptive only: held-out target variance is zero",
        transform=axis.transAxes,
        va="top",
        fontsize=9,
    )
    _finish(figure, output)


def plot_staleness_coverage(results: pd.DataFrame, output: Path) -> None:
    coverage = (
        results[["staleness_ns", "all_venues_valid_fraction", "mean_valid_venue_count"]]
        .drop_duplicates()
        .sort_values("staleness_ns")
    )
    thresholds_ms = coverage["staleness_ns"] / 1_000_000
    figure, axis = plt.subplots(figsize=(7.2, 4.5))
    axis.plot(
        thresholds_ms,
        coverage["all_venues_valid_fraction"] * 100,
        marker="o",
        color="tab:blue",
    )
    axis.set_xlabel("Venue freshness threshold (ms)")
    axis.set_ylabel("Rows with all venues valid (%)")
    axis.set_title("Cross-venue coverage versus freshness threshold")
    axis.set_ylim(bottom=0)
    axis.grid(alpha=0.25)
    _finish(figure, output)


def plot_lead_lag(results: pd.DataFrame, output: Path) -> None:
    selected = results.loc[
        (results["source_signal"] == "microprice_change")
        & (results["response_signal"] == "microprice_change")
        & results["pearson_correlation"].notna()
    ].copy()
    if selected.empty:
        raise ValueError("lead-lag results contain no defined microprice correlations")
    figure, axis = plt.subplots(figsize=(7.2, 4.5))
    for (source, response), group in selected.groupby(
        ["source_venue_id", "response_venue_id"]
    ):
        ordered = group.sort_values("lag_ns")
        axis.plot(
            ordered["lag_ns"] / 1_000_000,
            ordered["pearson_correlation"],
            marker="o",
            label=f"Venue {source} → venue {response}",
        )
    axis.axhline(0, color="black", linewidth=0.8)
    axis.set_xlabel("Lag (ms)")
    axis.set_ylabel("Pearson correlation")
    axis.set_title("Microprice-change lead-lag association")
    axis.grid(alpha=0.25)
    axis.legend()
    _finish(figure, output)


def generate_figures(results_directory: Path, output_directory: Path) -> list[Path]:
    outputs = [
        output_directory / "cross_venue_mae.png",
        output_directory / "staleness_coverage.png",
        output_directory / "lead_lag_microprice.png",
    ]
    plot_cross_venue_mae(
        pd.read_csv(results_directory / "cross_venue_results.csv"), outputs[0]
    )
    plot_staleness_coverage(
        pd.read_csv(results_directory / "staleness_results.csv"), outputs[1]
    )
    plot_lead_lag(pd.read_csv(results_directory / "lead_lag_results.csv"), outputs[2])
    return outputs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate research figures from result tables")
    parser.add_argument("--results-directory", type=Path, default=Path("research/results"))
    parser.add_argument("--output-directory", type=Path, default=Path("research/figures"))
    args = parser.parse_args(argv)
    for output in generate_figures(args.results_directory, args.output_directory):
        print(f"figure: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
