# Real market-data source assessment

Assessment date: 2026-09-10

## Decision

Use bounded, unauthenticated public WebSocket capture for the first real-data study. Kraken
`BTC/USD` and Binance `BTCUSDT` are the viable two-venue default. Binance's USDT quote creates a
basis that must not be treated as identical to USD. A live Coinbase Exchange probe on 2026-09-10
returned an explicit authentication-required error for `level2`, so Coinbase is not part of the
credential-free L2 default despite the public-channel documentation cited below.

No official, freely downloadable historical spot L2 event archive was verified for all three
venues. Binance's official public archive provides spot trades, aggregate trades, and klines, but
not reconstructable spot L2 updates. Coinbase and Kraken expose current snapshots, recent trades,
and live feeds rather than a documented bulk historical spot L2 archive. A synchronized live
capture is therefore the reproducible common denominator.

Local receipt timestamps remain the synchronization clock. Exchange timestamps are retained for
auditing and within-venue analysis; this assessment makes no assumption that venue clocks are
synchronized.

## Source comparison

| Venue | Instrument | Official source | Data type | Historical availability | Live availability | Book depth | Trades | Exchange timestamp | Sequence/update identifier | Normalization requirements | Known limitations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Coinbase Exchange | `BTC-USD` | [Exchange WebSocket channels](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels), [product book REST endpoint](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-book), [product trades REST endpoint](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-trades) | L2 snapshot and absolute-size price-level updates; matches/trades | Paginated trades are available through REST, but no official bulk historical L2 event archive was verified. The REST book is a current snapshot, not history. | A 2026-09-10 unauthenticated probe received matches and heartbeats but the server rejected `level2` as authentication-required. | The documented `level2` schema begins with the full aggregated book, but it was not credential-free in the live probe; REST level 2 is a current full aggregated snapshot. | Live `matches`; recent paginated REST trades with `trade_id` | L2 update `time` is recorded by the trading engine; trades include `time`. Snapshot examples do not supply a capture-local timestamp. | L2 messages do not document a per-update sequence field. Heartbeats expose feed sequence and last trade ID; the REST book has a snapshot `sequence`; matches carry trade IDs. | Convert decimal price strings to integer ticks using product metadata; preserve absolute level size semantics and maker-side trade convention; add local receipt time on every frame. | Current server behavior conflicts with the cited public-channel page. Do not use authenticated L2 under this project's credential-free policy. Heartbeat sequence is not an L2-update sequence, and REST trades cannot reconstruct historical books. |
| Kraken | `BTC/USD` | [WebSocket v2 book channel](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book), [WebSocket v2 trade channel](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade), [order-book REST endpoint](https://docs.kraken.com/api-reference/market-data/get-order-book), [recent-trades REST endpoint](https://docs.kraken.com/api-reference/market-data/get-recent-trades) | L2 snapshot and absolute-quantity price-level updates; trades | REST supplies a current book and recent trades. No official bulk historical spot L2 event archive was verified. | Public WebSocket v2 `book` and `trade` subscriptions | Configurable 10, 25, 100, 500, or 1,000 levels per side; snapshot and updates include a CRC32 checksum over the top 10 levels. | Live trade events, optionally preceded by a 50-trade snapshot; recent trades through REST | Book snapshot/update and trade records carry RFC3339 `timestamp` fields. | Book v2 exposes a checksum rather than a numeric sequence. Trade `trade_id` is a sequence unique per book. | Convert decimal values deterministically from their JSON representation; apply updates in received order; preserve the checksum and validate the top 10; add local receipt time. | Multiple changes to the same price may occur in one message and must be processed in order. A checksum detects divergence but cannot identify a missing update number. A reconnect requires resnapshot and checksum validation. |
| Binance Spot | `BTCUSDT` | [Spot WebSocket streams](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md), [public archive description](https://github.com/binance/binance-public-data), [WebSocket/REST depth procedure](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#how-to-manage-a-local-order-book-correctly) | L2 REST snapshot plus diff-depth updates; raw or aggregate trades; downloadable trade history | Official daily/monthly spot archives cover trades, aggregate trades, and klines. No reconstructable spot L2 update archive was verified there. | Public JSON streams provide diff depth at 100 ms or 1,000 ms and trades in real time; REST provides the bootstrap snapshot. | REST snapshot up to 5,000 levels per side; diff stream updates changed price levels. | Live raw `trade` and `aggTrade`; official daily/monthly archives for both | Depth `E` is event time and trade messages include event time `E` and trade time `T`; JSON timestamps default to milliseconds, with microsecond output available through the connection time-unit option. | Diff depth supplies first update ID `U` and final update ID `u`; snapshots supply `lastUpdateId`; raw trades supply trade ID `t`. | Map `BTCUSDT` separately from true USD pairs; convert decimal strings to ticks; bootstrap and bridge the REST snapshot to buffered diffs using Binance's documented ID rules; preserve `E`, `T`, `U`, and `u`; add local receipt time. | USDT/USD basis and venue-specific tick sizes affect cross-venue comparisons. The snapshot is capped at 5,000 levels, connections expire after 24 hours, and an update-ID gap requires discarding and rebuilding local state. Historical trades alone cannot reconstruct the book. |

## Capture design implications

The first capture should subscribe independently to each venue's unbatched L2 and trade feeds,
stamp each received frame immediately with a wall-clock time anchored to a monotonic clock, and
write the raw payload unchanged. Each venue needs its own recovery rule:

- Coinbase: exclude it from credential-free L2 capture unless official access behavior changes;
  public matches alone remain useful only as a trade-only auxiliary source.
- Kraken: request a book snapshot, validate every published checksum, and start a new book epoch
  when validation fails or the connection restarts.
- Binance: buffer diff-depth events while fetching the REST snapshot, bridge
  `lastUpdateId` into the first covering `[U, u]` range, and resnapshot after a gap.

Trade and book messages must share the same local receipt-time domain but retain their distinct
venue timestamps and identifiers. Capture records should identify reconnects and book epochs so
normalization never joins updates across an unverified discontinuity.

## Instrument and venue policy

The primary analysis should compare Kraken `BTC/USD` with Binance `BTCUSDT` using the following
safeguards:

- identify quote currency explicitly in every artifact;
- report the contemporaneous cross-venue basis rather than assuming parity;
- avoid interpreting a persistent USD/USDT basis as predictive book information;
- state clearly that the two instruments have different quote currencies.

The initial study should use L2 depth rather than L3 order data. L2 is available publicly and
consistently enough across the selected venues, aligns with the existing aggregated-price-level
book, and avoids introducing order-level semantics that differ by venue.

## Reproducibility boundaries

A live capture can be reproduced as a procedure, not as identical market observations. Every run
therefore needs machine-readable provenance including source URLs, venue instruments, UTC start
and end times, local clock method, software revision, event counts, and raw-file checksums. Raw
captures should remain outside Git; only tiny redacted fixtures and compact metadata belong in the
repository.

Public endpoints, schemas, retention, rate limits, and regional availability can change. The
acquisition tool should fail visibly on subscription errors or unexpected schemas and the source
assessment should be revisited before a major new capture campaign.
