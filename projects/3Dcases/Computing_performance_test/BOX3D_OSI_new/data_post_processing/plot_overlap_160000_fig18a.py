#!/usr/bin/env python3
"""Overlay BOX3D_OSI step-160000 centerline profile with Jaber Fig.18(a) data.

Reads 160000_3D.csv (ParaView centerline export at x=y=64), aggregates
duplicate z rows by mean, normalizes u by u_lid = 0.05 and z by domain
height = 128, and overlays the two machine-extracted reference series
from fig18a_velocity_profiles.csv (AMR_triangle, Cortes_and_Miller_circle).
"""
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent

U_LID = 0.05
DOMAIN_HEIGHT = 128.0

SIM_CSV = HERE / "160000_3D.csv"
FIG18A_CSV = HERE / "fig18a_velocity_profiles.csv"
OUT = HERE / "Re1000_3D_overlap_160000_fig18a.png"

# fig18a 机器提取参考系列配色
REF_STYLE = {
    "AMR_triangle": {"color": "#d62728", "label": "Fig.18(a) AMR triangle"},
    "Cortes_and_Miller_circle": {"color": "#1f77b4", "label": "Fig.18(a) Cortes & Miller circle"},
}


def read_sim(path: Path):
    """Aggregate duplicate z rows by mean and normalize to z/H, u/u_lid."""
    by_z = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            try:
                z = float(row["Points_2"])
                ux = float(row["ux"])
            except (ValueError, KeyError):
                continue
            by_z[round(z, 6)].append(ux)
    pts = sorted((z, sum(v) / len(v)) for z, v in by_z.items())
    return [(z / DOMAIN_HEIGHT, ux / U_LID) for z, ux in pts]


def read_fig18a(path: Path):
    """Return {series: [(z/H, u/u_lid), ...]} already in normalized coords."""
    out = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            series = row.get("series", "").strip()
            if not series:
                continue
            try:
                z = float(row["z"])
                u = float(row["u_over_ulid"])
            except (ValueError, KeyError):
                continue
            out[series].append((z, u))
    return {k: sorted(v) for k, v in out.items()}


sim = read_sim(SIM_CSV)
refs = read_fig18a(FIG18A_CSV)

fig, ax = plt.subplots(figsize=(6.0, 6.5))

# 仿真曲线
sim_z, sim_u = zip(*sim)
ax.plot(sim_u, sim_z, "-", color="#2ca02c", lw=1.6,
        label=f"BOX3D_OSI, step 160000 ({len(sim)} pts)")

# fig18a 参考系列（仅画 REF_STYLE 中列出的）
for series, style in REF_STYLE.items():
    if series not in refs:
        continue
    pts = refs[series]
    z_h, u_ul = zip(*pts)
    ax.plot(u_ul, z_h, "-o", color=style["color"], ms=6.0, lw=1.5,
            alpha=0.85, label=f"{style['label']} ({len(pts)} pts)")

ax.set_xlabel(r"$u/u_{lid}$")
ax.set_ylabel(r"$z/H$")
ax.set_xlim(-1, 1)
ax.set_ylim(0.0, 1.0)
ax.set_title("BOX3D_OSI step 160000 vs Jaber Fig.18(a) (Re=1000)")
ax.grid(True, alpha=0.3)
ax.legend(fontsize=9, loc="upper left")
fig.tight_layout()
fig.savefig(OUT, dpi=300)
print(f"wrote: {OUT}")
print(f"simulation: n={len(sim)}, "
      f"u/u_lid range=[{min(u for _, u in sim):.4f}, "
      f"{max(u for _, u in sim):.4f}]")
for series, pts in refs.items():
    print(f"  {series}: n={len(pts)}, "
          f"u range=[{min(u for _, u in pts):.4f}, "
          f"{max(u for _, u in pts):.4f}]")
