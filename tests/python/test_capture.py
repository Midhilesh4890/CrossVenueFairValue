import asyncio
import json
import time
from contextlib import AbstractAsyncContextManager
from pathlib import Path
from types import TracebackType

from fairvaluelab.capture.core import (
    AnchoredClock,
    BufferedNdjsonWriter,
    Subscription,
    WebSocketLike,
    capture_subscription,
    frame_record,
    subscription_for,
    summarize_capture,
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
                '{"channel":"l2_data","sequence_num":1,"events":[]}',
                b'{"channel":"market_trades","sequence_num":2,"events":[]}',
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
        second = FakeWebSocket(['{"channel":"market_trades","sequence_num":1,"events":[]}'])
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
    coinbase_channels = {json.loads(message)["channel"] for message in coinbase.messages}
    assert coinbase_channels == {"level2", "market_trades"}

    okx = subscription_for("okx", "BTC-USD")
    okx_channels = {item["channel"] for item in json.loads(okx.messages[0])["args"]}
    assert okx_channels == {"books", "trades"}
