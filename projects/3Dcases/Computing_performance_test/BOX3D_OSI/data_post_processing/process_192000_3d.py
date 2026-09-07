#!/usr/bin/env python3
"""Split and compare the paper and BOX3D_OSI centerline data."""
import csv
from pathlib import Path

HERE = Path(__file__).resolve().parent
source = HERE / "192000_3D.csv"
paper_out = HERE / "paper_fig18a.csv"
sim_out = HERE / "simulation_192000.csv"
figure_out = HERE / "Re1000_3D_comparison.png"

paper = []
simulation = []
with source.open(newline="", encoding="utf-8-sig") as stream:
    for row in csv.reader(stream):
        if len(row) < 7:
            continue
        try:
            x, y, z, ux = (float(row[i]) for i in range(4))
        except ValueError:
            continue
        if row[5].strip() and row[6].strip():
            try:
                paper.append((float(row[5]), float(row[6])))
            except ValueError:
                pass
        simulation.append((z, ux))

paper.sort(key=lambda item: item[1])
domain_height = max(z for z, _ in simulation)
simulation = [(z / domain_height, ux / 0.05) for z, ux in simulation]
simulation.sort(key=lambda item: item[0])

with paper_out.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.writer(stream)
    writer.writerow(["u_over_ulid", "z_over_H"])
    writer.writerows(paper)
with sim_out.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.writer(stream)
    writer.writerow(["z_over_H", "u_over_ulid"])
    writer.writerows(simulation)

paper_u, paper_z = zip(*paper)
sim_z, sim_u = zip(*simulation)
try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError:
    print("matplotlib unavailable; CSV files were generated, skip PNG")
    print(f"paper points: {len(paper)}")
    print(f"simulation points: {len(simulation)}")
    raise SystemExit(0)
plt.figure(figsize=(5.2, 6.0))
plt.plot(paper_u, paper_z, "k-", linewidth=1.5, label="Jaber Fig.18(a)")
plt.plot(sim_u, sim_z, "r.", markersize=2.0, label="BOX3D_OSI, step 192000")
plt.xlabel(r"$u/u_{lid}$")
plt.ylabel(r"$z/H$")
plt.xlim(-0.35, 1.05)
plt.ylim(0.0, 1.0)
plt.grid(True, alpha=0.3)
plt.legend()
plt.tight_layout()
plt.savefig(figure_out, dpi=300)
print(f"paper points: {len(paper)}")
print(f"simulation points: {len(simulation)}")
print(f"domain height used for normalization: {domain_height}")
print(f"wrote: {paper_out.name}, {sim_out.name}, {figure_out.name}")
