import asyncio
import json
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
)


class FakeWebSocket:
    def __init__(self, messages: list[str | bytes]) -> None:
        self.messages = messages
        self.sent: list[str] = []

    async def send(self, message: str) -> None:
        self.sent.append(message)

    async def recv(self) -> str | bytes:
        if not self.messages:
            raise EOFError
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
    def __init__(self, websocket: FakeWebSocket) -> None:
        self.websocket = websocket
        self.uris: list[str] = []

    def __call__(self, uri: str) -> AbstractAsyncContextManager[WebSocketLike]:
        self.uris.append(uri)
        return FakeConnection(self.websocket)


def test_record_framing() -> None:
    line = frame_record('{"price":"100.25"}', 123456789)
    assert line.endswith("\n")
    assert line.count("\n") == 1
    assert json.loads(line) == {
        "local_receipt_timestamp_ns": 123456789,
        "raw_payload": '{"price":"100.25"}',
    }


def test_buffered_writer_flushes_on_interval(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "capture.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=0.01)
        await writer.start()
        writer.submit(frame_record("first", 1))
        writer.submit(frame_record("second", 2))
        await asyncio.sleep(0.05)
        assert len(path.read_text(encoding="utf-8").splitlines()) == 2
        await writer.close()

    asyncio.run(scenario())


def test_capture_uses_fake_websocket_source(tmp_path: Path) -> None:
    async def scenario() -> None:
        path = tmp_path / "coinbase.ndjson"
        writer = BufferedNdjsonWriter(path, flush_interval=60.0)
        websocket = FakeWebSocket(['{"type":"snapshot"}', b'{"type":"update"}'])
        factory = FakeConnectionFactory(websocket)
        timestamps = iter((101, 102))
        clock = AnchoredClock(1_000, 100, lambda: next(timestamps))
        subscription = Subscription("coinbase", "wss://example.test", ("subscribe",))

        await writer.start()
        await capture_subscription(subscription, writer, clock, 1.0, factory)
        await writer.close()

        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        assert factory.uris == ["wss://example.test"]
        assert websocket.sent == ["subscribe"]
        assert [record["raw_payload"] for record in records] == [
            '{"type":"snapshot"}',
            '{"type":"update"}',
        ]
        assert [record["local_receipt_timestamp_ns"] for record in records] == [1_001, 1_002]

    asyncio.run(scenario())


def test_default_subscriptions_include_depth_and_trades() -> None:
    binance = subscription_for("binance", "BTC-USD")
    assert "btcusdt@depth@100ms/btcusdt@trade" in binance.uri

    coinbase = subscription_for("coinbase", "BTC-USD")
    coinbase_channels = {json.loads(message)["channel"] for message in coinbase.messages}
    assert coinbase_channels == {"level2", "market_trades"}

    okx = subscription_for("okx", "BTC-USD")
    okx_channels = {item["channel"] for item in json.loads(okx.messages[0])["args"]}
    assert okx_channels == {"books", "trades"}
