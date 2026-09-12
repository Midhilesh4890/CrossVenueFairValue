from __future__ import annotations

from pathlib import Path

import pandas as pd

from fairvaluelab.visualizations import generate_figures


def test_generate_figures_writes_three_png_files(tmp_path: Path) -> None:
    results = tmp_path / "results"
    figures = tmp_path / "figures"
    results.mkdir()
    pd.DataFrame(
        {
            "horizon_ns": [10_000_000, 50_000_000],
            "local_mae": [1.0, 2.0],
            "cross_venue_mae": [1.5, 1.8],
        }
    ).to_csv(results / "cross_venue_results.csv", index=False)
    pd.DataFrame(
        {
            "staleness_ns": [25_000_000, 50_000_000],
            "all_venues_valid_fraction": [0.1, 0.4],
            "mean_valid_venue_count": [0.5, 1.2],
        }
    ).to_csv(results / "staleness_results.csv", index=False)
    pd.DataFrame(
        {
            "source_venue_id": [1, 1, 4, 4],
            "response_venue_id": [4, 4, 1, 1],
            "source_signal": ["microprice_change"] * 4,
            "response_signal": ["microprice_change"] * 4,
            "lag_ns": [10_000_000, 50_000_000, 10_000_000, 50_000_000],
            "pearson_correlation": [0.01, 0.02, -0.01, 0.01],
        }
    ).to_csv(results / "lead_lag_results.csv", index=False)
    outputs = generate_figures(results, figures)
    assert len(outputs) == 3
    for output in outputs:
        assert output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
