# FairValueLab

## Cross-Venue Fair-Value Forecasting from Limit-Order-Book Events

FairValueLab is a C++20 and Python 3.12 market-microstructure research system for capturing public multi-venue L2 data, replaying it in local receipt-time order, building leakage-safe synchronized datasets, and testing short-horizon fair-value relationships.

The canonical empirical study uses a retained 30-minute public capture of Binance `BTCUSDT` and Kraken `BTC/USD` on 2026-09-14. It contains 211,541 source messages (97,214 Binance; 114,327 Kraken) and 940,999 normalized events. Kraken reconstruction reports zero crossed books, zero valid source messages rejected, 110,910 checksum matches, and zero mismatches. The tested forecast horizons are 10, 50, 100, and 250 ms and 1 second.

## Key finding

Cross-venue Ridge MAE was higher and IC lower than local Ridge at all five horizons.
At 10 ms, MAE was 14.481894 versus 9.620556 ticks; at 1 second, 159.074527 versus
141.223958 ticks. These results apply to one capture and one chronological split.
See [findings](research/findings.md) for the full tables and
[negative results](research/negative_results.md) for failed and inconclusive experiments.

## Architecture

```text
public venue feeds
    -> bounded raw capture + provenance
    -> venue-specific validation and normalization
    -> integer-tick C++ order books
    -> per-venue backward-looking features
    -> receipt-time cross-venue synchronization
    -> clock- or event-sampled research rows
    -> leakage-safe future targets
    -> chronological statistical studies
    -> result tables and figures
```

Code follows that pipeline:

- `src/fairvaluelab/capture/`: bounded public-feed capture and provenance.
- `cpp/src/venue_adapters.cpp`, `capture_converter.cpp`, and `capture_validation.cpp`: source validation and normalization.
- `cpp/src/order_book.cpp`, `feature_emitter.cpp`, and `cross_venue.cpp`: books, trailing features, and synchronization.
- `cpp/src/research_sampler.cpp` and `research_dataset.cpp`: sampling and future targets.
- `src/fairvaluelab/dataset.py` and `baseline.py`: leakage checks, chronological splits, and fixed models.
- `src/fairvaluelab/research_workflow.py`: the ordered capture-to-figures workflow; adjacent study modules own each analysis.

The [methodology](research/methodology.md) specifies receipt-time alignment, target
delays, purging, features, and metrics. Machine-readable results live in
[`research/results/`](research/results/).

## Performance

The committed Release benchmark was measured on an 11th Gen Intel Core i5-1135G7 running Windows 11, using GCC 15.2 from MSYS2. Each path processed 5,000,000 generated events in five timed repetitions after one unrecorded warm-up.

| Path | Mean latency | Mean throughput |
|---|---:|---:|
| Order-book update | 71.16 ns/event | 14.08 million events/s |
| Feature generation and cross-venue synchronization | 1,034.57 ns/event | 1.02 million events/s |

Percentiles were not measured. These results are machine-specific and exclude public-network, exchange, and capture latency. Full metadata and run ranges are in [benchmark_results.json](research/results/benchmark_results.json).

## Build and test

Install [uv](https://docs.astral.sh/uv/), CMake 3.20 or newer, and a C++20 compiler.

```console
uv sync
uv run ruff check .
uv run mypy src
uv run pytest

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, the generated C++ tools have an `.exe` suffix.

## Capture public data

Capture is research-only, unauthenticated, and bounded. Raw files are written beneath the gitignored `data/capture/` directory.

```console
uv run fvl-capture \
  --symbol BTC-USD \
  --venues binance kraken \
  --duration-seconds 600 \
  --max-events 10000 \
  --validate
```

Public endpoint behavior and regional availability can change. Review [research/data_sources.md](research/data_sources.md) before a new capture campaign. The tool does not submit orders or connect to authenticated trading APIs.

## Reproduce the experiments

After building the Release C++ targets and obtaining a capture, one command regenerates data-quality output, clock and event datasets, leakage checks, all statistical studies, benchmarks, and figures:

```console
uv run fvl-research data/capture/<capture-id> \
  --build-directory build \
  --generated-directory data/generated/research \
  --results-directory research/results \
  --figures-directory research/figures
```

Use `--dry-run` to inspect the complete command sequence without writing outputs. Benchmark size can be adjusted with `--benchmark-events` and `--benchmark-repetitions`; changing those values produces a different machine-specific benchmark experiment.

Individual tools remain available for focused work:

```console
uv run fvl-data-quality data/capture/<capture-id> --output research/results/data_quality.json
uv run fvl-research-dataset data/capture/<capture-id> --build-directory build
uv run python -m fairvaluelab.dataset data/generated/research/dataset_staleness_100000000.csv
uv run fvl-cross-venue-study --dataset <dataset.csv> --output <results.csv>
uv run fvl-visualize
```

Large raw captures and generated datasets are intentionally excluded from Git. The repository contains source code, deterministic fixtures, compact provenance and result files, documentation, and reproducible figures.

## Limitations

- The study contains one approximately 30-minute capture and one chronological split; results may differ on other days or under other market conditions.
- Binance `BTCUSDT` and Kraken `BTC/USD` have different quote currencies.
- At the primary 100 ms freshness threshold, only 291 of 35,980 clock rows have both venues valid.
- Live public capture reproduces a procedure, not identical historical events.
- Benchmarks are machine-specific and exclude exchange and network latency.
- The study measures prediction and association, not causality or execution outcomes.
