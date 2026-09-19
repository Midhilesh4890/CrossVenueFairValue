# Negative Results and Inconclusive Findings

## Cross-venue features

Cross-venue Ridge had higher MAE and lower IC at all five horizons. The paired
block-bootstrap MAE intervals excluded zero in the unfavorable direction. Adding
cumulative feature groups also failed to beat the top-of-book model on MAE.
[Findings](findings.md) records the measurements. Both results apply to the retained
capture, fixed Ridge setup, and chronological split; other samples remain untested.

## Lead-lag and reference choice

The tested lags showed no consistent venue leader. Consolidated microprice did not
improve MAE over midpoint for the future consolidated-midpoint target. Sparse
simultaneous venue eligibility limits these comparisons. These associations do
not measure causality or execution outcomes.

## Earlier invalid experiments

The original short capture had constant targets in the purged test partitions,
so its predictive metrics were inconclusive. The longer capture initially failed
the reconstruction quality gate: Python retained out-of-depth Kraken levels and
C++ rejected valid numeric quantities. The
[expanded-capture record](expanded_capture_attempt.md) preserves those diagnostic
results; the [reconstruction audit](kraken_reconstruction_fix.md) records the
fixtures, repair, and before/after checks. Those failed runs are not current results.

## Remaining evidence gaps

One 30-minute capture cannot measure stability across days or market conditions.
USD/USDT basis and low two-venue coverage complicate interpretation. Offline delay
alignment and machine-specific computation timings do not establish executable
value, signal lifetime, or profitability.
