import argparse
import asyncio
from pathlib import Path

from fairvaluelab.capture.core import (
    DEFAULT_VENUES,
    SUPPORTED_VENUES,
    run_capture,
    summarize_capture,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="fvl-capture")
    parser.add_argument("--symbol", default="BTC-USD")
    parser.add_argument("--venues", nargs="+", choices=SUPPORTED_VENUES, default=DEFAULT_VENUES)
    parser.add_argument(
        "--duration-seconds",
        "--duration",
        type=float,
        default=600.0,
        help="maximum capture duration",
    )
    parser.add_argument("--max-events", type=int, help="maximum raw records per venue")
    parser.add_argument("--output-directory", type=Path, default=Path("data/capture"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--silence-threshold", type=float, default=5.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    paths = asyncio.run(
        run_capture(
            args.symbol,
            tuple(args.venues),
            args.duration_seconds,
            args.output_directory,
            args.max_events,
        )
    )
    for venue, path in paths.items():
        print(f"{venue}: {path}")
    if args.validate:
        summaries = summarize_capture(paths, args.silence_threshold)
        for venue, summary in summaries.items():
            span = (
                "none"
                if summary.receipt_timestamp_span_ns is None
                else str(summary.receipt_timestamp_span_ns)
            )
            max_silent_gap = (
                "none" if summary.max_silent_gap_ns is None else str(summary.max_silent_gap_ns)
            )
            print(
                f"{venue}: total_records={summary.total_records} "
                f"records_by_kind={summary.records_by_kind} "
                f"receipt_timestamp_span_ns={span} "
                f"silent_gap_count={summary.silent_gap_count} "
                f"max_silent_gap_ns={max_silent_gap} "
                f"silence_threshold_ns={summary.silence_threshold_ns}"
            )
