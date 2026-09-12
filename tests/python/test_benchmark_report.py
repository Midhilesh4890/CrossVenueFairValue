from __future__ import annotations

from fairvaluelab.benchmark_report import parse_output, summarize_measurements


def test_parse_benchmark_output() -> None:
    parsed = parse_output(
        "total events: 1000\nupdates per second: 250.500\n"
        "average nanoseconds per update: 4.000\nresult checksum: 12\n"
    )
    assert parsed["total_events"] == 1000
    assert parsed["updates_per_second"] == 250.5
    assert parsed["average_nanoseconds_per_update"] == 4.0


def test_summarize_measurements_uses_run_aggregates() -> None:
    measurements = [
        {
            "average_ns_per_event": 10.0,
            "events_per_second": 100.0,
            "checksum": 7.0,
        },
        {
            "average_ns_per_event": 14.0,
            "events_per_second": 80.0,
            "checksum": 7.0,
        },
    ]
    summary = summarize_measurements("feature_and_cross_venue", measurements, 1000)
    assert summary["mean_ns_per_event"] == 12.0
    assert summary["median_run_ns_per_event"] == 12.0
    assert summary["mean_events_per_second"] == 90.0
