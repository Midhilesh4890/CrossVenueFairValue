import argparse
import json
import math
from dataclasses import dataclass, field
from datetime import UTC, datetime
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class RawRecord:
    venue: str
    receipt_timestamp_ns: int | None
    record_kind: str
    payload: object | None
    valid_envelope: bool


@dataclass
class VenueState:
    events: int = 0
    book_updates: int = 0
    trades: int = 0
    sequence_gaps: int = 0
    duplicates: int = 0
    stale_events: int = 0
    invalid_events: int = 0
    crossed_books: int = 0
    locked_books: int = 0
    missing_timestamps: int = 0
    timestamp_regressions: int = 0
    exchange_timestamp_regressions: int = 0
    first_receipt_ns: int | None = None
    last_receipt_ns: int | None = None
    previous_receipt_ns: int | None = None
    previous_exchange_ns: int | None = None
    last_source_sequence: int | None = None
    bids: dict[Decimal, Decimal] = field(default_factory=dict)
    asks: dict[Decimal, Decimal] = field(default_factory=dict)
    inter_arrival_ns: list[int] = field(default_factory=list)
    spreads: list[Decimal] = field(default_factory=list)
    depths: list[Decimal] = field(default_factory=list)
    imbalances: list[Decimal] = field(default_factory=list)
    trade_sizes: list[Decimal] = field(default_factory=list)
    venue_ages_ns: list[int] = field(default_factory=list)


def _decimal(value: object) -> Decimal:
    if isinstance(value, Decimal):
        result = value
    elif isinstance(value, (str, int)) and not isinstance(value, bool):
        result = Decimal(value)
    else:
        raise ValueError("invalid decimal")
    if not result.is_finite():
        raise ValueError("non-finite decimal")
    return result


def _integer(value: object) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError("invalid nonnegative integer")
    return value


def _parse_payload(raw_payload: object) -> object:
    if not isinstance(raw_payload, str):
        raise ValueError("raw payload is not a string")
    return json.loads(raw_payload, parse_float=Decimal)


def read_records(paths: list[Path]) -> list[RawRecord]:
    records: list[RawRecord] = []
    for path in paths:
        fallback_venue = path.stem.lower()
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line:
                continue
            try:
                envelope = json.loads(line)
                if not isinstance(envelope, dict):
                    raise ValueError("capture envelope is not an object")
                venue = str(envelope.get("venue", fallback_venue)).lower()
                receipt = envelope.get("local_receipt_timestamp_ns")
                receipt_timestamp_ns = (
                    receipt if isinstance(receipt, int) and not isinstance(receipt, bool) else None
                )
                record_kind = str(envelope.get("record_kind", "unknown"))
                payload = _parse_payload(envelope.get("raw_payload"))
                records.append(
                    RawRecord(venue, receipt_timestamp_ns, record_kind, payload, True)
                )
            except (json.JSONDecodeError, ValueError, TypeError):
                records.append(RawRecord(fallback_venue, None, "invalid", None, False))
    return records


def _levels(values: object, object_style: bool) -> list[tuple[Decimal, Decimal]]:
    if not isinstance(values, list):
        raise ValueError("levels are not an array")
    levels: list[tuple[Decimal, Decimal]] = []
    for value in values:
        if object_style:
            if not isinstance(value, dict):
                raise ValueError("level is not an object")
            price = _decimal(value.get("price"))
            quantity = _decimal(value.get("qty"))
        else:
            if not isinstance(value, list) or len(value) < 2:
                raise ValueError("level is not an array pair")
            price = _decimal(value[0])
            quantity = _decimal(value[1])
        if price <= 0 or quantity < 0:
            raise ValueError("invalid level value")
        levels.append((price, quantity))
    return levels


def _apply_levels(book: dict[Decimal, Decimal], levels: list[tuple[Decimal, Decimal]]) -> None:
    for price, quantity in levels:
        if quantity == 0:
            book.pop(price, None)
        else:
            book[price] = quantity


def _observe_book(state: VenueState) -> None:
    if not state.bids or not state.asks:
        return
    best_bid = max(state.bids)
    best_ask = min(state.asks)
    if best_bid > best_ask:
        state.crossed_books += 1
    elif best_bid == best_ask:
        state.locked_books += 1
    bid_depth = sum(state.bids.values(), Decimal(0))
    ask_depth = sum(state.asks.values(), Decimal(0))
    total_depth = bid_depth + ask_depth
    state.spreads.append(best_ask - best_bid)
    state.depths.append(total_depth)
    if total_depth > 0:
        state.imbalances.append((bid_depth - ask_depth) / total_depth)


def _observe_sequence(state: VenueState, first: int, final: int) -> None:
    previous = state.last_source_sequence
    if previous is not None:
        if final == previous:
            state.duplicates += 1
        elif final < previous:
            state.stale_events += 1
        elif first > previous + 1:
            state.sequence_gaps += 1
    if previous is None or final > previous:
        state.last_source_sequence = final


def _process_binance(state: VenueState, payload: dict[str, Any], kind: str) -> None:
    data = payload.get("data", payload)
    if not isinstance(data, dict):
        raise ValueError("invalid Binance payload")
    if kind == "snapshot":
        state.bids.clear()
        state.asks.clear()
        _apply_levels(state.bids, _levels(data.get("bids"), False))
        _apply_levels(state.asks, _levels(data.get("asks"), False))
        sequence = _integer(data.get("lastUpdateId"))
        state.last_source_sequence = sequence
        state.book_updates += 1
        _observe_book(state)
    elif kind == "depth_diff":
        first = _integer(data.get("U"))
        final = _integer(data.get("u"))
        if final < first:
            raise ValueError("invalid sequence range")
        _observe_sequence(state, first, final)
        _apply_levels(state.bids, _levels(data.get("b"), False))
        _apply_levels(state.asks, _levels(data.get("a"), False))
        _integer(data.get("E"))
        state.book_updates += 1
        _observe_book(state)
    elif kind == "trade":
        state.trade_sizes.append(_decimal(data.get("q")))
        _integer(data.get("T"))
        _integer(data.get("t"))
        state.trades += 1


def _process_kraken(state: VenueState, payload: dict[str, Any], kind: str) -> None:
    data = payload.get("data")
    if not isinstance(data, list) or not data:
        raise ValueError("invalid Kraken data")
    if kind in {"snapshot", "depth_diff"}:
        if kind == "snapshot":
            state.bids.clear()
            state.asks.clear()
        for entry in data:
            if not isinstance(entry, dict) or not isinstance(entry.get("timestamp"), str):
                raise ValueError("invalid Kraken book entry")
            _apply_levels(state.bids, _levels(entry.get("bids"), True))
            _apply_levels(state.asks, _levels(entry.get("asks"), True))
            _integer(entry.get("checksum"))
        state.book_updates += 1
        _observe_book(state)
    elif kind == "trade":
        for trade in data:
            if not isinstance(trade, dict) or not isinstance(trade.get("timestamp"), str):
                raise ValueError("invalid Kraken trade")
            state.trade_sizes.append(_decimal(trade.get("qty")))
            _integer(trade.get("trade_id"))
            state.trades += 1


def _process_coinbase(state: VenueState, payload: dict[str, Any], kind: str) -> None:
    if kind == "snapshot":
        state.bids.clear()
        state.asks.clear()
        _apply_levels(state.bids, _levels(payload.get("bids"), False))
        _apply_levels(state.asks, _levels(payload.get("asks"), False))
        state.book_updates += 1
        _observe_book(state)
    elif kind == "depth_diff":
        changes = payload.get("changes")
        if not isinstance(changes, list) or not isinstance(payload.get("time"), str):
            raise ValueError("invalid Coinbase update")
        for change in changes:
            if not isinstance(change, list) or len(change) < 3:
                raise ValueError("invalid Coinbase change")
            destination = state.bids if change[0] == "buy" else state.asks
            if change[0] not in {"buy", "sell"}:
                raise ValueError("invalid Coinbase side")
            _apply_levels(destination, [(_decimal(change[1]), _decimal(change[2]))])
        state.book_updates += 1
        _observe_book(state)
    elif kind == "trade":
        if not isinstance(payload.get("time"), str):
            raise ValueError("invalid Coinbase trade")
        state.trade_sizes.append(_decimal(payload.get("size")))
        _integer(payload.get("trade_id"))
        state.trades += 1


def _process_market_record(state: VenueState, record: RawRecord) -> None:
    if record.record_kind not in {"snapshot", "depth_diff", "trade"}:
        return
    if not isinstance(record.payload, dict):
        raise ValueError("market payload is not an object")
    if record.venue == "binance":
        _process_binance(state, record.payload, record.record_kind)
    elif record.venue == "kraken":
        _process_kraken(state, record.payload, record.record_kind)
    elif record.venue == "coinbase":
        _process_coinbase(state, record.payload, record.record_kind)
    else:
        raise ValueError("unsupported market venue")


def _has_exchange_timestamp(record: RawRecord) -> bool:
    if record.record_kind not in {"snapshot", "depth_diff", "trade"}:
        return True
    if not isinstance(record.payload, dict):
        return False
    payload = record.payload
    if record.venue == "binance":
        data = payload.get("data", payload)
        if not isinstance(data, dict):
            return False
        field = "T" if record.record_kind == "trade" else "E"
        return record.record_kind == "snapshot" or field in data
    if record.venue == "coinbase":
        return record.record_kind == "snapshot" or isinstance(payload.get("time"), str)
    if record.venue == "kraken":
        data = payload.get("data")
        return (
            isinstance(data, list)
            and bool(data)
            and all(
                isinstance(entry, dict) and isinstance(entry.get("timestamp"), str)
                for entry in data
            )
        )
    return False


def _iso_timestamp_ns(value: str) -> int:
    timestamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if timestamp.tzinfo is None:
        raise ValueError("timestamp lacks timezone")
    return int(timestamp.timestamp() * 1_000_000_000)


def _exchange_timestamp_ns(record: RawRecord) -> int | None:
    if record.record_kind not in {"depth_diff", "trade"}:
        return None
    if not isinstance(record.payload, dict):
        return None
    payload = record.payload
    if record.venue == "binance":
        data = payload.get("data", payload)
        if not isinstance(data, dict):
            return None
        value = data.get("T" if record.record_kind == "trade" else "E")
        return _integer(value) * 1_000_000
    if record.venue == "coinbase":
        value = payload.get("time")
        return _iso_timestamp_ns(value) if isinstance(value, str) else None
    if record.venue == "kraken":
        data = payload.get("data")
        if not isinstance(data, list) or not data or not isinstance(data[-1], dict):
            return None
        value = data[-1].get("timestamp")
        return _iso_timestamp_ns(value) if isinstance(value, str) else None
    return None


def _iso_timestamp_ns(value: str) -> int:
    timestamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if timestamp.tzinfo is None:
        raise ValueError("timestamp lacks timezone")
    return int(timestamp.timestamp() * 1_000_000_000)


def _exchange_timestamp_ns(record: RawRecord) -> int | None:
    if record.record_kind not in {"depth_diff", "trade"}:
        return None
    if not isinstance(record.payload, dict):
        return None
    payload = record.payload
    if record.venue == "binance":
        data = payload.get("data", payload)
        if not isinstance(data, dict):
            return None
        value = data.get("T" if record.record_kind == "trade" else "E")
        return _integer(value) * 1_000_000
    if record.venue == "coinbase":
        value = payload.get("time")
        return _iso_timestamp_ns(value) if isinstance(value, str) else None
    if record.venue == "kraken":
        data = payload.get("data")
        if not isinstance(data, list) or not data or not isinstance(data[-1], dict):
            return None
        value = data[-1].get("timestamp")
        return _iso_timestamp_ns(value) if isinstance(value, str) else None
    return None


def _summary(values: list[int] | list[Decimal]) -> dict[str, int | float | None]:
    if not values:
        return {"count": 0, "min": None, "median": None, "p95": None, "p99": None, "max": None}
    ordered = sorted(values)

    def percentile(probability: float) -> int | float:
        index = max(0, math.ceil(probability * len(ordered)) - 1)
        value = ordered[index]
        return int(value) if isinstance(value, int) else float(value)

    minimum = ordered[0]
    maximum = ordered[-1]
    return {
        "count": len(ordered),
        "min": int(minimum) if isinstance(minimum, int) else float(minimum),
        "median": percentile(0.5),
        "p95": percentile(0.95),
        "p99": percentile(0.99),
        "max": int(maximum) if isinstance(maximum, int) else float(maximum),
    }


def analyze_records(records: list[RawRecord]) -> dict[str, object]:
    states: dict[str, VenueState] = {}
    for record in records:
        state = states.setdefault(record.venue, VenueState())
        state.events += 1
        if not record.valid_envelope:
            state.invalid_events += 1
            state.missing_timestamps += 1
            continue
        receipt = record.receipt_timestamp_ns
        if receipt is None or not _has_exchange_timestamp(record):
            state.missing_timestamps += 1
        if receipt is not None:
            if state.first_receipt_ns is None:
                state.first_receipt_ns = receipt
            state.last_receipt_ns = max(state.last_receipt_ns or receipt, receipt)
            if state.previous_receipt_ns is not None:
                if receipt < state.previous_receipt_ns:
                    state.timestamp_regressions += 1
                else:
                    state.inter_arrival_ns.append(receipt - state.previous_receipt_ns)
            state.previous_receipt_ns = receipt
        try:
            exchange_timestamp = _exchange_timestamp_ns(record)
        except (TypeError, ValueError):
            exchange_timestamp = None
        if exchange_timestamp is not None:
            if (
                state.previous_exchange_ns is not None
                and exchange_timestamp < state.previous_exchange_ns
            ):
                state.exchange_timestamp_regressions += 1
            state.previous_exchange_ns = exchange_timestamp
        try:
            _process_market_record(state, record)
        except (InvalidOperation, KeyError, TypeError, ValueError):
            state.invalid_events += 1

    timed_records = sorted(
        (record for record in records if record.receipt_timestamp_ns is not None),
        key=lambda record: record.receipt_timestamp_ns or 0,
    )
    last_seen: dict[str, int] = {}
    for record in timed_records:
        timestamp = record.receipt_timestamp_ns
        if timestamp is None:
            continue
        last_seen[record.venue] = timestamp
        for venue, seen_timestamp in last_seen.items():
            states[venue].venue_ages_ns.append(timestamp - seen_timestamp)

    venues: dict[str, object] = {}
    for venue, state in sorted(states.items()):
        duration = (
            state.last_receipt_ns - state.first_receipt_ns
            if state.first_receipt_ns is not None and state.last_receipt_ns is not None
            else None
        )
        has_numeric_book_sequence = venue != "kraken"
        venues[venue] = {
            "events": state.events,
            "book_updates": state.book_updates,
            "trades": state.trades,
            "sequence_gaps": state.sequence_gaps if has_numeric_book_sequence else None,
            "duplicates": state.duplicates if has_numeric_book_sequence else None,
            "stale_events": state.stale_events if has_numeric_book_sequence else None,
            "invalid_events": state.invalid_events,
            "crossed_books": state.crossed_books,
            "locked_books": state.locked_books,
            "missing_timestamps": state.missing_timestamps,
            "timestamp_regressions": state.timestamp_regressions,
            "timestamp_regression_field": "local_receipt_timestamp_ns",
            "exchange_timestamp_regressions": state.exchange_timestamp_regressions,
            "exchange_timestamp_regression_scope": "combined_book_and_trade_message_order",
            "capture_duration_ns": duration,
            "inter_arrival_ns": _summary(state.inter_arrival_ns),
            "spread": _summary(state.spreads),
            "visible_depth": _summary(state.depths),
            "imbalance": _summary(state.imbalances),
            "trade_size": _summary(state.trade_sizes),
            "venue_age_ns": _summary(state.venue_ages_ns),
            "continuity_limitation": (
                "checksum_only_no_numeric_book_sequence"
                if venue == "kraken"
                else "source_sequence_checked_when_available"
            ),
        }
    return {"schema_version": 1, "quantile_method": "nearest_rank", "venues": venues}


def capture_paths(input_path: Path) -> list[Path]:
    if input_path.is_file():
        return [input_path]
    if not input_path.is_dir():
        raise ValueError(f"input does not exist: {input_path}")
    return sorted(input_path.rglob("*.ndjson"))


def build_report(input_path: Path) -> dict[str, object]:
    paths = capture_paths(input_path)
    if not paths:
        raise ValueError("input contains no NDJSON captures")
    report = analyze_records(read_records(paths))
    report["generated_at_utc"] = datetime.now(UTC).isoformat().replace("+00:00", "Z")
    report["input_files"] = [path.as_posix() for path in paths]
    provenance: list[object] = []
    for path in paths:
        metadata_path = path.with_suffix(".metadata.json")
        if metadata_path.is_file():
            provenance.append(json.loads(metadata_path.read_text(encoding="utf-8")))
    report["input_provenance"] = provenance
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="fvl-data-quality")
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    report = build_report(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
    )
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
