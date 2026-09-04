import asyncio
import json
import time
from collections.abc import Callable
from contextlib import AbstractAsyncContextManager
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Protocol, cast

from websockets.asyncio.client import connect

DEFAULT_VENUES = ("binance", "coinbase", "okx")


class WebSocketLike(Protocol):
    async def send(self, message: str) -> None: ...

    async def recv(self) -> str | bytes: ...


ConnectionFactory = Callable[[str], AbstractAsyncContextManager[WebSocketLike]]


@dataclass(frozen=True)
class Subscription:
    venue: str
    uri: str
    messages: tuple[str, ...]


class AnchoredClock:
    def __init__(
        self,
        wall_anchor_ns: int,
        performance_anchor_ns: int,
        performance_clock: Callable[[], int] = time.perf_counter_ns,
    ) -> None:
        self.wall_anchor_ns = wall_anchor_ns
        self.performance_anchor_ns = performance_anchor_ns
        self.performance_clock = performance_clock

    @classmethod
    def start(cls) -> "AnchoredClock":
        return cls(time.time_ns(), time.perf_counter_ns())

    def now_ns(self) -> int:
        return self.wall_anchor_ns + self.performance_clock() - self.performance_anchor_ns


def frame_record(raw_payload: str, local_receipt_timestamp_ns: int) -> str:
    return (
        json.dumps(
            {
                "local_receipt_timestamp_ns": local_receipt_timestamp_ns,
                "raw_payload": raw_payload,
            },
            ensure_ascii=False,
            separators=(",", ":"),
        )
        + "\n"
    )


def _append_lines(path: Path, lines: list[str]) -> None:
    with path.open("a", encoding="utf-8", newline="") as output:
        output.writelines(lines)


class BufferedNdjsonWriter:
    def __init__(self, path: Path, flush_interval: float = 1.0) -> None:
        if flush_interval <= 0:
            raise ValueError("flush interval must be positive")
        self.path = path
        self.flush_interval = flush_interval
        self.queue: asyncio.Queue[str | None] = asyncio.Queue()
        self.task: asyncio.Task[None] | None = None

    async def start(self) -> None:
        if self.task is not None:
            raise RuntimeError("writer already started")
        self.task = asyncio.create_task(self.run())

    def submit(self, record: str) -> None:
        if self.task is None:
            raise RuntimeError("writer is not running")
        self.queue.put_nowait(record)

    async def close(self) -> None:
        task = self.task
        if task is None:
            return
        self.queue.put_nowait(None)
        await task
        self.task = None

    async def run(self) -> None:
        await asyncio.to_thread(self.path.parent.mkdir, parents=True, exist_ok=True)
        buffer: list[str] = []
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.flush_interval
        while True:
            timeout = max(0.0, deadline - loop.time())
            try:
                record = await asyncio.wait_for(self.queue.get(), timeout)
            except TimeoutError:
                if buffer:
                    await asyncio.to_thread(_append_lines, self.path, buffer)
                    buffer = []
                deadline = loop.time() + self.flush_interval
                continue

            if record is None:
                if buffer:
                    await asyncio.to_thread(_append_lines, self.path, buffer)
                return
            buffer.append(record)
            if loop.time() >= deadline:
                await asyncio.to_thread(_append_lines, self.path, buffer)
                buffer = []
                deadline = loop.time() + self.flush_interval


def split_symbol(symbol: str) -> tuple[str, str]:
    normalized = symbol.strip().upper().replace("/", "-").replace("_", "-")
    if "-" in normalized:
        parts = normalized.split("-")
        if len(parts) == 2 and all(parts):
            return parts[0], parts[1]
    for quote in ("USDT", "USDC", "USD", "EUR"):
        if normalized.endswith(quote) and len(normalized) > len(quote):
            return normalized[: -len(quote)], quote
    raise ValueError(f"unsupported symbol format: {symbol}")


def subscription_for(venue: str, symbol: str) -> Subscription:
    base, quote = split_symbol(symbol)
    venue_name = venue.lower()
    if venue_name == "binance":
        binance_quote = "USDT" if quote == "USD" else quote
        stream_symbol = f"{base}{binance_quote}".lower()
        streams = f"{stream_symbol}@depth@100ms/{stream_symbol}@trade"
        uri = f"wss://stream.binance.com:9443/stream?streams={streams}"
        return Subscription(venue_name, uri, ())
    if venue_name == "coinbase":
        coinbase_quote = "USD" if quote == "USDT" else quote
        product = f"{base}-{coinbase_quote}"
        messages = (
            json.dumps({"type": "subscribe", "product_ids": [product], "channel": "level2"}),
            json.dumps(
                {"type": "subscribe", "product_ids": [product], "channel": "market_trades"}
            ),
        )
        return Subscription(venue_name, "wss://advanced-trade-ws.coinbase.com", messages)
    if venue_name == "okx":
        okx_quote = "USDT" if quote == "USD" else quote
        instrument = f"{base}-{okx_quote}"
        message = json.dumps(
            {
                "op": "subscribe",
                "args": [
                    {"channel": "books", "instId": instrument},
                    {"channel": "trades", "instId": instrument},
                ],
            }
        )
        return Subscription(venue_name, "wss://ws.okx.com:8443/ws/v5/public", (message,))
    raise ValueError(f"unsupported venue: {venue}")


def open_connection(uri: str) -> AbstractAsyncContextManager[WebSocketLike]:
    return cast(AbstractAsyncContextManager[WebSocketLike], connect(uri, max_size=16 * 1024 * 1024))


async def capture_subscription(
    subscription: Subscription,
    writer: BufferedNdjsonWriter,
    clock: AnchoredClock,
    duration_seconds: float,
    connection_factory: ConnectionFactory = open_connection,
) -> None:
    if duration_seconds <= 0:
        raise ValueError("duration must be positive")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + duration_seconds
    async with connection_factory(subscription.uri) as websocket:
        for message in subscription.messages:
            await websocket.send(message)
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                return
            try:
                raw_payload = await asyncio.wait_for(websocket.recv(), remaining)
            except TimeoutError:
                return
            except EOFError:
                return
            receipt_timestamp = clock.now_ns()
            if isinstance(raw_payload, bytes):
                raw_payload = raw_payload.decode("utf-8")
            writer.submit(frame_record(raw_payload, receipt_timestamp))


async def run_capture(
    symbol: str,
    venues: tuple[str, ...],
    duration_seconds: float,
    output_directory: Path,
) -> dict[str, Path]:
    if duration_seconds <= 0:
        raise ValueError("duration must be positive")
    selected_venues = tuple(dict.fromkeys(venue.lower() for venue in venues))
    if not selected_venues:
        raise ValueError("at least one venue is required")

    clock = AnchoredClock.start()
    capture_start = datetime.fromtimestamp(clock.wall_anchor_ns / 1_000_000_000, UTC)
    capture_date = capture_start.date().isoformat()
    paths = {
        venue: output_directory / capture_date / f"{venue}.ndjson" for venue in selected_venues
    }
    writers = {venue: BufferedNdjsonWriter(path) for venue, path in paths.items()}
    for writer in writers.values():
        await writer.start()

    try:
        async with asyncio.TaskGroup() as tasks:
            for venue in selected_venues:
                tasks.create_task(
                    capture_subscription(
                        subscription_for(venue, symbol),
                        writers[venue],
                        clock,
                        duration_seconds,
                    )
                )
    finally:
        await asyncio.gather(*(writer.close() for writer in writers.values()))
    return paths
