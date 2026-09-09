import json
from decimal import Decimal
from pathlib import Path

from fairvaluelab.data_quality import RawRecord, analyze_records, build_report


def record(venue: str, timestamp: int, kind: str, payload: dict[str, object]) -> RawRecord:
    return RawRecord(venue, timestamp, kind, payload, True)


def test_data_quality_measures_continuity_books_and_timing() -> None:
    records = [
        record(
            "binance",
            100,
            "snapshot",
            {"lastUpdateId": 10, "bids": [["100", "2"]], "asks": [["101", "1"]]},
        ),
        record(
            "kraken",
            150,
            "snapshot",
            {
                "channel": "book",
                "type": "snapshot",
                "data": [
                    {
                        "bids": [{"price": Decimal("99.9"), "qty": Decimal("1.5")}],
                        "asks": [{"price": Decimal("100.0"), "qty": Decimal("0.5")}],
                        "checksum": 5,
                        "timestamp": "2026-09-10T00:00:00Z",
                    }
                ],
            },
        ),
        record(
            "binance",
            200,
            "depth_diff",
            {"e": "depthUpdate", "E": 1, "U": 12, "u": 12, "b": [["100", "3"]], "a": []},
        ),
        record(
            "binance",
            300,
            "depth_diff",
            {"e": "depthUpdate", "E": 2, "U": 12, "u": 12, "b": [], "a": []},
        ),
        record(
            "binance",
            400,
            "depth_diff",
            {"e": "depthUpdate", "E": 3, "U": 9, "u": 9, "b": [], "a": []},
        ),
        record(
            "binance",
            500,
            "trade",
            {"e": "trade", "T": 4, "t": 20, "q": "0.25"},
        ),
        record("binance", 600, "depth_diff", {"e": "depthUpdate"}),
        RawRecord("binance", None, "invalid", None, False),
    ]

    report = analyze_records(records)
    binance = report["venues"]["binance"]
    kraken = report["venues"]["kraken"]

    assert report["quantile_method"] == "nearest_rank"
    assert binance["events"] == 7
    assert binance["book_updates"] == 4
    assert binance["trades"] == 1
    assert binance["sequence_gaps"] == 1
    assert binance["duplicates"] == 1
    assert binance["stale_events"] == 1
    assert binance["invalid_events"] == 2
    assert binance["missing_timestamps"] == 2
    assert binance["timestamp_regressions"] == 0
    assert binance["exchange_timestamp_regressions"] == 0
    assert binance["timestamp_regression_field"] == "local_receipt_timestamp_ns"
    assert binance["capture_duration_ns"] == 500
    assert binance["inter_arrival_ns"]["median"] == 100
    assert binance["spread"]["median"] == 1.0
    assert binance["visible_depth"]["min"] == 3.0
    assert binance["visible_depth"]["max"] == 4.0
    assert binance["trade_size"]["median"] == 0.25
    assert kraken["book_updates"] == 1
    assert kraken["continuity_limitation"] == "checksum_only_no_numeric_book_sequence"
    assert kraken["venue_age_ns"]["max"] == 450


def test_build_report_reads_capture_envelopes(tmp_path: Path) -> None:
    capture = tmp_path / "kraken.ndjson"
    payload = {
        "channel": "trade",
        "type": "update",
        "data": [
            {
                "side": "buy",
                "qty": 0.125,
                "price": 100.0,
                "trade_id": 7,
                "timestamp": "2026-09-10T00:00:00Z",
            }
        ],
    }
    envelope = {
        "venue": "kraken",
        "record_kind": "trade",
        "local_receipt_timestamp_ns": 123,
        "raw_payload": json.dumps(payload),
    }
    capture.write_text(json.dumps(envelope) + "\nnot-json\n", encoding="utf-8")

    report = build_report(tmp_path)
    kraken = report["venues"]["kraken"]

    assert kraken["events"] == 2
    assert kraken["trades"] == 1
    assert kraken["invalid_events"] == 1
    assert kraken["missing_timestamps"] == 1
    assert report["input_files"] == [capture.as_posix()]
    assert report["generated_at_utc"].endswith("Z")
