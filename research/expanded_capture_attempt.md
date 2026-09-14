# Extended market capture attempt — 2026-09-14

## Decision

**Do not replace the committed empirical study.** The single requested 1,800-second capture completed and the candidate held-out targets have nonzero variance at all five horizons. However, existing Kraken normalization and book reconstruction defects invalidate the candidate cross-venue study. More elapsed time has not produced acceptable synchronized two-venue evidence through the current pipeline.

The raw Kraken capture is independently checksum-valid. An audit using exact decimals and the subscribed depth of ten matched all 110,910 exchange CRC32 checksums, with zero crossed books. The existing quality reconstruction instead reports 110,028 crossed Kraken books; the C++ converter rejects 140 valid book messages and one valid trade message. Candidate statistics below are pipeline diagnostics, not publishable predictive findings. No model, hyperparameter, split, sampling, feature, or source code was changed to improve results.

Only this report is committed. Existing `research/results/`, `research/figures/`, findings, negative results, methodology, README, and canonical benchmarks are preserved. This does not certify the older study against the newly discovered defects; its existing conclusion already makes no predictive claim.

## Capture and provenance

Command:

```console
uv run fvl-capture --symbol BTC-USD --venues binance kraken --duration-seconds 1800 --validate
```

Exact output directory: `data/capture/20260914T122148.319244Z`.

Start: `2026-09-14T12:21:48.319243800Z`. Provenance end: `2026-09-14T12:51:54.775021700Z`. Total provenance interval: **1,806.455778 seconds**, including connection/shutdown overhead; collection was bounded by 1,800 seconds, with no event cap and no manual interruption. Source revision: `624423d76bfa2dd94057a92b8331fb57d6a05b72`.

| Venue | Instrument | Source records | Book messages including snapshot | Trade messages | Individual trades | Other | Receipt span (seconds) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Binance | BTCUSDT | 97,214 | 17,990 | 79,224 | 79,224 | 0 | 1,799.015648 |
| Kraken | BTC/USD | 114,327 | 110,910 | 1,617 | 3,198 | 1,800 | 1,798.354511 |

Total: **211,541 source records**, 98,042,013 raw bytes. Both venues supplied substantial book data, although trade activity was strongly asymmetric. USD and USDT are different quote currencies.

Raw SHA-256:

- Binance: `47504951cbef763977b4943c4d754f5adc944bead3bf068ff82107f871985782`
- Kraken: `c1375819413c62a315ac1667559cd6ad60ef5dcbc684b4951d42bff1efe7c304`

Preflight: clean `main`, expected `Midhilesh4890/CrossVenueFairValue` remote, successful `git pull --ff-only origin main`, successful dependency sync, 47 Python tests, Ruff, and 12 Release C++ tests. CMake was already configured for Release; its installed Visual Studio CMake path and MSYS2 compiler directory were added to the process PATH. Both raw/generated paths were verified ignored. Initial free D: space was 2,415,415,296 bytes; live monitoring showed raw capture growth remained modest. Free space increased before large dataset generation. Windows AC/DC sleep timeouts were both zero. DNS and TLS handshakes to both WebSockets and the Binance snapshot endpoint succeeded.

## Quality and rejection reasons

| Check | Binance | Kraken |
| --- | --- | --- |
| Reconnect markers | 0 | 0 |
| Silent gaps over 5 seconds | 0 | 0 |
| Largest forward receipt gap | 1.892631 s | 1.011260 s |
| Exact duplicate market payloads | 0 | 0 |
| Numeric book sequence gaps / duplicates / stale events | 0 / 0 / 0 | Not available in this feed |
| Invalid raw messages reported by existing quality tool | 0 | 0 |
| Receipt timestamp regressions in file order | 1 at startup | 0 |
| Combined book/trade exchange timestamp regressions | 58 | 0 |
| Crossed books in existing quality reconstruction | 0 | 110,028 of 110,910 |
| C++ malformed source records | 0 | 141 |

Binance's sole receipt regression is line 2, a buffered depth update whose receipt precedes the snapshot receipt by 322,293,600 ns. The normalizer preserves these times and sorts by receipt; no normalized receipt regression remains. The quality tool's file-first Binance duration is 1,798.693354 seconds, whereas the sorted receipt span is 1,799.015648 seconds. Combined exchange timestamp regressions are reported separately from numeric book sequence continuity; they are not hidden or recast as receipt regressions.

Two defects are evidenced:

1. **Missing Kraken depth truncation.** `src/fairvaluelab/data_quality.py::_process_kraken` retains levels outside the subscription depth, and `cpp/src/venue_adapters.cpp::KrakenAdapter::normalize` emits received levels without maintaining the subscribed book and emitting removals for evicted levels. [Kraken's official book rules](https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2) require truncation after each message because out-of-scope levels do not receive explicit zero-quantity removals. An independent diagnostic replay parsed the original numeric tokens with `Decimal`, applied each complete message, retained the ten best levels per side, and calculated CRC32 from asks then bids. All 110,910 checksums matched, and no crossed book remained. This diagnostic replay was not substituted into the research datasets.
2. **Loss of exact numeric text.** The existing nlohmann JSON float parse/dump path changes valid `0.04145246` into `0.041452459999999997`, and `0.03439124` into `0.034391239999999997`. Exact quantity scaling then rejects those values. The actual adapter was used to identify all 141 rejected source lines: 140 book updates and one trade message; none contains an off-scale original decimal. For example, Kraken raw line 3493 carries price `77898.6` and quantity `0.04145246`. These are decoder/reconstruction failures, not evidence of corrupt market payloads.

Kraken has no numeric book sequence for reporting sequence gaps. Its raw CRC32 audit is stronger evidence of correct captured book continuity here; the unchanged research converter does not provide that check. No disconnection was observed and no second capture was started.

## Normalization and datasets

The existing converter produced **940,799 normalized events**:

| Venue | Book updates | Trades | Total |
| --- | --- | --- | --- |
| Binance | 695,446 | 79,224 | 774,670 |
| Kraken | 162,932 | 3,197 | 166,129 |
| Total | 858,378 | 82,421 | 940,799 |

Both use price ticks of 0.01 and quantity scale 100,000,000. Output integer fields parse exactly, all normalized receipt timestamps occur in their original venue source, and receipt ordering has zero regressions. Exchange timestamps remain present under the existing adapter semantics; Binance snapshot time is the receipt-time fallback because that snapshot has no exchange timestamp. Successful conversion of accepted rows does not repair the rejected Kraken messages or missing book removals.

Normalized SHA-256: `d1106fa19da0179e37a3ee0ba5f9fb377d52c26c705a2abe9ee2f84e81d9da12`.

All seven generated datasets passed the existing temporal/leakage validator. Partition counts below use the unchanged 70/15/15 chronological split, keep equal timestamps together, and purge train/validation targets crossing later boundaries. Passing these checks does not validate reconstructed market state.

| dataset | rows | train_rows | validation_rows | test_rows | purged_train_rows | purged_validation_rows | temporal_validation |
| --- | --- | --- | --- | --- | --- | --- | --- |
| dataset_event_100000000.csv | 930799 | 650973 | 138823 | 139620 | 586 | 797 | passed |
| dataset_lead_lag_5000000.csv | 359803 | 251662 | 53770 | 53971 | 200 | 200 | passed |
| dataset_staleness_100000000.csv | 35980 | 25166 | 5377 | 5397 | 20 | 20 | passed |
| dataset_staleness_25000000.csv | 35980 | 25165 | 5377 | 5397 | 21 | 20 | passed |
| dataset_staleness_250000000.csv | 35980 | 25166 | 5377 | 5397 | 20 | 20 | passed |
| dataset_staleness_50000000.csv | 35980 | 25165 | 5377 | 5397 | 21 | 20 | passed |
| dataset_staleness_500000000.csv | 35980 | 25166 | 5377 | 5397 | 20 | 20 | passed |

## Target variation in the primary 100 ms freshness dataset

The target is `mid_return_<horizon>` in price ticks. Standard deviations below use population convention (`ddof=0`), matching the cross-venue study. Non-missing counts exclude unavailable returns; row counts include them. Up/down/unchanged are signs of the target. **Every test standard deviation is nonzero, but corrupted venue reconstruction prevents interpreting that as acceptable market evidence.** At 10 ms, only 65 of 5,315 non-missing test returns change; at 1 second, 900 of 5,281 change. The primary dataset samples every 50 ms, so the nominal 10 ms horizon is not a precisely resolved 10 ms forecast.

| partition | Horizon | rows | non_missing_targets | mean_target | target_standard_deviation | minimum_target | maximum_target | distinct_targets | up_count | down_count | unchanged_count |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| train | 10 ms | 25166 | 24385 | 0.725528 | 125.760634 | -3121.000000 | 4331.500000 | 305 | 333 | 275 | 23777 |
| train | 50 ms | 25166 | 24422 | 0.547990 | 127.589134 | -3121.000000 | 4331.500000 | 322 | 342 | 290 | 23790 |
| train | 100 ms | 25166 | 24352 | 1.215670 | 168.607785 | -3267.000000 | 4331.500000 | 341 | 645 | 551 | 23156 |
| train | 250 ms | 25166 | 24314 | 2.796280 | 216.011490 | -3267.000000 | 5600.000000 | 442 | 1252 | 1027 | 22035 |
| train | 1 s | 25166 | 24277 | 9.996334 | 435.363712 | -5200.000000 | 5799.000000 | 555 | 3458 | 2582 | 18237 |
| validation | 10 ms | 5377 | 5138 | -0.245426 | 56.696359 | -1234.000000 | 1214.000000 | 84 | 51 | 65 | 5022 |
| validation | 50 ms | 5377 | 5148 | -0.795066 | 58.902605 | -1234.000000 | 1214.000000 | 87 | 51 | 72 | 5025 |
| validation | 100 ms | 5377 | 5129 | -1.628193 | 81.166604 | -1234.000000 | 1214.000000 | 89 | 102 | 138 | 4889 |
| validation | 250 ms | 5377 | 5117 | -3.896424 | 136.323500 | -1600.000000 | 1600.000000 | 113 | 208 | 298 | 4611 |
| validation | 1 s | 5377 | 5094 | -16.060071 | 341.652769 | -1600.000000 | 2200.000000 | 145 | 575 | 904 | 3615 |
| test | 10 ms | 5397 | 5315 | -1.630103 | 43.783893 | -1000.000000 | 776.000000 | 47 | 25 | 40 | 5250 |
| test | 50 ms | 5397 | 5319 | -1.536003 | 46.510994 | -1000.000000 | 1008.000000 | 51 | 27 | 42 | 5250 |
| test | 100 ms | 5397 | 5308 | -3.062359 | 65.924925 | -1061.000000 | 1008.000000 | 53 | 52 | 79 | 5177 |
| test | 250 ms | 5397 | 5296 | -6.875566 | 109.856521 | -1600.000000 | 1008.000000 | 66 | 115 | 167 | 5014 |
| test | 1 s | 5397 | 5281 | -30.686044 | 262.905028 | -2415.000000 | 1008.000000 | 85 | 397 | 503 | 4381 |

## Staleness coverage

One-venue and two-venue coverage are mutually exclusive fractions of all 35,980 clock rows. Missing-target rates below refer to the actual midpoint return, not just the future timestamp: a future timestamp can exist while the current midpoint is unavailable.

| threshold_ms | rows | zero_venue_rows | one_venue_rows | two_venue_rows | one_venue_percent | two_venue_percent |
| --- | --- | --- | --- | --- | --- | --- |
| 25.000000 | 35980 | 22307 | 13662 | 11 | 37.971095 | 0.030573 |
| 50.000000 | 35980 | 15879 | 19975 | 126 | 55.516954 | 0.350195 |
| 100.000000 | 35980 | 927 | 34762 | 291 | 96.614786 | 0.808783 |
| 250.000000 | 35980 | 369 | 35221 | 390 | 97.890495 | 1.083936 |
| 500.000000 | 35980 | 154 | 35380 | 446 | 98.332407 | 1.239578 |

| Threshold ms | 10 ms missing % | 50 ms missing % | 100 ms missing % | 250 ms missing % | 1 s missing % |
| --- | --- | --- | --- | --- | --- |
| 25.000000 | 65.158421 | 64.658143 | 64.321845 | 64.930517 | 64.630350 |
| 50.000000 | 44.861034 | 44.619233 | 44.599778 | 44.941634 | 44.866593 |
| 100.000000 | 3.062813 | 2.921067 | 3.198999 | 3.371317 | 3.579767 |
| 250.000000 | 1.147860 | 1.108949 | 1.181212 | 1.331295 | 1.481379 |
| 500.000000 | 0.472485 | 0.469705 | 0.508616 | 0.580878 | 0.739300 |

Coverage is not materially improved: old two-venue percentages at 25/50/100/250/500 ms were 1.69/15.21/43.10/58.31/59.72. Candidate coverage is far lower because venue book validity is compromised. These are not credible estimates of the true freshness tradeoff for the checksum-valid raw capture.

## Local versus cross-venue models — diagnostic only

Fixed Ridge alpha 1; 13 local features versus 44 local-plus-cross-venue features. The established workflow fits on purged train plus validation, without selecting models on test outcomes. Delta means cross-venue minus local: positive MAE delta and negative IC delta favor local. Candidate local Ridge is better on both metrics at every horizon; this does not establish a market result given the failed quality gate.

| Horizon | local_mae | local_r2 | local_ic | cross_venue_mae | cross_r2 | cross_venue_ic | delta_mae | delta_ic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 ms | 9.620556 | 0.018364 | 0.145772 | 14.518542 | -0.036918 | 0.100305 | 4.897986 | -0.045468 |
| 50 ms | 10.322595 | 0.021118 | 0.151519 | 15.134595 | -0.027246 | 0.109926 | 4.812001 | -0.041594 |
| 100 ms | 19.454271 | 0.036776 | 0.201919 | 25.859995 | -0.016768 | 0.170238 | 6.405724 | -0.031682 |
| 250 ms | 43.238553 | 0.052320 | 0.247502 | 49.802616 | -0.015468 | 0.215252 | 6.564063 | -0.032250 |
| 1 s | 141.223958 | 0.067526 | 0.307063 | 159.496167 | -0.066859 | 0.272007 | 18.272210 | -0.035056 |

Paired 95% block-bootstrap intervals use the existing 10-row blocks, 2,000 replicates, and fixed seed; all 2,000 IC replicates are defined at every horizon. The intervals quantify only this defective candidate dataset and do not remove the quality failure.

| Horizon | delta_mae_ci_lower | delta_mae_ci_upper | delta_ic_ci_lower | delta_ic_ci_upper |
| --- | --- | --- | --- | --- |
| 10 ms | 4.441264 | 5.347567 | -0.076356 | -0.013355 |
| 50 ms | 4.335555 | 5.261880 | -0.072768 | -0.013023 |
| 100 ms | 5.590906 | 7.260940 | -0.065985 | 0.005143 |
| 250 ms | 5.042583 | 8.155985 | -0.053009 | -0.014200 |
| 1 s | 13.594298 | 22.860983 | -0.064538 | -0.010450 |

Logistic classification metrics are numerically defined, but dominated by unchanged outcomes at short horizons. AUC is measured only among nonzero outcomes; values near 1 here are not reliable evidence of tradable or generalizable prediction. The raw class counts are reported above.

| Horizon | local_accuracy | cross_venue_accuracy | local_balanced_accuracy | cross_venue_balanced_accuracy | local_roc_auc_nonzero | cross_venue_roc_auc_nonzero |
| --- | --- | --- | --- | --- | --- | --- |
| 10 ms | 0.987770 | 0.987770 | 0.333333 | 0.333333 | 1.000000 | 1.000000 |
| 50 ms | 0.987028 | 0.987028 | 0.333333 | 0.333333 | 1.000000 | 1.000000 |
| 100 ms | 0.975320 | 0.975697 | 0.333333 | 0.356428 | 1.000000 | 0.999757 |
| 250 ms | 0.946941 | 0.944109 | 0.335329 | 0.336262 | 0.983598 | 0.978756 |
| 1 s | 0.829767 | 0.728082 | 0.341035 | 0.459523 | 0.938805 | 0.924754 |

## Lead-lag — diagnostic association only

The existing study uses the 5 ms clock dataset and freshness filtering, without fitted parameters. All 60 signal/direction/lag correlations are defined, ranging from approximately -0.002595 to 0.020328. There is no convincing consistent leader, and invalid book reconstruction makes even these weak associations untrustworthy. No causal claim is made.

Midpoint-change and microprice-change correlations at all supported lags:

| Lag ms | Binance to Kraken microprice_change | Binance to Kraken mid_price_change | Kraken to Binance microprice_change | Kraken to Binance mid_price_change |
| --- | --- | --- | --- | --- |
| 10.000000 | 0.003864 | 0.009161 | -0.001241 | 0.018989 |
| 25.000000 | -0.000403 | 0.003508 | -0.000033 | 0.001972 |
| 50.000000 | -0.000223 | 0.002859 | -0.000040 | 0.002393 |
| 100.000000 | 0.000099 | 0.002256 | -0.000182 | 0.003538 |
| 250.000000 | -0.000085 | 0.000771 | 0.000634 | -0.002595 |
| 500.000000 | 0.001121 | 0.001201 | 0.001092 | -0.000264 |

## Midpoint versus microprice — diagnostic only

The existing reference comparison uses the full capture and no fitted parameters. For the future consolidated-midpoint target, microprice has higher candidate MAE than midpoint at every horizon for the consolidated, Binance, and Kraken references. It therefore does not improve the previous descriptive conclusion; the enormous Kraken errors are another warning about the reconstructed state. These numbers cannot validate a general midpoint advantage.

| Horizon | consolidated_microprice | consolidated_midpoint | venue_1_microprice | venue_1_midpoint | venue_4_microprice | venue_4_midpoint |
| --- | --- | --- | --- | --- | --- | --- |
| 10 ms | 10.982329 | 10.659327 | 20.048901 | 19.731800 | 14482.052482 | 6552.177828 |
| 50 ms | 11.491424 | 11.170575 | 20.544772 | 20.229816 | 14483.189314 | 6549.236804 |
| 100 ms | 20.515376 | 20.226076 | 26.845806 | 26.559693 | 14485.787186 | 6555.612187 |
| 250 ms | 39.529090 | 39.271889 | 45.095842 | 44.840138 | 14484.629004 | 6560.289924 |
| 1 s | 139.070937 | 138.904560 | 141.934296 | 141.768095 | 14493.443567 | 6575.144378 |

## Feature ablation — diagnostic only

All nine existing cumulative groups were evaluated with the same Ridge settings. The following reports every group, not a selected test winner. MAE is in ticks. No feature ranking from these defective datasets should guide deployment or model selection.

| feature_set | 10 ms | 50 ms | 100 ms | 250 ms | 1 s |
| --- | --- | --- | --- | --- | --- |
| top_of_book | 4.347708 | 4.470153 | 8.840941 | 21.605314 | 89.579680 |
| plus_depth | 8.275441 | 8.943634 | 17.082646 | 37.359260 | 125.498175 |
| plus_imbalance | 9.736224 | 10.428247 | 19.555841 | 43.561599 | 142.464532 |
| plus_ofi | 9.716325 | 10.398243 | 19.422041 | 43.173734 | 141.181520 |
| plus_multi_level_ofi | 9.562885 | 10.246147 | 19.213428 | 42.951552 | 141.117249 |
| plus_trade_flow | 9.620556 | 10.322595 | 19.454271 | 43.238553 | 141.223958 |
| plus_cross_venue_basis | 11.263111 | 11.686779 | 22.089800 | 45.729469 | 149.075012 |
| plus_pairwise_features | 12.415283 | 12.848020 | 23.613451 | 46.407883 | 152.723909 |
| plus_lead_lag_features | 13.298520 | 13.873000 | 23.577584 | 46.496408 | 152.922588 |

## Offline latency sensitivity — diagnostic only

The unchanged event-sampled study evaluates every requested delay from 0 to 5 ms. It realigns decision/target states and refits the fixed Ridge pipeline for each delay; this is not measured network execution latency. The event dataset has 930,799 rows but only 209,599 distinct receipt timestamps, with 721,200 zero-length timestamp gaps from normalization expansion. The smallest positive gap is 10.4 microseconds and the median positive gap is 69.7 microseconds. In particular, 5 microseconds is below the smallest observed positive interval; small-delay differences cannot be interpreted as precisely measured signal decay. All delay results also inherit the failed Kraken state reconstruction.

All 50 delay/horizon combinations have nonzero target variation. Candidate IC falls between 0 and 5 ms at every horizon (10 ms: 0.206593 to 0.096996; 1 s: 0.471680 to 0.461364), but is not monotonic at every intermediate delay. This is a diagnostic association with delayed alignment, not a defensible estimate of market signal decay given the reconstruction failure.

IC by delay and horizon:

| Added delay us | 10 ms | 50 ms | 100 ms | 250 ms | 1 s |
| --- | --- | --- | --- | --- | --- |
| 0.000000 | 0.206593 | 0.250412 | 0.308512 | 0.411968 | 0.471680 |
| 5.000000 | 0.206584 | 0.250401 | 0.308486 | 0.411968 | 0.471657 |
| 10.000000 | 0.206585 | 0.250404 | 0.308450 | 0.411968 | 0.471750 |
| 25.000000 | 0.206428 | 0.250291 | 0.308196 | 0.411943 | 0.471899 |
| 50.000000 | 0.205844 | 0.249859 | 0.308061 | 0.411845 | 0.471750 |
| 100.000000 | 0.204855 | 0.249141 | 0.309608 | 0.411612 | 0.471548 |
| 250.000000 | 0.205658 | 0.247271 | 0.308257 | 0.411006 | 0.470854 |
| 500.000000 | 0.200839 | 0.243037 | 0.308785 | 0.409494 | 0.469861 |
| 1000.000000 | 0.186352 | 0.230484 | 0.309040 | 0.408002 | 0.468104 |
| 5000.000000 | 0.096996 | 0.162118 | 0.256899 | 0.402617 | 0.461364 |

MAE by delay and horizon:

| Added delay us | 10 ms | 50 ms | 100 ms | 250 ms | 1 s |
| --- | --- | --- | --- | --- | --- |
| 0.000000 | 34.939931 | 54.695126 | 75.331014 | 138.062135 | 268.884960 |
| 5.000000 | 34.942527 | 54.695176 | 75.315272 | 138.062135 | 268.968929 |
| 10.000000 | 34.944772 | 54.695537 | 75.313237 | 138.062209 | 268.948826 |
| 25.000000 | 34.917284 | 54.662861 | 75.351018 | 138.042029 | 268.874085 |
| 50.000000 | 34.766473 | 54.518265 | 75.283957 | 137.934747 | 269.029095 |
| 100.000000 | 34.494654 | 54.244666 | 75.160835 | 137.732337 | 268.547186 |
| 250.000000 | 34.556369 | 53.366862 | 75.997254 | 137.078510 | 267.347954 |
| 500.000000 | 33.069143 | 51.944452 | 75.174380 | 136.133572 | 266.577758 |
| 1000.000000 | 30.022809 | 49.514331 | 73.164071 | 133.916591 | 264.346461 |
| 5000.000000 | 16.681104 | 34.499277 | 65.995479 | 123.581342 | 255.475109 |

## Regimes — diagnostic only

Thresholds remain medians from the purged training partition, never test-derived. The table aggregates each populated regime/band across horizons and reports sample-count ranges, target-variation ranges, and cross-minus-local MAE ranges. Positive MAE deltas favor local. It provides no validated claim of regime-dependent predictive value. `trade_activity` is the existing absolute signed trade-volume proxy, not a separately measured trade-arrival intensity.

| regime | band | threshold | min_test_rows | max_test_rows | min_target_std | max_target_std | min_delta_mae | max_delta_mae | horizons_with_lower_cross_mae |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| depth | high | 2024751000.000000 | 1547 | 1577 | 62.341935 | 272.821434 | 2.494532 | 15.036589 | 0 |
| depth | low | 2024751000.000000 | 3730 | 3742 | 32.866599 | 256.617467 | 5.788657 | 19.612731 | 0 |
| imbalance | high | 0.773412 | 2065 | 2078 | 63.001916 | 288.730275 | 2.274211 | 10.122184 | 0 |
| imbalance | low | 0.773412 | 3216 | 3241 | 24.517483 | 244.827105 | 5.948420 | 23.505357 | 0 |
| spread | low | 1.000000 | 5281 | 5319 | 43.783893 | 262.905028 | 4.812001 | 18.272210 | 0 |
| trade_activity | high | 476000.000000 | 2018 | 2041 | 68.386464 | 271.372286 | 3.380236 | 19.862297 | 0 |
| trade_activity | low | 476000.000000 | 3263 | 3278 | 13.731928 | 256.667987 | 5.703469 | 17.288821 | 0 |
| venue_age | high | 30491550.000000 | 2436 | 2444 | 47.309428 | 264.046428 | 3.714308 | 17.260791 | 0 |
| venue_age | low | 30491550.000000 | 2845 | 2875 | 40.550488 | 261.918551 | 5.703971 | 19.138225 | 0 |
| volatility | high | 0.000000 | 808 | 812 | 79.460873 | 294.464785 | 0.707077 | 16.898212 | 0 |
| volatility | low | 0.000000 | 4469 | 4507 | 33.464584 | 255.008666 | 5.551561 | 18.521860 | 0 |

All 55 populated regime/horizon cells have higher cross-venue MAE than local MAE. The spread test observations occupy only the low band, so no between-spread-regime comparison is available. Other bands contain at least 808 test observations, but their apparent differences remain unvalidated because of the reconstruction defects.

## Benchmarks and computation value

The workflow ran its automatic benchmark once, with 5,000,000 events and five timed repetitions after warmup. These are validation measurements, not replacements for the canonical 71.1626 ns/book event and 1,034.566 ns/feature-and-cross-venue event. No benchmarks were repeated to seek faster numbers.

| benchmark | event_count | repetitions | mean_ns_per_event | mean_events_per_second |
| --- | --- | --- | --- | --- |
| order_book_update | 5000000 | 5 | 47.229400 | 21174255.379800 |
| feature_and_cross_venue | 5000000 | 5 | 647.368000 | 1547973.642000 |

| feature_set | min_inference_ns | max_inference_ns | min_effective_ns | max_effective_ns |
| --- | --- | --- | --- | --- |
| local_microstructure | 877395.600000 | 896766.600000 | 878042.968000 | 897413.968000 |
| local_plus_cross_venue | 1117244.600000 | 1141899.000000 | 1117891.968000 | 1142546.368000 |
| microprice_deviation | 824838.800000 | 916634.000000 | 825486.168000 | 917281.368000 |

The latency-versus-predictive-power tool completed, but numerical target variation does not override the quality failure. No conclusion that extra feature computation is worthwhile is supported.

## Old versus new

| Measure | Old committed study | New candidate |
| --- | --- | --- |
| Provenance duration | 27.737 seconds | 1,806.456 seconds including shutdown |
| Receipt data span | Short, venue-asymmetric | About 1,799 seconds per venue |
| Source records | 600 | 211,541 |
| Normalized events | 14,487 | 940,799, with 141 rejected source records |
| Primary clock rows | 355 | 35,980 |
| Purged train / validation / test rows | 228 / 33 / 54 | 25,166 / 5,377 / 5,397 |
| Test std: 10 / 50 / 100 / 250 ms / 1 s | 0 / 0 / 0 / 0 / 0 | 43.783893 / 46.510994 / 65.924925 / 109.856521 / 262.905028 |
| Two-venue coverage at 100 ms | 43.10% | 0.808783% |
| Publishable new predictive evidence | No | No: normalization/reconstruction failure |

The duration and target-variation goals are numerically improved. The required quality, normalization, and synchronized-coverage criteria fail, so the new experiment is not promoted. The initial workflow completed stages 1-13, then stopped in latency sensitivity on a 368 MiB NumPy allocation failure during the chronological partition copy. After the separate dataset audit finished, latency was retried alone with identical settings, and the remaining existing stages were resumed individually. Together these completed quality, normalization, five staleness datasets, event and lead-lag datasets, temporal checks, Ridge/logistic, paired bootstrap, lead-lag, references, ablation, staleness, latency, benchmark, latency-power, regimes, and figures. Computational completion is not empirical validity.

## Reproduction and retained local artifacts

```console
uv run fvl-research data/capture/20260914T122148.319244Z --build-directory build --generated-directory data/generated/expanded_capture --results-directory data/generated/expanded_capture/results_candidate --figures-directory data/generated/expanded_capture/figures_candidate
```

The first workflow step is the requested `fvl-data-quality` command, writing `results_candidate/data_quality.json`. Logs and all candidate outputs remain under ignored `data/generated/expanded_capture/`; raw captures remain under ignored `data/capture/`. Supplementary audit scripts in the generated directory produced raw hashes, receipt checks, CRC32 diagnosis, normalized counts, target distributions, coverage, partition validation, and event resolution. The CRC32 procedure is specified above so the result is reviewable even without committing diagnostic tooling. The same raw files, revision, hashes, and commands are retained for reproduction; a second full statistical run was not performed. Future live captures and machine timing measurements need not match.

Final verification: `uv run ruff check .`, `uv run pytest` (47 tests), Release C++ build, and `ctest --test-dir build -C Release --output-on-failure` (12 tests), plus temporal validation of all seven candidate datasets. Git review includes `git diff`, `git diff --check`, and status. Only `research/expanded_capture_attempt.md` is selected for the focused commit; no raw data, generated datasets, candidate directories, build artifacts, credentials, or source edits are included.

## Next step recommendation

Repair and verify the existing Kraken decimal-decoding and subscribed-depth reconstruction defects, then reprocess this retained 30-minute capture before spending time collecting more data. This run stops at the documented failed promotion gate. For a subsequent independent capture after those fixes, **60 minutes** is a reasonable next duration, with emphasis on stable two-venue validity and varied held-out targets rather than elapsed time alone. No additional capture was started.
