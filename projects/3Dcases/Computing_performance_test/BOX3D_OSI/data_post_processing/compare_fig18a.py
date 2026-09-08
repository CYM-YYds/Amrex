#!/usr/bin/env python3
import csv
from pathlib import Path

import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent

def read_manual():
    with (HERE / "paper_fig18a.csv").open(newline="") as f:
        rows = list(csv.DictReader(f))
    return [(float(r["z_over_H"]), float(r["u_over_ulid"])) for r in rows]

def read_machine():
    out = {}
    with (HERE / "fig18a_velocity_profiles.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            out.setdefault(r["series"], []).append((float(r["z"]), float(r["u_over_ulid"])))
    return {k: sorted(v) for k, v in out.items()}

def interp(points, z):
    for (z0, u0), (z1, u1) in zip(points, points[1:]):
        if z0 <= z <= z1:
            t = (z - z0) / (z1 - z0)
            return u0 + t * (u1 - u0)
    return None

manual = sorted(read_manual())
machine = read_machine()
fig, ax = plt.subplots(figsize=(6, 6.5))
ax.plot([u for z, u in manual], [z for z, u in manual], "k-", lw=1.8, label="Manual extraction")
colors = {"AMR_triangle": "#d62728", "Cortes_and_Miller_circle": "#1f77b4"}
for name, points in machine.items():
    ax.scatter([u for z, u in points], [z for z, u in points], s=18, alpha=.8,
               color=colors.get(name), label=f"Machine: {name} ({len(points)} pts)")
    errors = []
    for z, u in points:
        ref = interp(manual, z)
        if ref is not None:
            errors.append(abs(u - ref))
    print(f"{name}: n={len(points)}, MAE={sum(errors)/len(errors):.6g}, maxAE={max(errors):.6g}")
ax.set_xlabel(r"$u/u_{lid}$")
ax.set_ylabel(r"$z/H$")
ax.set_xlim(-.35, 1.05); ax.set_ylim(0, 1)
ax.grid(True, alpha=.3); ax.legend(fontsize=8); fig.tight_layout()
out = HERE / "Re1000_fig18a_manual_vs_machine.png"
fig.savefig(out, dpi=300)
print(out)
