#!/usr/bin/env python3
"""Plot convergence diagnostics for BOX3D_OSI runs 586660 + 586864 (restart).

586660 covers steps 0--96000; 586864 restarts from checkpoint 96000 and runs
to 160000. The two logs are merged by step number to produce a single
continuous convergence history.
"""
import argparse
from pathlib import Path

import matplotlib.pyplot as plt

from convergence_logs import read_records

CASE = Path(__file__).resolve().parents[1]
LOGS = [
    CASE / "logs" / "submit" / "586660-out.log",
    CASE / "logs" / "submit" / "586864-out.log",
]
OUT = CASE / "data_post_processing" / "586660_convergence.png"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--log", action="append", type=Path, dest="logs")
parser.add_argument("--output", type=Path, default=OUT)
args = parser.parse_args()
logs = args.logs or LOGS
trend_records, formal_records = read_records(logs)
trend_xy = [(record[0], record[2]) for record in trend_records]
formal_xy = [(record[0], record[2]) for record in formal_records]
trend_interval = trend_records[-1][1]
formal_interval = formal_records[-1][1]
tolerance = formal_records[-1][5]

fig, ax = plt.subplots(figsize=(9.5, 5.5))
ax.semilogy(*zip(*trend_xy), color="#377eb8", lw=1.0, alpha=0.7,
            label=f"Trend ({trend_interval}-step interval)")
ax.semilogy(*zip(*formal_xy), "o-", color="#d95f02", ms=3.5, lw=1.2,
            label=f"Formal check ({formal_interval}-step interval)")
if args.logs is None:
    ax.axvline(96000, color="#999999", ls=":", lw=1.0,
               label="Checkpoint restart at step 96000")
ax.axhline(tolerance, color="#555555", ls="--", lw=1.0,
           label=f"Tolerance = {tolerance:.0e}")
ax.set_xlabel("Step")
ax.set_ylabel(r"Relative velocity change $||u^n-u^{n-k}||_2/||u^n||_2$")
ax.set_title("BOX3D_OSI convergence history (jobs 586660 + 586864, steps 0--160000)"
             if args.logs is None else "BOX3D_OSI convergence history")
ax.grid(True, which="both", alpha=0.25)
ax.legend(fontsize=9)
fig.text(0.5, 0.01,
         f"Sources: {', '.join(str(path) for path in logs)}; steps {min(trend_xy)[0]}–{max(trend_xy)[0]}; "
         f"relative level-0 velocity L2 change ({trend_interval}/{formal_interval} steps); "
         "reference: tolerance from final formal log record",
         ha="center", fontsize=7)
fig.tight_layout(rect=(0, 0.04, 1, 1))
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=220)
print(f"trend points: {len(trend_xy)}, formal points: {len(formal_xy)}")
print(f"formal minimum: step={min(formal_xy, key=lambda p: p[1])[0]}, "
      f"value={min(p[1] for p in formal_xy):.6e}")
print(f"formal final: step={formal_xy[-1][0]}, value={formal_xy[-1][1]:.6e}")
print(args.output)
