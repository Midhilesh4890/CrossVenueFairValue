# Negative Results and Inconclusive Findings

This document preserves results that did not support a predictive claim. They are not evidence that cross-venue information is universally ineffective. They show what could and could not be established from the repository's current bounded public capture.

## Evidence boundary

The analyzed capture contains 600 events, split evenly between Binance BTCUSDT and Kraken BTC/USD, over approximately 27.7 seconds. It produced 355 clock-sampled rows. The venues use economically related but non-identical quote instruments, the capture is short, and only 153 rows in the 100 ms-staleness dataset contain two valid venues. These limitations prevent broad market conclusions. See [real_dataset.json](results/real_dataset.json) and [data_quality.json](results/data_quality.json).

The purged chronological test partitions contain only one unique midpoint-return value at every evaluated horizon. Consequently, their target standard deviation is zero, IC is undefined, and classification metrics may be degenerate. MAE differences remain descriptive but cannot establish generalization to varying future returns.

## Cross-venue features did not establish predictive improvement

The cross-venue comparison found no supported improvement at any horizon. The 44-feature local-plus-cross-venue Ridge model had higher MAE than the 13-feature local model at 10 ms, 50 ms, 100 ms, and 250 ms. The respective MAE increases were 837.58, 837.58, 1,045.70, and 835.19 ticks. At 1 second, cross-venue MAE was 153.15 ticks lower, but the test target was still constant, so the result was classified as insufficient target variation rather than supported improvement. See [cross_venue_results.csv](results/cross_venue_results.csv).

This means the primary question—whether other venues improve short-horizon prediction—remains unanswered by this capture.

## Larger feature sets generally increased test MAE

In the fixed Ridge ablation, the top-of-book feature set had the lowest MAE at all five horizons. Most successive feature additions increased MAE, sometimes substantially. For example, at 10 ms MAE rose from 1.29 ticks for top-of-book to 911.90 ticks after lead-lag features were included. At 1 second it rose from 28.11 ticks to 225.12 ticks. A few individual additions reduced MAE relative to the immediately preceding set, but none beat the top-of-book result. See [ablation_results.csv](results/ablation_results.csv).

Because the held-out targets were constant, this is a failure of the richer models on this split, not reliable evidence that deeper-book, flow, or cross-venue features lack value in general.

## Lead-lag estimates were mostly undefined or negligible

Of 60 tested venue-direction, signal, and lag combinations, only 27 produced a defined correlation. Mid-price change, OFI, multi-level OFI, and signed trade-flow relationships were frequently undefined because one side lacked variation. Defined correlations ranged from -0.0171 to 0.0381 and remained close to zero. The study therefore did not identify consistent venue leadership or a robust flow-before-price relationship. See [lead_lag_results.csv](results/lead_lag_results.csv).

These are temporal associations only; even a larger measured correlation would not establish causality.

## Microprice did not consistently outperform midpoint

For predicting future consolidated midpoint, current consolidated microprice had slightly higher MAE than current consolidated midpoint at every horizon. Venue-level midpoint-versus-microprice differences were small and mixed across targets and horizons. No consistent microprice MAE advantage was established. See [microprice_results.csv](results/microprice_results.csv).

Several directional metrics look strong in isolated rows, but class imbalance and differing row availability make them unsuitable as standalone evidence.

## Staleness sensitivity was not estimable

At a 25 ms freshness threshold, the chronological test partitions contained only one to four usable rows, so all five horizons were marked as having insufficient test rows. At thresholds from 50 ms through 500 ms, the test targets had one unique value and every horizon was marked as having insufficient target variation. The experiment measured the expected coverage increase as freshness thresholds relaxed, but it could not determine how much genuine predictive information survives stale-state filtering. See [staleness_results.csv](results/staleness_results.csv).

## Latency decay was not estimable

Across simulated additional decision delays from 0 to 5 ms, all 50 horizon-delay combinations had zero test-target standard deviation and undefined IC. Changes in MAE cannot be interpreted as predictive-signal decay under these conditions. The experiment therefore does not establish a usable lifetime for the cross-venue relationship. See [latency_results.csv](results/latency_results.csv).

The latency-versus-predictive-power table likewise marks all 15 model, feature-set, and horizon combinations as unassessable. It reports measured machine-specific computation cost, but the current quality data cannot answer whether added computation is worthwhile. See [latency_power_results.csv](results/latency_power_results.csv).

## Regime conclusions were not supported

All 45 populated regime evaluations had constant targets and were classified as insufficient target variation. Spread, volatility, and depth each occupied only one test band because the short capture did not cross their training-derived median thresholds out of sample. Trade activity, imbalance, and venue age populated both bands, but still lacked target variation. Cross-venue usefulness therefore could not be compared reliably across regimes. See [regime_results.csv](results/regime_results.csv).

## Nonlinear modeling was intentionally skipped

A nonlinear baseline was not run. With no held-out target variation, a comparison against Ridge could not provide a meaningful predictive-performance judgment, while adding another fitted model would increase complexity without resolving the data limitation.

## What would resolve these findings

A defensible follow-up requires materially longer synchronized captures across multiple market conditions, more events from both venues, and chronological test partitions containing varied future returns. The same predeclared horizons, feature groups, freshness thresholds, and latency delays should then be rerun without selecting settings based on the current test results.
