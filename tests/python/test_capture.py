import asyncio
import json
import time
from contextlib import AbstractAsyncContextManager
from pathlib import Path
from types import TracebackType

from fairvaluelab.capture.core import (
    AnchoredClock,
    BufferedNdjsonWriter,
    CaptureValidationSummary,
    Subscription,
    WebSocketLike,
    capture_provenance,
    capture_subscription,
    frame_record,
    subscription_for,
    summarize_capture,
    write_capture_provenance,
)


class FakeWebSocket:
    def __init__(self, messages: list[str | bytes], eof_when_empty: bool = False) -> None:
        self.messages = messages
        self.eof_when_empty = eof_when_empty
        self.sent: list[str] = []

    async def send(self, message: str) -> None:
        self.sent.append(message)

    async def recv(self) -> str | bytes:
        if not self.messages:
            if self.eof_when_empty:
                raise EOFError
            await asyncio.sleep(3600.0)
        return self.messages.pop(0)


class FakeConnection(AbstractAsyncContextManager[WebSocketLike]):
    def __init__(self, websocket: FakeWebSocket) -> None:
        self.websocket = websocket

    async def __aenter__(self) -> WebSocketLike:
        return self.websocket

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        return None


class FakeConnectionFactory:
    def __init__(self, websockets: list[FakeWebSocket]) -> None:
        self.websockets = websockets
        self.uris: list[str] = []

    def __call__(self, uri: str) -> AbstractAsyncContextManager[WebSocketLike]:
        self.uris.append(uri)
        if not self.websockets:
            raise ConnectionError("no websocket")
        return FakeConnection(self.websockets.pop(0))


def test_record_framing() -> None:
    line = frame_record('{"price":"100.25"}', 123456789, "trade")
    assert line.endswith("\n")
    assert line.count("\n") == 1
    assert json.loads(line) == {
        "local_receipt_timestamp_ns": 123456789,
        "raw_payload": '{"price":"100.25"}',
        "record_kind": "trade",
    }


def test_buffered_writer_flushes_on_interval(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "capture.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=0.01)
        await writer.start()
        writer.submit(frame_record("first", 1, "other"))
        writer.submit(frame_record("second", 2, "other"))
        await asyncio.sleep(0.05)
        assert len(path.read_text(encoding="utf-8").splitlines()) == 2
        await writer.close()

    asyncio.run(scenario())


def test_capture_uses_fake_websocket_source(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "coinbase.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=60.0)
        websocket = FakeWebSocket(
            [
                '{"type":"l2update","product_id":"BTC-USD","changes":[]}',
                b'{"type":"match","product_id":"BTC-USD","trade_id":2}',
            ]
        )
        factory = FakeConnectionFactory([websocket])
        timestamps = iter((101, 102))
        clock = AnchoredClock(1_000, 100, lambda: next(timestamps))
        subscription = Subscription("coinbase", "wss://example.test", ("subscribe",))

        await writer.start()
        await capture_subscription(subscription, writer, clock, 1.0, factory)
        await writer.close()

        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        assert factory.uris == ["wss://example.test"]
        assert websocket.sent == ["subscribe"]
        assert [record["record_kind"] for record in records] == ["depth_diff", "trade"]
        assert [record["local_receipt_timestamp_ns"] for record in records] == [1_001, 1_002]

    asyncio.run(scenario())


def test_binance_snapshot_bootstrap_discards_stale_buffered_depth(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "binance.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=60.0)
        stale = json.dumps(
            {
                "stream": "btcusdt@depth@100ms",
                "data": {
                    "e": "depthUpdate",
                    "E": 1,
                    "s": "BTCUSDT",
                    "U": 8,
                    "u": 10,
                    "b": [],
                    "a": [],
                },
            },
            separators=(",", ":"),
        )
        current = json.dumps(
            {
                "stream": "btcusdt@depth@100ms",
                "data": {
                    "e": "depthUpdate",
                    "E": 2,
                    "s": "BTCUSDT",
                    "U": 11,
                    "u": 12,
                    "b": [],
                    "a": [],
                },
            },
            separators=(",", ":"),
        )
        trade = json.dumps(
            {
                "stream": "btcusdt@trade",
                "data": {
                    "e": "trade",
                    "E": 3,
                    "s": "BTCUSDT",
                    "t": 1,
                    "p": "1",
                    "q": "1",
                    "T": 3,
                    "m": True,
                },
            },
            separators=(",", ":"),
        )
        websocket = FakeWebSocket([stale, current, trade])
        factory = FakeConnectionFactory([websocket])
        timestamps = iter((101, 102, 103, 104))
        clock = AnchoredClock(1_000, 100, lambda: next(timestamps))
        subscription = Subscription(
            "binance",
            "wss://example.test",
            (),
            "https://example.test/depth",
        )

        def delayed_snapshot(_: str) -> str:
            time.sleep(0.01)
            return '{"lastUpdateId":10,"bids":[],"asks":[]}'

        await writer.start()
        await capture_subscription(
            subscription,
            writer,
            clock,
            1.0,
            factory,
            delayed_snapshot,
        )
        await writer.close()

        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        assert [record["record_kind"] for record in records] == ["snapshot", "depth_diff", "trade"]
        assert records[0]["raw_payload"] == '{"lastUpdateId":10,"bids":[],"asks":[]}'
        assert records[1]["raw_payload"] == current
        assert records[2]["raw_payload"] == trade

    asyncio.run(scenario())


def test_capture_records_reconnect_marker(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "coinbase.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=60.0)
        first = FakeWebSocket([], eof_when_empty=True)
        second = FakeWebSocket(['{"type":"match","product_id":"BTC-USD","trade_id":1}'])
        factory = FakeConnectionFactory([first, second])
        timestamps = iter((101, 102))
        clock = AnchoredClock(1_000, 100, lambda: next(timestamps))
        subscription = Subscription("coinbase", "wss://example.test", ())

        await writer.start()
        await capture_subscription(
            subscription,
            writer,
            clock,
            0.05,
            factory,
            reconnect_initial_delay_seconds=0.001,
            reconnect_max_delay_seconds=0.001,
            max_events=1,
        )
        await writer.close()

        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        assert records[0]["record_kind"] == "reconnect"
        assert records[1]["record_kind"] == "trade"

    asyncio.run(scenario())


def test_capture_validation_summary_reports_kind_counts_and_silence(tmp_path: Path) -> None:
    path = tmp_path / "coinbase.ndjson"
    path.write_text(
        frame_record("{}", 1, "depth_diff")
        + frame_record("{}", 3_000_000_002, "trade")
        + frame_record("{}", 3_000_000_003, "trade"),
        encoding="utf-8",
    )

    summary = summarize_capture({"coinbase": path}, 2.0)["coinbase"]

    assert summary.total_records == 3
    assert summary.records_by_kind == {"depth_diff": 1, "trade": 2}
    assert summary.receipt_timestamp_span_ns == 3_000_000_002
    assert summary.silent_gap_count == 1
    assert summary.max_silent_gap_ns == 3_000_000_001
    assert summary.silence_threshold_ns == 2_000_000_000


def test_default_subscriptions_include_depth_and_trades() -> None:
    binance = subscription_for("binance", "BTC-USD")
    assert "btcusdt@depth@100ms/btcusdt@trade" in binance.uri
    assert binance.snapshot_uri == "https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=5000"

    coinbase = subscription_for("coinbase", "BTC-USD")
    assert coinbase.uri == "wss://ws-feed.exchange.coinbase.com"
    assert json.loads(coinbase.messages[0])["channels"] == ["level2", "matches", "heartbeat"]
    assert coinbase.instrument == "BTC-USD"

    kraken = subscription_for("kraken", "BTC-USD")
    kraken_channels = {json.loads(message)["params"]["channel"] for message in kraken.messages}
    assert kraken_channels == {"book", "trade"}
    assert kraken.instrument == "BTC/USD"

    okx = subscription_for("okx", "BTC-USD")
    okx_channels = {item["channel"] for item in json.loads(okx.messages[0])["args"]}
    assert okx_channels == {"books", "trades"}


def test_capture_stops_at_max_events_and_records_identity(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "kraken.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=60.0)
        websocket = FakeWebSocket(
            [
                '{"channel":"book","type":"snapshot","data":[]}',
                '{"channel":"book","type":"update","data":[]}',
                '{"channel":"trade","type":"update","data":[]}',
            ]
        )
        factory = FakeConnectionFactory([websocket])
        timestamps = iter((101, 102))
        clock = AnchoredClock(1_000, 100, lambda: next(timestamps))
        subscription = subscription_for("kraken", "BTC-USD")

        await writer.start()
        await capture_subscription(
            subscription,
            writer,
            clock,
            1.0,
            factory,
            max_events=2,
        )
        await writer.close()

        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        assert [record["record_kind"] for record in records] == ["snapshot", "depth_diff"]
        assert all(record["venue"] == "kraken" for record in records)
        assert all(record["instrument"] == "BTC/USD" for record in records)
        assert len(websocket.messages) == 1

    asyncio.run(scenario())


def test_capture_provenance_sidecar_is_machine_readable(tmp_path: Path) -> None:
    raw_path = tmp_path / "2026-09-09" / "kraken.ndjson"
    summary = CaptureValidationSummary(
        total_records=12,
        records_by_kind={"snapshot": 1, "depth_diff": 9, "trade": 2},
        receipt_timestamp_span_ns=900,
        silent_gap_count=0,
        max_silent_gap_ns=None,
        silence_threshold_ns=5_000_000_000,
    )
    provenance = capture_provenance(
        subscription_for("kraken", "BTC-USD"),
        raw_path,
        summary,
        1_789_000_000_000_000_000,
        1_789_000_001_000_000_000,
        "abcdef123456",
    )
    metadata_path = raw_path.with_suffix(".metadata.json")

    write_capture_provenance(metadata_path, provenance)

    stored = json.loads(metadata_path.read_text(encoding="utf-8"))
    assert stored["schema_version"] == 1
    assert stored["venue"] == "kraken"
    assert stored["instrument"] == "BTC/USD"
    assert stored["event_count"] == 12
    assert stored["raw_file"].endswith("2026-09-09/kraken.ndjson")
    assert stored["normalized_file"] is None
    assert stored["price_scale"] is None
    assert stored["quantity_scale"] is None
    assert stored["receipt_timestamp_field"] == "local_receipt_timestamp_ns"
    assert stored["sequence_update_fields"] == ["data[].checksum", "data[].trade_id"]
    assert stored["software_revision"] == "abcdef123456"
    assert stored["capture_start_utc"].endswith("Z")
    assert stored["capture_end_utc"].endswith("Z")
