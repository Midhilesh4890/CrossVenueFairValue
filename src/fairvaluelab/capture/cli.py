import argparse
import asyncio
from pathlib import Path

from fairvaluelab.capture.core import DEFAULT_VENUES, run_capture


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="fvl-capture")
    parser.add_argument("--symbol", default="BTC-USD")
    parser.add_argument("--venues", nargs="+", choices=DEFAULT_VENUES, default=DEFAULT_VENUES)
    parser.add_argument("--duration", type=float, default=600.0)
    parser.add_argument("--output-directory", type=Path, default=Path("data/capture"))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    paths = asyncio.run(
        run_capture(args.symbol, tuple(args.venues), args.duration, args.output_directory)
    )
    for venue, path in paths.items():
        print(f"{venue}: {path}")
