#!/usr/bin/env python3
"""Overlay BOX3D_OSI centerline velocity profiles at multiple time steps.

Reads 64000_3D.csv, 96000_3D.csv and 160000_3D.csv (ParaView centerline
exports at x=y=64), aggregates duplicate z rows by mean, normalizes
u by u_lid = 0.05 and z by domain height = 128, and plots the three
profiles together to compare the 96000 -> 160000 restart evolution.
"""
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
CASE = HERE.parent

# 仿真参数（与 process_192000_3d.py 一致）
U_LID = 0.05
DOMAIN_HEIGHT = 128.0

STEPS = [
    (64000, HERE / "64000_3D.csv", "#1f77b4"),
    (96000, HERE / "96000_3D.csv", "#d62728"),
    (160000, HERE / "160000_3D.csv", "#2ca02c"),
]
OUT = HERE / "Re1000_3D_overlap_64000_96000_reference.png"


def read_profile(path: Path):
    """Aggregate duplicate z rows by mean and normalize."""
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


fig, ax = plt.subplots(figsize=(6.0, 6.5))
for step, path, color in STEPS:
    if not path.exists():
        raise SystemExit(f"Missing CSV: {path}")
    profile = read_profile(path)
    z_h, u_ul = zip(*profile)
    ax.plot(u_ul, z_h, "-", color=color, lw=1.4, ms=2.5,
            label=f"BOX3D_OSI, step {step} ({len(profile)} pts)")

ax.set_xlabel(r"$u/u_{lid}$")
ax.set_ylabel(r"$z/H$")
ax.set_xlim(-0.35, 1.05)
ax.set_ylim(0.0, 1.0)
ax.set_title("BOX3D_OSI centerline $u_x$ profile (Re=1000)\n"
             "steps 64000 / 96000 / 160000")
ax.grid(True, alpha=0.3)
ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(OUT, dpi=300)
print(f"wrote: {OUT}")
for step, path, _ in STEPS:
    profile = read_profile(path)
    umin = min(u for _, u in profile)
    umax = max(u for _, u in profile)
    print(f"  step {step}: n={len(profile)} "
          f"u/u_lid range=[{umin:.4f}, {umax:.4f}]")
