#!/usr/bin/env python3
"""绘制 Re=3200 三段连续计算到 288000 步的收敛趋势。"""

import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt


CASE = Path(__file__).resolve().parents[1]
LOGS = [
    CASE / "logs" / "submit" / "596154-out.log",
    CASE / "logs" / "submit" / "596532-out.log",
    CASE / "runs" / "20260915_210444_job596888" / "run.log",
]
OUT = CASE / "data_post_processing" / "596154_Re3200_convergence.png"
CSV_OUT = CASE / "data_post_processing" / "596154_Re3200_convergence.csv"

TREND = re.compile(
    r"CONVERGENCE_TREND step=(\d+) interval=(\d+) "
    r"velocity_l2_relative=([0-9.eE+-]+) velocity_l2=([0-9.eE+-]+) "
    r"delta_velocity_l2=([0-9.eE+-]+)"
)
FORMAL = re.compile(
    r"CONVERGENCE step=(\d+) interval=(\d+) "
    r"velocity_l2_relative=([0-9.eE+-]+) velocity_l2=([0-9.eE+-]+) "
    r"delta_velocity_l2=([0-9.eE+-]+) tolerance=([0-9.eE+-]+) "
    r"consecutive=(\d+)/(\d+) converged=(\d+)"
)


def read_records():
    # 续跑日志可能与前一段在重启步附近重叠，按绝对 step 去重。
    trend_by_step = {}
    formal_by_step = {}
    for log in LOGS:
        if not log.exists():
            raise SystemExit(f"Missing log: {log}")
        for line in log.read_text(errors="replace").splitlines():
            match = TREND.search(line)
            if match:
                step, interval, relative, velocity, delta = match.groups()
                trend_by_step[int(step)] = (
                    int(step), int(interval), float(relative), float(velocity), float(delta)
                )
            match = FORMAL.search(line)
            if match:
                step, interval, relative, velocity, delta, tolerance, count, required, converged = match.groups()
                formal_by_step[int(step)] = (
                    int(step), int(interval), float(relative), float(velocity),
                    float(delta), float(tolerance), int(count), int(required),
                    bool(int(converged)),
                )
    trend_records = [trend_by_step[step] for step in sorted(trend_by_step)]
    formal_records = [formal_by_step[step] for step in sorted(formal_by_step)]
    if not trend_records or not formal_records:
        raise SystemExit("No convergence records found")
    return trend_records, formal_records


trend, formal = read_records()
tolerance = formal[-1][5]

with CSV_OUT.open("w", newline="") as stream:
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
    color="#377eb8", linewidth=1.2, alpha=0.75, label="Trend: 1000-step change",
)
ax_change.semilogy(
    [row[0] for row in formal], [row[2] for row in formal],
    "o-", color="#d95f02", markersize=3.8, linewidth=1.4,
    label="Formal check: 3200-step change",
)
ax_change.axhline(
    tolerance, color="#333333", linestyle="--", linewidth=1.1,
    label=f"Tolerance = {tolerance:.0e} (3 consecutive checks)",
)
for restart_step, label in (
    (96000, "Restart: 596532 at step 96000"),
    (192000, "Restart: 596888 at step 192000"),
):
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

fig.suptitle("BOX3D_OSI Re=3200 convergence trend — steps 0–288000")
fig.text(
    0.5, 0.012,
    "Sources: jobs 596154 + 596532 + 596888; steps 0–288000; "
    "non-periodic; levels 0–2; "
    "relative changes use 1000/3200-step intervals",
    ha="center", fontsize=8,
)
fig.tight_layout(rect=(0, 0.035, 1, 0.96))
fig.savefig(OUT, dpi=220)

minimum = min(formal, key=lambda row: row[2])
final = formal[-1]
print(f"trend points: {len(trend)}, formal points: {len(formal)}")
print(f"formal minimum: step={minimum[0]}, value={minimum[2]:.9e}")
print(f"formal final: step={final[0]}, value={final[2]:.9e}")
print(f"final/tolerance: {final[2] / tolerance:.6e}")
print(f"converged checks: {sum(row[8] for row in formal)}")
print(OUT)
print(CSV_OUT)
