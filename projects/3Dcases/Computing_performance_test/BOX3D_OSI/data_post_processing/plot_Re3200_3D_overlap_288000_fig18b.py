#!/usr/bin/env python3
"""叠加 BOX3D_OSI step 288000 中心线剖面与 Jaber Fig.18(b) 数据。"""

import csv
import math
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


HERE = Path(__file__).resolve().parent
U_LID = 0.05
DOMAIN_HEIGHT = 128.0

SIM_CSV = HERE / "Re3200_288000.csv"
REFERENCE_CSV = HERE / "fig18b_velocity_profiles.csv"
OUT = HERE / "Re3200_3D_overlap_288000_fig18b.png"
ERRORS_OUT = HERE / "Re3200_288000_fig18b_errors.csv"

REFERENCE_STYLE = {
    "AMR_stream": {
        "color": "#d62728", "marker": "^", "fillstyle": "none",
        "label": "Fig.18(b) AMR",
    },
    "Cortes_and_Miller_stream": {
        "color": "#1f77b4", "marker": "o", "fillstyle": "none",
        "label": "Cortes & Miller",
    },
    "Experimental_stream": {
        "color": "#222222", "marker": "^", "fillstyle": "full",
        "label": "Experiment",
    },
}


def read_simulation(path: Path):
    """按 z 聚合重复行，并归一化为 (z/H, u/u_lid)。"""
    by_z = defaultdict(list)
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            try:
                z = float(row["Points_2"])
                ux = float(row["ux"])
            except (KeyError, ValueError):
                continue
            by_z[round(z, 9)].append(ux)
    points = sorted((z / DOMAIN_HEIGHT, sum(values) / len(values) / U_LID)
                    for z, values in by_z.items())
    if len(points) < 2:
        raise SystemExit(f"Insufficient simulation points in {path}")
    return points


def read_references(path: Path):
    """读取已经处于无量纲坐标的 Fig.18(b) 三组数据。"""
    series = defaultdict(list)
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            name = row.get("series", "").strip()
            if name not in REFERENCE_STYLE:
                continue
            try:
                z_h = float(row["z"])
                u_ulid = float(row["u_over_ulid"])
            except (KeyError, ValueError):
                continue
            series[name].append((z_h, u_ulid))
    return {name: sorted(points) for name, points in series.items()}


def interpolate(points, x):
    """在有序 (x, y) 点列上线性插值。"""
    if not points[0][0] <= x <= points[-1][0]:
        raise ValueError(f"Interpolation coordinate outside range: {x}")
    for left, right in zip(points, points[1:]):
        if left[0] <= x <= right[0]:
            if right[0] == left[0]:
                return 0.5 * (left[1] + right[1])
            weight = (x - left[0]) / (right[0] - left[0])
            return left[1] + weight * (right[1] - left[1])
    return points[-1][1]


def error_metrics(simulation, reference):
    differences = [interpolate(simulation, z_h) - u_ref
                   for z_h, u_ref in reference]
    return {
        "n": len(differences),
        "mae": sum(abs(value) for value in differences) / len(differences),
        "rmse": math.sqrt(sum(value * value for value in differences) / len(differences)),
        "max_abs": max(abs(value) for value in differences),
    }


simulation = read_simulation(SIM_CSV)
references = read_references(REFERENCE_CSV)
metrics = {name: error_metrics(simulation, points)
           for name, points in references.items()}

with ERRORS_OUT.open("w", newline="") as stream:
    writer = csv.writer(stream)
    writer.writerow(["reference", "n", "mae", "rmse", "max_abs_error"])
    for name in REFERENCE_STYLE:
        values = metrics[name]
        writer.writerow([name, values["n"], values["mae"],
                         values["rmse"], values["max_abs"]])

fig, ax = plt.subplots(figsize=(6.4, 7.0))

sim_z, sim_u = zip(*simulation)
ax.plot(sim_u, sim_z, color="#2ca02c", linewidth=1.8,
        label=f"BOX3D_OSI step 288000 ({len(simulation)} pts)")

for name, style in REFERENCE_STYLE.items():
    points = references[name]
    z_h, u_ulid = zip(*points)
    ax.plot(
        u_ulid, z_h, linestyle="-", linewidth=0.9,
        color=style["color"], marker=style["marker"], markersize=4.2,
        fillstyle=style["fillstyle"], markeredgewidth=1.0, alpha=0.9,
        label=f"{style['label']} ({len(points)} pts)",
    )

ax.set_xlabel(r"$u/u_{lid}$")
ax.set_ylabel(r"$z/H$")
ax.set_xlim(-1, 1)
ax.set_ylim(0.0, 1.0)
ax.set_title("BOX3D_OSI step 288000 vs Jaber Fig.18(b) (Re=3200)")
ax.grid(True, alpha=0.3)
ax.legend(fontsize=8.7, loc="upper left")
fig.text(
    0.5, 0.012,
    "Simulation: Re3200_288000.csv; normalization: z/H = Points_2/128, "
    "u/u_lid = ux/0.05",
    ha="center", fontsize=7.8,
)
fig.tight_layout(rect=(0, 0.035, 1, 1))
fig.savefig(OUT, dpi=300)

print(f"wrote: {OUT}")
print(f"wrote: {ERRORS_OUT}")
print(f"simulation: raw rows aggregated to {len(simulation)} unique z points")
for name in REFERENCE_STYLE:
    values = metrics[name]
    print(f"{name}: n={values['n']}, MAE={values['mae']:.8f}, "
          f"RMSE={values['rmse']:.8f}, max_abs={values['max_abs']:.8f}")
