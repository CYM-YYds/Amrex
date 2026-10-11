#!/usr/bin/env python3
"""绘制 Re=3200 三段连续计算到 288000 步的收敛趋势。"""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt

from convergence_logs import read_records


CASE = Path(__file__).resolve().parents[1]
LOGS = [
    CASE / "logs" / "submit" / "596154-out.log",
    CASE / "logs" / "submit" / "596532-out.log",
    CASE / "runs" / "20260915_210444_job596888" / "run.log",
]
OUT = CASE / "data_post_processing" / "596154_Re3200_convergence.png"
CSV_OUT = CASE / "data_post_processing" / "596154_Re3200_convergence.csv"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--log", action="append", type=Path, dest="logs")
parser.add_argument("--output", type=Path, default=OUT)
parser.add_argument("--csv-output", type=Path)
args = parser.parse_args()
logs = args.logs or LOGS
csv_output = args.csv_output or (CSV_OUT if args.output == OUT else args.output.with_suffix(".csv"))
trend, formal = read_records(logs)
tolerance = formal[-1][5]

csv_output.parent.mkdir(parents=True, exist_ok=True)
with csv_output.open("w", newline="") as stream:
    writer = csv.writer(stream)
    writer.writerow(
        ["record", "step", "interval", "velocity_l2_relative", "velocity_l2", "delta_velocity_l2"]
    )
    writer.writerows(("trend", *record) for record in trend)
    writer.writerows(("formal", *record[:5]) for record in formal)

fig, (ax_change, ax_norm) = plt.subplots(
    2, 1, figsize=(10, 7.5), sharex=True, gridspec_kw={"height_ratios": [2.2, 1]}
)

ax_change.semilogy(
    [row[0] for row in trend], [row[2] for row in trend],
    color="#377eb8", linewidth=1.2, alpha=0.75,
    label=f"Trend: {trend[-1][1]}-step change",
)
ax_change.semilogy(
    [row[0] for row in formal], [row[2] for row in formal],
    "o-", color="#d95f02", markersize=3.8, linewidth=1.4,
    label=f"Formal check: {formal[-1][1]}-step change",
)
ax_change.axhline(
    tolerance, color="#333333", linestyle="--", linewidth=1.1,
    label=f"Tolerance = {tolerance:.0e} ({formal[-1][7]} consecutive checks)",
)
for restart_step, label in (() if args.logs else (
    (96000, "Restart: 596532 at step 96000"),
    (192000, "Restart: 596888 at step 192000"),
)):
    ax_change.axvline(
        restart_step, color="#777777", linestyle=":", linewidth=1.1,
        label=label,
    )
ax_change.set_ylabel(r"Relative change $\|u^n-u^{n-k}\|_2/\|u^n\|_2$")
ax_change.grid(True, which="both", alpha=0.25)
ax_change.legend(fontsize=9)

ax_norm.plot(
    [row[0] for row in trend], [row[3] for row in trend],
    color="#4daf4a", linewidth=1.3,
)
ax_norm.set_xlabel("Coarse step")
ax_norm.set_ylabel(r"Level-0 velocity $L_2$ norm")
ax_norm.grid(True, alpha=0.25)

fig.suptitle("BOX3D_OSI Re=3200 convergence trend — steps 0–288000"
             if args.logs is None else "BOX3D_OSI convergence trend")
fig.text(
    0.5, 0.025,
    f"Sources: {', '.join(str(path) for path in logs)}\n"
    f"Steps {trend[0][0]}–{trend[-1][0]}; level-0 relative velocity L2 change; "
    f"intervals {trend[-1][1]}/{formal[-1][1]}; "
    "reference: tolerance from final formal log record",
    ha="center", fontsize=7,
)
fig.tight_layout(rect=(0, 0.07, 1, 0.96))
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=220)

minimum = min(formal, key=lambda row: row[2])
final = formal[-1]
print(f"trend points: {len(trend)}, formal points: {len(formal)}")
print(f"formal minimum: step={minimum[0]}, value={minimum[2]:.9e}")
print(f"formal final: step={final[0]}, value={final[2]:.9e}")
print(f"final/tolerance: {final[2] / tolerance:.6e}")
print(f"converged checks: {sum(row[8] for row in formal)}")
print(args.output)
print(csv_output)
