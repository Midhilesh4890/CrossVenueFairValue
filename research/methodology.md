# Research Methodology

## Scope and research question

FairValueLab studies whether synchronized state from another public spot venue improves short-horizon fair-value prediction beyond local limit-order-book information. The current empirical sample compares Binance `BTCUSDT` with Kraken `BTC/USD`. The instruments are economically related but their quote currencies are not identical, so cross-venue basis features may contain USD/USDT effects as well as market-microstructure information.

The project measures predictive association, not causality, trading profitability, or executable alpha. No orders are submitted and no authenticated trading endpoints are used.

## Data sources and capture

The source selection rationale and official endpoints are recorded in [data_sources.md](data_sources.md). No common, official, freely downloadable historical spot-L2 event archive was verified for the selected venues, so the study uses bounded live public capture.

`fvl-capture` connects to the public Binance and Kraken book and trade feeds without credentials. A run is bounded by `--duration-seconds`, `--max-events`, or both. Each raw record preserves the venue, instrument, event kind, raw payload, venue timestamps, and venue-specific identifiers when supplied. The receiver timestamps each frame immediately in a common local receipt-time domain. Receipt time is anchored to a wall-clock value using a monotonic clock for elapsed time, avoiding wall-clock adjustments during a capture.

Binance reconstruction starts from a REST depth snapshot and bridges buffered diff-depth messages according to the published `[U, u]` update-ID rules. An update gap invalidates the epoch and requires another snapshot. Kraken supplies absolute quantities and a top-ten checksum rather than a numeric book sequence; updates remain in received order and checksum failure invalidates the epoch. These sequence models are kept distinct.

Raw captures are stored under the gitignored `data/capture/` directory. Per-venue JSON metadata records source URLs, capture boundaries, event counts, schema fields, file paths, and the software revision. A live procedure is reproducible, but the market observations themselves are not repeatable.

## Normalization and tick conversion

The C++ `fvl_convert_capture` tool converts capture records into the existing normalized-event schema. It does not introduce a separate research representation. Normalized records contain venue and instrument identifiers, event type, side, integer `price_ticks`, scaled integer quantity, exchange timestamp, local receipt timestamp, and sequence or trade identifier where the source provides one.

Decimal price and quantity strings are parsed deterministically into scaled integers. Invalid syntax, excessive precision, overflow, nonpositive prices, and invalid quantities fail validation rather than being silently rounded into book state. Venue configuration defines the price and quantity scale. Floating-point prices are not used in the core order book.

Book updates preserve venue semantics: Binance diff quantities replace the aggregate size at a price and zero deletes the level; Kraken absolute quantities behave likewise. Trades are normalized separately from book mutations. Exchange timestamps are retained for audit and within-venue fields, but they are not used to synchronize venues.

## Data-quality diagnostics

`fvl-data-quality` analyzes raw capture files and provenance before modeling. Per venue it reports event, book-update, and trade counts; sequence gaps or checksum limitations; duplicates where identifiable; stale or invalid events; crossed and locked books; missing timestamps; timestamp regressions; capture duration; and nearest-rank inter-arrival quantiles. It also summarizes spread, visible depth, imbalance, trade size, and venue age.

Null values are used when a source does not expose enough information to measure a diagnostic. In particular, Kraken's checksum cannot be reported as a numeric sequence-gap count, and the absence of a value is not interpreted as zero.

## Replay, synchronization, and sampling

Normalized events are replayed in nondecreasing local receipt-time order through the C++ multi-venue engine. Out-of-order receipt timestamps and invalid venue sequences are rejected without mutating accepted state. Each venue maintains its own book, rolling flow features, and latest receipt and exchange timestamps.

Cross-venue synchronization uses local receipt time. At sample time `t`, a venue is fresh only when it has valid state and its latest accepted receipt timestamp is no later than `t` and no older than the configured staleness threshold. Consolidated midpoint and microprice references use only valid, fresh venues. Pairwise features are emitted only from contemporaneously eligible states; freshness indicators and venue ages remain explicit columns.

The primary research datasets use fixed clock sampling at 50 ms. Clock rows make venue comparisons regular in receipt time even when message rates differ. Event sampling is generated separately for offline latency sensitivity, where a delayed decision must be aligned to the latest state observable by that decision time.

## Feature construction

Per-venue features include spread, L1/L3/L5 imbalance, visible bid and ask depth, event- and time-window OFI, multi-level OFI, signed trade volume, last midpoint move, venue age, and deviation from consolidated references. Cross-venue columns include per-venue state, pairwise midpoint and microprice differences, imbalance and flow differences, receipt- and exchange-timestamp differences, and last-move differences.

All model inputs are current or backward-looking at the sample timestamp. Rolling features are trailing windows. No future target participates in feature construction.

The three baseline groups are:

- `microprice_deviation`: the primary venue's microprice deviation from the consolidated microprice;
- `local_microstructure`: local spread, depth, imbalance, OFI, trade-flow, and last-move fields;
- `local_plus_cross_venue`: local features plus available per-venue and pairwise cross-market fields.

Columns that are entirely missing in development data are excluded. Remaining missing values are median-imputed from fitted development data and standardized before modeling.

## Future targets and leakage protection

For each sample timestamp `t` and configured horizon `h`, target alignment selects the first later sample with a valid consolidated reference at or after `t + h`. The target delay is the selected target timestamp minus `t + h`. A target is left undefined when no valid observation exists within the configured maximum target delay. Midpoint and microprice returns are future reference minus current reference, in ticks; direction is the sign of that return.

The primary horizons are 10 ms, 50 ms, 100 ms, 250 ms, and 1 second. Dataset validation enforces:

- nondecreasing, integral sample timestamps;
- every venue receipt timestamp is no later than its row's sample timestamp;
- every target timestamp is at least the sample timestamp plus its horizon;
- every recorded target delay exactly matches that timestamp difference;
- an undefined target has no defined delay.

These invariants are checked in C++ during dataset production and again when Python loads a dataset.

## Chronological splitting and purging

Fitted studies use chronological partitions: 70% train, 15% validation, and 15% test by ordered row position. Equal sample timestamps are not divided across partitions. Before fitting, training rows whose targets reach the validation boundary are purged, and validation rows whose targets reach the test boundary are purged. The train and validation partitions are then combined as development data for the fixed final model. The test partition is never used to choose features, thresholds, or hyperparameters.

The baseline regression is Ridge with fixed `alpha=1`. Direction classification uses logistic regression with a fixed maximum of 1,000 iterations and random state zero. The project does not conduct broad hyperparameter searches.

## Evaluation metrics

Regression outputs include mean absolute error in ticks, R², and Pearson correlation between prediction and realized return, labeled IC. IC is undefined when either series has zero variance or fewer than two observations.

Directional outputs include accuracy, macro recall reported as balanced accuracy, and ROC AUC on nonzero up/down observations. Balanced accuracy and AUC are left undefined when the required outcome classes are absent. Results retain row counts and target variability so degenerate metrics are visible.

The cross-venue comparison reports local and local-plus-cross-venue MAE and IC and their deltas. Its uncertainty calculation is a paired moving-through-time block resample of contiguous row blocks, not an IID row bootstrap. The current configuration uses blocks of ten rows, 2,000 replicates, and a fixed seed. An improvement is supported only when target variation is nonzero and the 95% interval excludes no improvement for MAE or IC.

## Reference and feature-ablation studies

The midpoint-versus-microprice study is a parameter-free full-capture comparison. Each current venue or consolidated reference is expressed as a deviation from the current target anchor, then compared with the future reference return. Because it has no fitted parameters, it does not use the model split, but it still uses only contemporaneous predictors and aligned future targets.

Feature ablation holds Ridge and its hyperparameter fixed while feature groups are added cumulatively: top of book, depth, multi-level imbalance, OFI, multi-level OFI, trade flow, cross-venue basis, pairwise fields, and lead-lag fields. Marginal metrics compare each cumulative set with the immediately preceding set.

## Lead-lag methodology

Lead-lag analysis requires a fixed clock-sampled dataset. For each ordered venue pair and lag, it correlates a source series at time `t` with the response series at `t + lag`. Evaluated lags are 10, 25, 50, 100, 250, and 500 ms when they are exact multiples of the dataset interval. Signals include midpoint change, microprice change, OFI, multi-level OFI, and signed trade flow; flow signals are compared with later midpoint changes. Both source and future response venue states must be fresh. Results are associations and do not imply causality.

## Staleness sensitivity

Separate datasets are generated from the same normalized events using predeclared venue-freshness thresholds of 25, 50, 100, 250, and 500 ms. The study reports target coverage, valid-venue counts, the fraction of rows with all venues valid, and fixed cross-venue model metrics. Fewer than ten test observations and fewer than two unique test targets are marked explicitly instead of being treated as completed estimates.

## Offline latency sensitivity

Offline latency sensitivity uses event-sampled data and predeclared additional decision delays of 0, 5, 10, 25, 50, 100, 250, and 500 microseconds and 1 and 5 milliseconds. For a sample at `t`, the decision state is the latest valid state observable no later than `t + delay`; the outcome is the first valid reference at or after the delayed decision time plus the forecast horizon. Both state age and target observation delay are bounded. This is an offline alignment experiment, not a claim about exchange colocation or live end-to-end latency.

System benchmark latency is measured separately in a Release C++ build. Python/scikit-learn single-row prediction latency is measured after warm-up using repeated `perf_counter_ns` timing trials and reported as the median trial average. The two measurements retain explicit runtime and scope labels and are machine-specific.

## Market regimes

Regime analysis evaluates spread, trailing 20-sample midpoint-change volatility, total visible depth, absolute signed trade-volume-window activity, absolute L1 imbalance, and venue age. Every low/high threshold is the median calculated from the purged training partition only. Current and trailing features define regimes; future targets do not. Empty test bands are omitted. The same fitted local and local-plus-cross-venue Ridge models are compared within each populated band.

## Reproducibility and limitations

Compact provenance, dataset metadata, result tables, benchmark metadata, and generated figures are committed under `research/`. Raw captures and generated datasets remain outside Git because of size and because live observations cannot be recreated exactly.

The current committed capture is a bounded sample rather than a representative market history. It contains different quote currencies, unequal venue message and trade counts, a short observation window, and constant targets in the purged model test partitions. These facts make the present predictive results inconclusive. They are documented in [negative_results.md](negative_results.md) and must remain visible when reporting the study.
