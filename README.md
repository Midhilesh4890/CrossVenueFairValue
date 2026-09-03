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
