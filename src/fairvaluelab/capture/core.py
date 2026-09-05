import asyncio
import json
import time
import urllib.request
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
SnapshotFetcher = Callable[[str], str]


@dataclass(frozen=True)
class Subscription:
    venue: str
    uri: str
    messages: tuple[str, ...]
    snapshot_uri: str | None = None


@dataclass(frozen=True)
class ReceivedMessage:
    raw_payload: str
    local_receipt_timestamp_ns: int


@dataclass(frozen=True)
class CaptureValidationSummary:
    total_records: int
    records_by_kind: dict[str, int]
    receipt_timestamp_span_ns: int | None
    silent_gap_count: int
    max_silent_gap_ns: int | None
    silence_threshold_ns: int


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


def frame_record(raw_payload: str, local_receipt_timestamp_ns: int, record_kind: str) -> str:
    return (
        json.dumps(
            {
                "local_receipt_timestamp_ns": local_receipt_timestamp_ns,
                "raw_payload": raw_payload,
                "record_kind": record_kind,
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
        rest_symbol = f"{base}{binance_quote}".upper()
        streams = f"{stream_symbol}@depth@100ms/{stream_symbol}@trade"
        uri = f"wss://stream.binance.com:9443/stream?streams={streams}"
        snapshot_uri = f"https://api.binance.com/api/v3/depth?symbol={rest_symbol}&limit=5000"
        return Subscription(venue_name, uri, (), snapshot_uri)
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


def fetch_text(uri: str) -> str:
    with urllib.request.urlopen(uri, timeout=10.0) as response:
        body = response.read()
    if not isinstance(body, bytes):
        raise TypeError("expected bytes response body")
    return body.decode("utf-8")


def _decode_raw_payload(raw_payload: str | bytes) -> str:
    if isinstance(raw_payload, bytes):
        return raw_payload.decode("utf-8")
    return raw_payload


def _json_payload(raw_payload: str) -> object:
    payload = json.loads(raw_payload)
    if isinstance(payload, dict) and isinstance(payload.get("data"), dict):
        return payload["data"]
    return payload


def classify_record_kind(venue: str, raw_payload: str) -> str:
    try:
        payload = _json_payload(raw_payload)
    except json.JSONDecodeError:
        return "other"
    if not isinstance(payload, dict):
        return "other"
    venue_name = venue.lower()
    if venue_name == "binance":
        event_type = payload.get("e")
        if event_type == "depthUpdate":
            return "depth_diff"
        if event_type == "trade":
            return "trade"
        if "lastUpdateId" in payload and "bids" in payload and "asks" in payload:
            return "snapshot"
    if venue_name == "coinbase":
        channel = payload.get("channel")
        if channel == "l2_data":
            return "depth_diff"
        if channel == "market_trades":
            return "trade"
    if venue_name == "okx":
        arg = payload.get("arg")
        channel = arg.get("channel") if isinstance(arg, dict) else None
        if channel == "books":
            return "depth_diff"
        if channel == "trades":
            return "trade"
    return "other"


def _binance_update_id(payload: object, key: str) -> int | None:
    if isinstance(payload, dict):
        value = payload.get(key)
        if isinstance(value, int):
            return value
    return None


def _binance_last_update_id(raw_payload: str) -> int:
    payload = _json_payload(raw_payload)
    if isinstance(payload, dict):
        value = payload.get("lastUpdateId")
        if isinstance(value, int):
            return value
    raise ValueError("Binance snapshot missing lastUpdateId")


def _binance_depth_range(raw_payload: str) -> tuple[int, int] | None:
    try:
        payload = _json_payload(raw_payload)
    except json.JSONDecodeError:
        return None
    if not isinstance(payload, dict) or payload.get("e") != "depthUpdate":
        return None
    first_update = _binance_update_id(payload, "U")
    final_update = _binance_update_id(payload, "u")
    if first_update is None or final_update is None:
        return None
    return first_update, final_update


def _submit_received(writer: BufferedNdjsonWriter, venue: str, message: ReceivedMessage) -> None:
    writer.submit(
        frame_record(
            message.raw_payload,
            message.local_receipt_timestamp_ns,
            classify_record_kind(venue, message.raw_payload),
        )
    )


async def _receive_message(websocket: WebSocketLike, clock: AnchoredClock) -> ReceivedMessage:
    raw_payload = await websocket.recv()
    return ReceivedMessage(_decode_raw_payload(raw_payload), clock.now_ns())


async def _capture_binance_snapshot(
    subscription: Subscription,
    websocket: WebSocketLike,
    writer: BufferedNdjsonWriter,
    clock: AnchoredClock,
    deadline: float,
    snapshot_fetcher: SnapshotFetcher,
) -> None:
    if subscription.snapshot_uri is None:
        return
    loop = asyncio.get_running_loop()
    buffered: list[ReceivedMessage] = []
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            return
        snapshot_task = asyncio.create_task(
            asyncio.to_thread(snapshot_fetcher, subscription.snapshot_uri)
        )
        receive_task = asyncio.create_task(_receive_message(websocket, clock))
        try:
            while not snapshot_task.done():
                remaining = deadline - loop.time()
                if remaining <= 0:
                    snapshot_task.cancel()
                    receive_task.cancel()
                    return
                done, pending = await asyncio.wait(
                    {snapshot_task, receive_task},
                    timeout=remaining,
                    return_when=asyncio.FIRST_COMPLETED,
                )
                if not done:
                    snapshot_task.cancel()
                    receive_task.cancel()
                    return
                if receive_task in done:
                    buffered.append(receive_task.result())
                    receive_task = asyncio.create_task(_receive_message(websocket, clock))
                if snapshot_task in done:
                    break
            snapshot_payload = snapshot_task.result()
        finally:
            if not receive_task.done():
                receive_task.cancel()
        snapshot_id = _binance_last_update_id(snapshot_payload)
        depth_messages = [
            message
            for message in buffered
            if (depth_range := _binance_depth_range(message.raw_payload)) is not None
            and depth_range[1] > snapshot_id
        ]
        if depth_messages:
            first_depth = _binance_depth_range(depth_messages[0].raw_payload)
            if first_depth is not None and not (
                first_depth[0] <= snapshot_id + 1 <= first_depth[1]
            ):
                continue
        writer.submit(frame_record(snapshot_payload, clock.now_ns(), "snapshot"))
        for message in buffered:
            depth_range = _binance_depth_range(message.raw_payload)
            if depth_range is not None and depth_range[1] <= snapshot_id:
                continue
            _submit_received(writer, subscription.venue, message)
        return


async def _capture_connected(
    subscription: Subscription,
    websocket: WebSocketLike,
    writer: BufferedNdjsonWriter,
    clock: AnchoredClock,
    deadline: float,
    snapshot_fetcher: SnapshotFetcher,
) -> None:
    for subscription_message in subscription.messages:
        await websocket.send(subscription_message)
    if subscription.venue == "binance":
        await _capture_binance_snapshot(
            subscription, websocket, writer, clock, deadline, snapshot_fetcher
        )
    loop = asyncio.get_running_loop()
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            return
        try:
            received = await asyncio.wait_for(_receive_message(websocket, clock), remaining)
        except TimeoutError:
            return
        _submit_received(writer, subscription.venue, received)


async def capture_subscription(
    subscription: Subscription,
    writer: BufferedNdjsonWriter,
    clock: AnchoredClock,
    duration_seconds: float,
    connection_factory: ConnectionFactory = open_connection,
    snapshot_fetcher: SnapshotFetcher = fetch_text,
    reconnect_initial_delay_seconds: float = 0.25,
    reconnect_max_delay_seconds: float = 5.0,
) -> None:
    if duration_seconds <= 0:
        raise ValueError("duration must be positive")
    if reconnect_initial_delay_seconds <= 0 or reconnect_max_delay_seconds <= 0:
        raise ValueError("reconnect delays must be positive")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + duration_seconds
    backoff = reconnect_initial_delay_seconds
    attempted = False
    while loop.time() < deadline:
        if attempted:
            writer.submit(frame_record("{}", clock.now_ns(), "reconnect"))
            await asyncio.sleep(min(backoff, max(0.0, deadline - loop.time())))
            backoff = min(reconnect_max_delay_seconds, backoff * 2.0)
        attempted = True
        try:
            async with connection_factory(subscription.uri) as websocket:
                await _capture_connected(
                    subscription, websocket, writer, clock, deadline, snapshot_fetcher
                )
                return
        except (ConnectionError, EOFError, OSError, TimeoutError):
            continue


def summarize_capture(
    paths: dict[str, Path],
    silence_threshold_seconds: float,
) -> dict[str, CaptureValidationSummary]:
    if silence_threshold_seconds <= 0:
        raise ValueError("silence threshold must be positive")
    silence_threshold_ns = int(silence_threshold_seconds * 1_000_000_000)
    summaries: dict[str, CaptureValidationSummary] = {}
    for venue, path in paths.items():
        records_by_kind: dict[str, int] = {}
        timestamps: list[int] = []
        if path.exists():
            for line in path.read_text(encoding="utf-8").splitlines():
                if not line:
                    continue
                record = json.loads(line)
                kind = record.get("record_kind", "unknown")
                records_by_kind[str(kind)] = records_by_kind.get(str(kind), 0) + 1
                timestamp = record.get("local_receipt_timestamp_ns")
                if isinstance(timestamp, int):
                    timestamps.append(timestamp)
        timestamps.sort()
        gaps = [right - left for left, right in zip(timestamps, timestamps[1:], strict=False)]
        silent_gaps = [gap for gap in gaps if gap > silence_threshold_ns]
        span = timestamps[-1] - timestamps[0] if timestamps else None
        summaries[venue] = CaptureValidationSummary(
            total_records=sum(records_by_kind.values()),
            records_by_kind=records_by_kind,
            receipt_timestamp_span_ns=span,
            silent_gap_count=len(silent_gaps),
            max_silent_gap_ns=max(silent_gaps) if silent_gaps else None,
            silence_threshold_ns=silence_threshold_ns,
        )
    return summaries


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
