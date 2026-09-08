#!/usr/bin/env python3
"""Plot convergence diagnostics from BOX3D_OSI submit job 586660."""
import re
from pathlib import Path

import matplotlib.pyplot as plt

CASE = Path(__file__).resolve().parents[1]
LOG = CASE / "logs" / "submit" / "586660-out.log"
OUT = CASE / "data_post_processing" / "586660_convergence.png"
trend = re.compile(r"CONVERGENCE_TREND step=(\d+).*?velocity_l2_relative=([0-9.eE+-]+)")
formal = re.compile(r"CONVERGENCE step=(\d+).*?velocity_l2_relative=([0-9.eE+-]+)")

trend_points, formal_points = [], []
for line in LOG.read_text(errors="replace").splitlines():
    match = trend.search(line)
    if match:
        trend_points.append((int(match.group(1)), float(match.group(2))))
    match = formal.search(line)
    if match:
        formal_points.append((int(match.group(1)), float(match.group(2))))

if not trend_points or not formal_points:
    raise SystemExit(f"No convergence records found in {LOG}")

fig, ax = plt.subplots(figsize=(8.5, 5.5))
ax.semilogy(*zip(*trend_points), color="#377eb8", lw=1.0, alpha=0.7,
            label="Trend (1000-step interval)")
ax.semilogy(*zip(*formal_points), "o-", color="#d95f02", ms=3.5, lw=1.2,
            label="Formal check (3200-step interval)")
ax.axhline(1e-12, color="#555555", ls="--", lw=1.0, label="Tolerance = 1e-12")
ax.set_xlabel("Step")
ax.set_ylabel(r"Relative velocity change $||u^n-u^{n-k}||_2/||u^n||_2$")
ax.set_title("BOX3D_OSI job 586660 convergence history")
ax.grid(True, which="both", alpha=0.25)
ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(OUT, dpi=220)
print(f"trend points: {len(trend_points)}, formal points: {len(formal_points)}")
print(f"formal minimum: step={min(formal_points, key=lambda p: p[1])[0]}, value={min(p[1] for p in formal_points):.6e}")
print(OUT)
