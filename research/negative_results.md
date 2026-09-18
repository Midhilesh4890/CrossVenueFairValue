# Negative Results and Inconclusive Findings

## Evidence boundary

The corrected retained 2026-09-14 capture spans 1,806.456 seconds, with 211,541 source messages and 940,999 normalized events. Kraken reconstruction has zero crossed books, zero valid source messages rejected, 110,910 matching checksums, and zero mismatches. The primary held-out target varies at all five horizons; its standard deviation rises from 43.783893 ticks at 10 ms to 262.905028 ticks at 1 second. See [real_dataset.json](results/real_dataset.json), [data_quality.json](results/data_quality.json), and the [reconstruction audit](kraken_reconstruction_fix.md).

## Current cross-venue features do not improve this split

The local-plus-cross-venue Ridge model has higher MAE and lower IC than local Ridge at 10, 50, 100, and 250 ms and 1 second. Cross minus local MAE ranges from +4.770100 to +17.850570 ticks; the paired block-bootstrap intervals for MAE exclude zero in the unfavorable direction at all five horizons. The [comparison table](results/cross_venue_results.csv) records eligible rows, target variation, IC, and uncertainty. This is a result for the current capture, model, features, and chronological split, not a claim about all cross-venue information.

## Richer cumulative features do not win on MAE

The top-of-book Ridge set has the lowest held-out MAE at all five horizons in the cumulative [ablation](results/ablation_results.csv). The full set reaches 13.53 ticks versus 4.35 at 10 ms and 152.82 versus 89.58 at 1 second. Some intermediate additions improve on the preceding set, but none overtakes top of book. This ranking is specific to this sample and fixed Ridge setup.

## No consistent lead-lag or microprice advantage

The 60 defined [lead-lag correlations](results/lead_lag_results.csv) range from -0.00257 to 0.02102, with no consistent directional leader. For the future consolidated-midpoint reference, consolidated microprice has slightly higher MAE than consolidated midpoint at every horizon in the full-capture [reference study](results/microprice_results.csv). Neither result rules out useful relationships under other sampling, instruments, or market conditions.

## Freshness, latency, and regimes

Two-venue valid coverage is 0.031% at 25 ms freshness, 0.809% at the primary 100 ms setting, and 1.240% at 500 ms. All 25 [staleness evaluations](results/staleness_results.csv) have varied targets, but sparse simultaneous eligibility constrains cross-venue interpretation.

All 50 [offline delay evaluations](results/latency_results.csv) completed with defined IC. They measure sensitivity to 0 to 5 ms added decision delay on an event-sampled dataset, not live trading latency or executable value. The [latency-power table](results/latency_power_results.csv) combines predictive and machine-specific computation measurements; complexity value is marked unassessable for two entries.

All 55 populated [regime evaluations](results/regime_results.csv) have higher cross-venue MAE. Spread has only one populated test band; the remaining regime variables have both bands. Subgroup outcomes on this single capture should not be generalized to other regimes.

## Conclusion

The corrected study has meaningful target variation and clean Kraken reconstruction, yet the current cross-venue feature set fails to outperform the local set on this chronological split. Additional captures across market conditions are needed to assess whether that result persists. The study makes no claim about causality, market alpha, or profitability.
