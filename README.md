# FairValueLab

FairValueLab is a C++20 and Python 3.12 platform for deterministic market-microstructure
research, cross-venue synchronization, and short-horizon fair-value estimation. It does not
connect to live venues or submit orders.

## Implemented functionality

- Integer-tick normalized book updates and trades, with local receipt and exchange timestamps.
- Venue adapters, capture validation, raw-capture conversion, and deterministic multi-venue replay.
- A fixed-capacity L2 `OrderBook` with accepted, duplicate, stale, gap, and invalid-update handling.
- `FeatureEmitter` event and clock sampling with spread, mid, microprice, L1/L3/L5 imbalance,
  depth, book slope, OFI, multi-level OFI, and signed trade-flow features.
- Fixed-capacity rolling histories and allocation-tested book, feature, and synchronized update
  paths.
- Receipt-time synchronization of the latest valid state for configured venues, including explicit
  freshness, age, consolidated references, cross-market basis, pairwise, and lead-lag features.
- Leakage-safe, multi-horizon fair-value targets and an auditable CSV research-dataset generator.
- Python dataset validation, missing-value and target summaries, chronological splitting, and
  fixed Ridge/logistic-regression baselines.

The committed fixtures are synthetic and deterministic. They are not presented as real market
data.

## Build and test

Install [`uv`](https://docs.astral.sh/uv/) and a C++20 compiler with CMake 3.20 or newer. Python
dependencies are defined by `pyproject.toml` and `uv.lock`:

```console
uv sync
uv run ruff check .
uv run pytest
```

Configure, build, and test C++ in Release mode:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Data flow and tools

The research path is:

```text
normalized events
    -> per-venue order books
    -> per-venue microstructure features
    -> receipt-time-aligned cross-venue state
    -> future fair-value targets
    -> research CSV
```

Raw fixture capture files can be validated and converted to the normalized schema before replay:

```console
./build/fvl_validate_capture data/fixtures/multi_venue
./build/fvl_convert_capture data/fixtures/multi_venue build/normalized.csv
./build/fvl_multi_replay build/normalized.csv --snapshot-interval 1000
./build/fvl_features build/normalized.csv build/features.csv --clock-interval-ns 50000000
```

Generate a clock-sampled cross-venue dataset with configurable staleness, horizons, and maximum
target delay:

```console
./build/fvl_dataset \
  --input data/fixtures/multi_venue_updates.csv \
  --output data/generated/research_dataset.csv \
  --sampling clock \
  --clock-ns 50000000 \
  --max-staleness-ns 100000000 \
  --horizons-ns 10000000,50000000,100000000,250000000,1000000000 \
  --max-target-delay-ns 50000000
```

`--sampling event` is available, but clock sampling is recommended for cross-venue research so a
high-activity venue does not determine the observation count. Add `--discard-missing-targets` to
remove rows lacking any configured horizon; by default they remain with empty target fields.

Validate the output and run the statistical baseline with:

```console
uv run python -m fairvaluelab.dataset data/generated/research_dataset.csv
uv run python -m fairvaluelab.baseline --dataset data/generated/research_dataset.csv
```

On Windows, invoke the corresponding `.exe` files in the selected CMake output directory.

## Synchronization and target semantics

Local receipt time is the only synchronization timeline. At sample time `t`, a venue is usable
when it has been observed, its latest receipt timestamp is not later than `t`, its age
`t - latest_receipt_timestamp` is no greater than `max_staleness_ns`, and its book is valid and
two-sided. Stale or missing inputs stay undefined; they are never replaced with zero. Consolidated
mid and microprice are unweighted means over their valid fresh contributors, and contributor counts
are written with every row. Pairwise fields use the configured `A - B` orientation and require both
venues to be fresh.

For sample time `t` and horizon `h`, the target is the first valid synchronized sample whose
timestamp is at least `t + h`. A sample before the horizon is never selected. The target remains
undefined when none occurs within the inclusive `max_target_delay_ns`. Every row records target
timestamp and delay, and validators enforce:

```text
feature receipt timestamp <= sample timestamp
target timestamp >= sample timestamp + horizon
target delay = target timestamp - (sample timestamp + horizon)
```

Feature histories are backward-looking. Target alignment runs only after current samples have been
materialized and cannot mutate their features.

## Baseline methodology

The baseline compares one primary venue's microprice deviation, its local microstructure features,
and local plus cross-venue features. Ridge regression reports MAE, R-squared, and Pearson
correlation. Logistic regression reports accuracy, balanced accuracy, and binary up/down ROC AUC
when both classes are present. These fixed models are intended as dataset checks, not performance
claims.

Splits use nominal chronological row boundaries of 70% training, 15% validation, and 15% test,
adjusted so equal sample timestamps are never divided. Training rows are purged when any configured
target reaches the validation boundary; validation rows are purged when any target reaches the test
boundary. The final fixed-model fit uses the remaining training and validation rows; metrics are
reported only on the later test partition. No random split is used.

## Allocation and performance notes

`OrderBook`, `FeatureEmitter`, and `CrossVenueSynchronizer` use bounded, preallocated storage in
their steady-state update paths. Caller-owned output spans/vectors are used where applicable.
Construction, returning convenience overloads, research-row materialization, target alignment, CSV
parsing, and serialization may allocate. Dedicated tests assert zero allocations for the paths that
carry that guarantee.

Run deterministic Release benchmarks with inputs generated before timing:

```console
./build/fvl_order_book_benchmark --events 1000000
./build/fvl_cross_venue_benchmark --events 1000000
```

Both report elapsed time, throughput, average nanoseconds per event, and checksums. Results depend
on compiler, hardware, build configuration, and system load and should be measured locally.

## Design boundaries

Prices use signed 64-bit integer ticks; quantities, sequence numbers, and timestamps use unsigned
64-bit integers. A full fixed-depth book discards an out-of-range price or evicts its current worst
level. Sequence gaps do not mutate the book or advance its sequence. Exchange timestamps are kept
for auditing and optional lead-lag fields but are not treated as comparable across venues.
