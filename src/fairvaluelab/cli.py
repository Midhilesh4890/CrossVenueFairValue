from __future__ import annotations

import argparse
from pathlib import Path


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
