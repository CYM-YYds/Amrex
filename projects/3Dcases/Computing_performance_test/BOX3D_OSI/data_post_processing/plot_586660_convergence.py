#!/usr/bin/env python3
"""Plot convergence diagnostics for BOX3D_OSI runs 586660 + 586864 (restart).

586660 covers steps 0--96000; 586864 restarts from checkpoint 96000 and runs
to 160000. The two logs are merged by step number to produce a single
continuous convergence history.
"""
import re
from pathlib import Path

import matplotlib.pyplot as plt

CASE = Path(__file__).resolve().parents[1]
LOGS = [
    CASE / "logs" / "submit" / "586660-out.log",
    CASE / "logs" / "submit" / "586864-out.log",
]
OUT = CASE / "data_post_processing" / "586660_convergence.png"
trend = re.compile(r"CONVERGENCE_TREND step=(\d+).*?velocity_l2_relative=([0-9.eE+-]+)")
formal = re.compile(r"CONVERGENCE step=(\d+).*?velocity_l2_relative=([0-9.eE+-]+)")

trend_points, formal_points = {}, {}
for log in LOGS:
    if not log.exists():
        raise SystemExit(f"Missing log: {log}")
    for line in log.read_text(errors="replace").splitlines():
        m = trend.search(line)
        if m:
            trend_points[int(m.group(1))] = float(m.group(2))
        m = formal.search(line)
        if m:
            formal_points[int(m.group(1))] = float(m.group(2))

if not trend_points or not formal_points:
    raise SystemExit("No convergence records found")

trend_xy = sorted(trend_points.items())
formal_xy = sorted(formal_points.items())

fig, ax = plt.subplots(figsize=(9.5, 5.5))
ax.semilogy(*zip(*trend_xy), color="#377eb8", lw=1.0, alpha=0.7,
            label="Trend (1000-step interval)")
ax.semilogy(*zip(*formal_xy), "o-", color="#d95f02", ms=3.5, lw=1.2,
            label="Formal check (3200-step interval)")
ax.axvline(96000, color="#999999", ls=":", lw=1.0,
           label="Checkpoint restart at step 96000")
ax.axhline(1e-12, color="#555555", ls="--", lw=1.0, label="Tolerance = 1e-12")
ax.set_xlabel("Step")
ax.set_ylabel(r"Relative velocity change $||u^n-u^{n-k}||_2/||u^n||_2$")
ax.set_title("BOX3D_OSI convergence history (jobs 586660 + 586864, steps 0--160000)")
ax.grid(True, which="both", alpha=0.25)
ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(OUT, dpi=220)
print(f"trend points: {len(trend_xy)}, formal points: {len(formal_xy)}")
print(f"formal minimum: step={min(formal_xy, key=lambda p: p[1])[0]}, "
      f"value={min(p[1] for p in formal_xy):.6e}")
print(f"formal final: step={formal_xy[-1][0]}, value={formal_xy[-1][1]:.6e}")
print(OUT)
