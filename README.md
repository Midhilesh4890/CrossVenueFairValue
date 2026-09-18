# FairValueLab

## Cross-Venue Fair-Value Forecasting from Limit-Order-Book Events

FairValueLab is a C++20 and Python 3.12 market-microstructure research system for capturing public multi-venue L2 data, replaying it in local receipt-time order, building leakage-safe synchronized datasets, and testing short-horizon fair-value relationships.

The canonical empirical study uses a retained 30-minute public capture of Binance `BTCUSDT` and Kraken `BTC/USD` on 2026-09-14. It contains 211,541 source messages (97,214 Binance; 114,327 Kraken) and 940,999 normalized events. Kraken reconstruction reports zero crossed books, zero valid source messages rejected, 110,910 checksum matches, and zero mismatches. The tested forecast horizons are 10, 50, 100, and 250 ms and 1 second.

## Key finding

The corrected chronological test has meaningful target variation, yet the 44-feature local-plus-cross-venue Ridge model has higher MAE and lower information coefficient (IC) than the 13-feature local model at all five horizons. This capture and feature design do not establish incremental predictive value from the current cross-venue features. Binance and Kraken use different quote currencies; two-venue coverage is sparse at tight freshness thresholds. The C++ Release benchmark averages 71.16 ns per order-book update and 1,034.57 ns per feature and synchronization event on the documented test machine.

See the [findings](research/findings.md), [negative results](research/negative_results.md), and [methodology](research/methodology.md).

![Local and cross-venue Ridge MAE across horizons](research/figures/cross_venue_mae.png)

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

The core provides:

- fixed-capacity L2 books with explicit accepted, duplicate, stale, gap, and invalid-update handling;
- public Binance and Kraken capture with duration and event bounds, local receipt timestamps, raw payload preservation, and JSON provenance;
- venue-aware normalization into integer ticks and scaled integer quantities;
- deterministic multi-venue replay ordered by local receipt time;
- event and clock sampling with spread, depth, microprice, L1/L3/L5 imbalance, OFI, multi-level OFI, trade flow, freshness, basis, pairwise, and lead-lag fields;
- consolidated midpoint and microprice references built only from valid fresh venues;
- configurable multi-horizon targets and validation in both C++ and Python;
- fixed Ridge and logistic baselines, cross-venue comparison, block-bootstrap uncertainty, reference comparison, ablation, staleness, offline latency, and regime studies;
- machine-specific C++ benchmarks and reproducible figures.

## Research methodology

Local receipt time is the synchronization timeline. Exchange timestamps are retained for auditing and within-venue fields but are not assumed to be synchronized across venues. At sample time `t`, a venue contributes only when its latest valid two-sided state is no later than `t` and no older than the configured freshness threshold. Missing or stale values remain undefined rather than being replaced with zero.

For horizon `h`, the target is the first valid synchronized observation at or after `t + h`, subject to a maximum observation delay. Validators enforce:

```text
feature receipt timestamp <= sample timestamp
target timestamp >= sample timestamp + horizon
target delay = target timestamp - (sample timestamp + horizon)
```

Fitted studies use ordered 70% train, 15% validation, and 15% test partitions. Equal timestamps are not split. Training and validation rows are purged when their future targets cross the next partition boundary. Regime thresholds come only from the purged training partition. No random time-series split is used.

The source assessment, exact normalization rules, feature definitions, target alignment, metrics, uncertainty method, and experiment limitations are documented in [research/methodology.md](research/methodology.md). Committed machine-readable outputs live under [`research/results/`](research/results/).

## Measured results

The main Ridge comparison uses 13 local features and 44 local-plus-cross-venue features:

| Horizon | Local MAE | Cross-venue MAE | Local IC | Cross-venue IC | Test target std. dev. |
|---:|---:|---:|---:|---:|---:|
| 10 ms | 9.620556 | 14.481894 | 0.145772 | 0.101371 | 43.783893 |
| 50 ms | 10.322595 | 15.092695 | 0.151519 | 0.110845 | 46.510994 |
| 100 ms | 19.454271 | 25.809284 | 0.201919 | 0.171502 | 65.924925 |
| 250 ms | 43.238553 | 49.700837 | 0.247502 | 0.215845 | 109.856521 |
| 1 s | 141.223958 | 159.074527 | 0.307063 | 0.272493 | 262.905028 |

MAE is in normalized price ticks. The committed [cross-venue results](research/results/cross_venue_results.csv) include paired time-block bootstrap intervals and directional fields.

![Cross-venue coverage versus freshness threshold](research/figures/staleness_coverage.png)

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

- The study contains one approximately 30-minute capture and one chronological split; it does not establish behavior across other days or market conditions.
- Binance `BTCUSDT` and Kraken `BTC/USD` have different quote currencies.
- At the primary 100 ms freshness threshold, only 291 of 35,980 clock rows have both venues valid.
- Live public capture reproduces a procedure, not identical historical events.
- Benchmarks are machine-specific and exclude exchange and network latency.
- The study measures prediction and association, not causality or execution outcomes.
