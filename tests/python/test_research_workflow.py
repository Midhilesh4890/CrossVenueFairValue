from __future__ import annotations

from pathlib import Path

from fairvaluelab.research_workflow import STALENESS_THRESHOLDS, build_commands


def test_workflow_builds_complete_ordered_command_sequence() -> None:
    commands = build_commands(
        Path("capture"),
        Path("build"),
        Path("generated"),
        Path("results"),
        Path("figures"),
        Path("build/fvl_dataset"),
        100,
        2,
    )
    modules = [command[2] for command in commands if command[1:2] == ["-m"]]
    assert modules == [
        "fairvaluelab.data_quality",
        "fairvaluelab.research_dataset",
        "fairvaluelab.dataset",
        "fairvaluelab.dataset",
        "fairvaluelab.dataset",
        "fairvaluelab.baseline",
        "fairvaluelab.cross_venue_study",
        "fairvaluelab.lead_lag",
        "fairvaluelab.reference_study",
        "fairvaluelab.ablation",
        "fairvaluelab.staleness",
        "fairvaluelab.latency",
        "fairvaluelab.benchmark_report",
        "fairvaluelab.latency_power",
        "fairvaluelab.regime",
        "fairvaluelab.visualizations",
    ]
    staleness = next(command for command in commands if "fairvaluelab.staleness" in command)
    specifications = [
        staleness[index + 1]
        for index, value in enumerate(staleness)
        if value == "--dataset"
    ]
    assert len(specifications) == len(STALENESS_THRESHOLDS)
    assert commands[-1][2] == "fairvaluelab.visualizations"
