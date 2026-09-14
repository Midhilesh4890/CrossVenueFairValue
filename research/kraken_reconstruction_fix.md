# Kraken Reconstruction Fix

This note documents the deterministic repair pass for the retained
`20260914T122148.319244Z` capture. No new market capture was run.

## Root Cause

Two Kraken-specific reconstruction defects invalidated the expanded-capture attempt:

1. The Python data-quality replay treated Kraken book updates as unbounded absolute
   level updates. Kraken v2 book messages are for the subscribed depth, so levels
   outside the top 10 must be dropped after each snapshot or update entry. The
   first retained-capture top-10 divergence appears at `kraken.ndjson` line 73,
   book event 68. The current replay kept ask `77852.1`; a depth-limited replay
   had ask `77852.8` in the tenth slot. The first visible crossed book appeared
   later at line 921, where the broken replay produced `77853.5/77852.7` while
   the depth-limited replay produced `77853.5/77853.6`.
2. The C++ Kraken adapter parsed numeric JSON prices and quantities through
   `nlohmann::json::dump()`. For valid raw quantities such as `0.04145246`, the
   dump string became `0.041452459999999997`, which failed exact integer scaling.
   The previous expanded attempt rejected 141 valid Kraken source messages.

## Regression Fixtures

The deterministic fixtures are:

- `data/fixtures/kraken_depth_truncation.ndjson`: a three-message depth-10 replay
  where an ask level is pushed outside the subscribed depth and later exposed if
  stale levels are retained.
- `data/fixtures/kraken_numeric_quantity.ndjson`: a minimal Kraken snapshot with
  numeric JSON quantities that previously failed exact scaling.

Before production code was modified:

- `test_kraken_book_quality_truncates_to_subscribed_depth` failed with
  `visible_depth.max == 21.0`, expected `20.0`.
- `test_kraken_numeric_quantities_are_exact` failed because
  `KrakenAdapter::normalize` returned `Malformed`.

## Fix

- `src/fairvaluelab/data_quality.py` now trims Kraken bid and ask books to the
  subscribed depth of 10 after each book entry.
- `cpp/src/venue_adapters.cpp` now parses Kraken numeric JSON price and quantity
  values directly into integer ticks and scaled quantities, with near-integer
  tolerance only at the final integer boundary. String inputs still use the
  existing exact rational parser.

## Validation

Requested checks passed:

- `uv sync`
- `uv run ruff check .`
- `uv run pytest -q`
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure`

The retained capture was reprocessed through the full existing workflow:

```powershell
uv run fvl-research data/capture/20260914T122148.319244Z --build-directory build --generated-directory data/generated/fixed_capture --results-directory data/generated/fixed_capture/results --figures-directory data/generated/fixed_capture/figures
```

The workflow completed all 18 stages.

## Before And After

| Check | Before fix | After fix |
|---|---:|---:|
| Kraken crossed books in quality replay | 110,028 | 0 |
| Kraken valid source messages rejected by C++ normalization | 141 | 0 |
| Normalized events | 940,799 | 940,999 |
| Event-sampled rows | 930,799 | 930,999 |
| Lead-lag rows | 359,803 | 359,803 |
| 50 ms clock rows | 35,980 | 35,980 |
| Kraken checksum matches | 110,910 | 110,910 |
| Kraken checksum mismatches | 0 | 0 |

Post-fix converter summary:

- Binance: `accepted=774670 malformed=0 unsupported=0`
- Kraken: `accepted=166329 malformed=0 unsupported=1800`

Post-fix data quality:

- Binance: `events=97214 book_updates=17990 trades=79224 invalid=0 crossed=0 locked=0`
- Kraken: `events=114327 book_updates=110910 trades=3198 invalid=0 crossed=0 locked=0`

Primary cross-venue comparison after the fix:

| Horizon | Test rows | Target std | Local MAE | Cross MAE | Delta MAE | Local IC | Cross IC | Cross improved |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 10 ms | 5,315 | 43.783893 | 9.620556 | 14.481894 | 4.861339 | 0.145772 | 0.101371 | false |
| 50 ms | 5,319 | 46.510994 | 10.322595 | 15.092695 | 4.770100 | 0.151519 | 0.110845 | false |
| 100 ms | 5,308 | 65.924925 | 19.454271 | 25.809284 | 6.355012 | 0.201919 | 0.171502 | false |
| 250 ms | 5,296 | 109.856521 | 43.238553 | 49.700837 | 6.462284 | 0.247502 | 0.215845 | false |
| 1 s | 5,281 | 262.905028 | 141.223958 | 159.074527 | 17.850570 | 0.307063 | 0.272493 | false |

The previous cross-venue degradation was not fully explained by the reconstruction
defect. After the fix, cross-venue features still underperform the local feature
set on this split, but the result is now based on a clean Kraken reconstruction
rather than crossed books and malformed normalizer rejects.

## Publication Recommendation

The retained 30-minute capture is sufficient to replace the previous invalid
27-second published study as a corrected empirical result. A separate 60-minute
capture is still useful for stronger evidence and stability checks, but it is not
required before replacing the invalid study artifacts.
