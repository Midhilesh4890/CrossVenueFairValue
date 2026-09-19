# Empirical Findings

## Dataset studied

The 2026-09-14 capture runs from 12:21:48 to 12:51:54 UTC (1,806.456 seconds). It has 97,214 Binance `BTCUSDT` and 114,327 Kraken `BTC/USD` source messages, or 211,541 total. Normalization yields 940,999 events. Kraken has zero crossed books and zero valid source messages rejected; 110,910 checksums match and none mismatch. The primary 50 ms clock dataset contains 35,980 rows. Its chronological partition contains 25,166 train, 5,377 validation, and 5,397 test rows before horizon-specific purging and eligibility filters. See [real_dataset.json](results/real_dataset.json), [data_quality.json](results/data_quality.json), and the historical [reconstruction audit](kraken_reconstruction_fix.md).

The quote currencies differ, and only 291 of 35,980 primary rows have both venues valid at the 100 ms freshness threshold.

## Cross-venue comparison and horizon results

| Horizon | Eligible test rows | Target std. dev. (ticks) | Local MAE | Cross-venue MAE | Local IC | Cross-venue IC |
|---:|---:|---:|---:|---:|---:|---:|
| 10 ms | 5,315 | 43.783893 | 9.620556 | 14.481894 | 0.145772 | 0.101371 |
| 50 ms | 5,319 | 46.510994 | 10.322595 | 15.092695 | 0.151519 | 0.110845 |
| 100 ms | 5,308 | 65.924925 | 19.454271 | 25.809284 | 0.201919 | 0.171502 |
| 250 ms | 5,296 | 109.856521 | 43.238553 | 49.700837 | 0.247502 | 0.215845 |
| 1 s | 5,281 | 262.905028 | 141.223958 | 159.074527 | 0.307063 | 0.272493 |

MAE and target standard deviation use normalized price ticks. The paired block-bootstrap MAE difference (cross minus local) is positive at all horizons; its reported intervals exclude zero. These results describe this split, not a universal ranking. See [cross_venue_results.csv](results/cross_venue_results.csv).

![Held-out Ridge MAE across horizons](figures/cross_venue_mae.png)

## Feature ablation

The one-feature top-of-book Ridge set has the lowest test MAE among the nine cumulative sets at each horizon: 4.35, 4.47, 8.84, 21.61, and 89.58 ticks from 10 ms to 1 second. The full cumulative set has 13.53, 14.05, 23.67, 46.40, and 152.82 ticks. Some intermediate additions improve on the immediately preceding set, but none beats top of book. This is a within-capture comparison; feature count and correlated predictors can affect fitted Ridge behavior. See [ablation_results.csv](results/ablation_results.csv).

## Lead-lag

All 60 tested direction, signal, and lag combinations have defined correlations, ranging from -0.00257 to 0.02102. Microprice-change correlations in both venue directions remain close to zero over the tested 10 to 500 ms lags. The table does not show consistent venue leadership. These are temporal associations, not causal estimates. See [lead_lag_results.csv](results/lead_lag_results.csv).

![Microprice-change lead-lag correlations](figures/lead_lag_microprice.png)

## Microprice

For the future consolidated-midpoint reference, current consolidated microprice has slightly higher MAE than current consolidated midpoint at every horizon: 10.982 versus 10.659 ticks at 10 ms and 139.071 versus 138.905 at 1 second. The reference comparison uses the full capture without fitted parameters, so it is separate from the held-out Ridge comparison. See [microprice_results.csv](results/microprice_results.csv).

## Staleness

Two-venue coverage grows from 11 of 35,980 clock rows (0.031%) at 25 ms freshness to 446 rows (1.240%) at 500 ms; at the primary 100 ms threshold it is 291 rows (0.809%). All 25 horizon and freshness evaluations have varied test targets and completed status. The coverage change is measurable, while the scarcity of simultaneously valid venues limits interpretation of cross-venue features. See [staleness_results.csv](results/staleness_results.csv).

![Two-venue coverage by freshness threshold](figures/staleness_coverage.png)

## Latency and computation

All 50 offline horizon and added-decision-delay evaluations, covering 0 to 5 ms, completed with defined IC. The delay study uses an event-sampled dataset and is not directly comparable to the primary 50 ms clock split. Its results describe delayed alignment in this capture, not live execution or an exploitable signal lifetime. See [latency_results.csv](results/latency_results.csv).

The canonical C++ Release benchmark averages 71.16 ns per order-book update and 1,034.57 ns per feature and synchronization event. The corrected run also reports machine-specific Python/scikit-learn single-row inference of roughly 0.79 to 1.10 ms across its evaluated pipelines. The separate benchmark rerun is not promoted because a faster run alone does not change the canonical hardware methodology. See [benchmark_results.json](results/benchmark_results.json) and [latency_power_results.csv](results/latency_power_results.csv).

## Regimes

All 55 populated horizon and regime-band evaluations report higher cross-venue MAE. Spread occupies only its low test band, limiting that comparison; the other five regime variables have both bands. Training-partition medians set thresholds. These subgroup results are exploratory. See [regime_results.csv](results/regime_results.csv).
