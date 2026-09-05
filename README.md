# FairValueLab

**Low-Latency Cross-Venue Fair-Value Research & Execution System**

FairValueLab is an open-source research and engineering platform for studying limit-order-book
dynamics, short-horizon fair-value estimation, cross-venue signals, latency-aware inference, and
execution and queue dynamics.

## Current functionality

- Normalized integer-tick market events and integer quantities.
- A fixed-capacity, allocation-free-on-update L2 order book with sorted contiguous storage.
- Explicit handling of accepted, duplicate, stale, and sequence-gap updates.
- Spread, mid-price, microprice, and top-of-book depth-imbalance calculations.
- Deterministic C++ tests, normalized CSV replay, and a synthetic update benchmark.
- A minimal Python 3.12 package managed with `uv`.

Future work will build on these primitives to investigate cross-venue normalization, fair-value
models, latency-aware inference, and execution simulation. Those components are not implemented
yet. The project does not currently connect to live venues or submit orders.

## Local setup

Install [`uv`](https://docs.astral.sh/uv/) and a C++20 compiler with CMake 3.20 or newer. From the
repository root, prepare Python with:

```console
uv python pin 3.12
uv sync
uv run python -c "import fairvaluelab"
uv run ruff check .
uv run pytest
```

Configure and build the C++ targets in Release mode:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Run the C++ test suite:

```console
ctest --test-dir build -C Release --output-on-failure
```

On Linux and single-configuration generators, run the tools with:

```console
./build/fvl_replay data/fixtures/order_book_updates.csv
./build/fvl_order_book_benchmark --events 1000000
```

With a Visual Studio generator on Windows, use:

```console
.\build\Release\fvl_replay.exe data\fixtures\order_book_updates.csv
.\build\Release\fvl_order_book_benchmark.exe --events 1000000
```

The benchmark generates all deterministic synthetic events before entering the timed section.
Results depend on the compiler, build configuration, hardware, and system load, so measurements
should be collected locally rather than treated as portable claims.

## Design notes

Exchange prices are normalized to signed 64-bit integer ticks before entering the book. Quantities,
timestamps, and sequence numbers use unsigned 64-bit integers. Each side retains the best configured
number of levels in a fixed `std::array`; a full book discards an out-of-range price or evicts its
current worst level. A gap never mutates the book or advances its sequence, leaving recovery policy
to the future venue adapter.

## Feature emitter allocation behavior

The output-parameter overloads `process(update, output)` and `process(trade, output)` append
to caller-owned storage. Reserve enough space for the existing output, every clock sample
that the next input will trigger across all venues, and its event sample. `output.clear()`
allows that capacity to be reused. The book overload returns the actual `ApplyResult`.

Construction reserves `FeatureEmitterConfig::venue_capacity` venue slots (default 64).
Each slot has fixed arrays for book snapshots and order/trade flow. Each history ring uses
`event_window` entries, up to `FeatureEmitter::maximum_event_window` (4,096). A full ring
overwrites its oldest entry and increments its cumulative dropped-entry counter. Event
windows contain the retained entries; time windows filter those entries by timestamp.
`dropped_entries(venue_id)` exposes both counters without allocating, and `fvl_features`
prints them per venue alongside its feature-row count. `--venue-capacity N` sets the number
of venue slots; exceeding it raises an error instead of growing storage during processing.

The allocation test measures accepted book updates with strictly increasing sequences,
first-venue creation, event-only output, multi-venue clock batches, and accepted trades.
It checks actual accepted-update counts against inputs fed, and asserts zero allocations
and zero deallocations at history capacities 1, 100, and 4,096. Rejected updates have a
separate named allocation test. Clock samples are sorted in place within the newly appended
output range.

Construction allocates the reserved venue storage. The returning `process` overloads and
the all-venue `dropped_entries()` report construct vectors; callers choose the output-parameter
overloads and per-venue accessor for allocation-free processing. Output-vector growth allocates
when the caller has not reserved enough capacity. CSV parsing, formatting, and reporting run
outside the measured processing path.

## Multi-level order flow imbalance

`FeatureEmitterConfig::band_ticks` and `fvl_features --band-ticks N` select a positive
price-band radius in ticks. Each accepted book update uses the pre-update midpoint,
rounded to the nearest tick with half ticks rounded away from zero. Previous and current
quantities at each price within that same inclusive band are compared using weight
`1 / (1 + distance)`. Bid increases are positive and ask increases are negative.
Unchanged prices contribute zero regardless of their rank or band membership.

An event has no multi-level OFI when the pre-update book has an empty side or the union
contains no in-band levels. Event and time windows sum defined contributions and remain
absent when they contain none. Absent values are written as empty CSV fields.
