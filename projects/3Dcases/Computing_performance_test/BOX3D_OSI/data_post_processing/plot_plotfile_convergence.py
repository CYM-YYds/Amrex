"""从单块 Level-0 PlotFile 重建速度收敛趋势。"""

import argparse
import csv
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def read_velocity(plot):
    # 只接受本次算例的无幽灵单块、三分量、双精度格式，避免静默误读。
    header = (plot / "Header").read_text().splitlines()
    if header[1:6] != ["3", "ux", "uy", "uz", "3"]:
        raise ValueError(f"变量或维度不符: {plot}")
    cell_header = (plot / "Level_0/Cell_H").read_text()
    records = re.findall(r"FabOnDisk: (\S+) (\d+)", cell_header)
    if len(records) != 1 or cell_header.splitlines()[:4] != ["1", "1", "3", "0"]:
        raise ValueError(f"仅支持单块无幽灵 Level-0: {plot}")
    filename, offset = records[0]
    with (plot / "Level_0" / filename).open("rb") as stream:
        stream.seek(int(offset))
        fab = stream.readline().decode("ascii").strip()
        expected = "FAB ((8, (64 11 52 0 1 12 0 1023)),(8, (8 7 6 5 4 3 2 1)))((0,0,0) (127,127,127) (0,0,0)) 3"
        if fab != expected:
            raise ValueError(f"未支持的 FAB 布局: {fab}")
        values = np.fromfile(stream, dtype="<f8", count=3 * 128**3)
    if values.size != 3 * 128**3 or not np.isfinite(values).all():
        raise ValueError(f"速度数据不完整或非有限: {plot}")
    extrema = re.findall(r"1,3\s*\n([^\n]+)", cell_header)
    components = values.reshape(3, -1)
    for stored, actual in zip(extrema, (components.min(axis=1), components.max(axis=1))):
        expected_extrema = np.array([float(v) for v in stored.split(",") if v])
        if not np.allclose(actual, expected_extrema, rtol=1e-12, atol=1e-15):
            raise ValueError(f"二进制数据与 Cell_H 极值不符: {plot}")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    plots = sorted(args.run.glob("plt_[0-9]*"), key=lambda p: int(p.name[4:]))
    if len(plots) < 2:
        raise ValueError("至少需要两个 PlotFile")
    overrides = dict(token.split("=", 1) for token in (args.run / "overrides.txt").read_text().split())
    reynolds = float(overrides["lbm.reynolds_number"])
    rows = []
    previous = None
    previous_step = None
    for plot in plots:
        step = int(plot.name[4:])
        velocity = read_velocity(plot)
        norm = np.sqrt(np.sum(velocity * velocity))
        if previous is not None:
            delta = velocity - previous
            delta_norm = np.sqrt(np.sum(delta * delta))
            rows.append((step, step - previous_step, delta_norm / max(norm, np.finfo(float).tiny), norm, delta_norm))
        previous, previous_step = velocity, step
    data = np.array(rows)
    if set(data[:, 1]) != {3200}:
        raise ValueError("当前绘图要求连续 3200 步间隔")
    tolerance = 1e-4
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.with_suffix(".csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["step", "interval", "velocity_l2_relative", "velocity_l2", "delta_velocity_l2"])
        writer.writerows(rows)
    fig, axes = plt.subplots(2, 1, figsize=(11, 8), sharex=True, gridspec_kw={"height_ratios": [2, 1]})
    axes[0].semilogy(data[:, 0], data[:, 2], "o-", markersize=3, label="Level-0 velocity change (3200 steps)")
    axes[0].axhline(tolerance, linestyle="--", color="gray", label="Configured reference tolerance: 1e-4")
    axes[0].set_ylabel(r"$\|u^n-u^{n-3200}\|_2 / \|u^n\|_2$")
    axes[0].legend()
    axes[1].plot(data[:, 0], data[:, 3], color="green")
    axes[1].set_ylabel("Level-0 velocity L2 norm")
    axes[1].set_xlabel("Coarse step")
    for ax in axes:
        ax.grid(True, which="both", alpha=0.25)
    fig.suptitle(f"BOX3D_OSI Re={reynolds:g} — job {args.run.name.split('job')[-1]}\nConvergence reconstructed from saved Level-0 velocity")
    fig.text(0.5, 0.015, f"Input: runs/{args.run.name}/plt_*/Level_0/Cell\n"
             f"Steps {int(data[0, 0])}–{int(data[-1, 0])}; all 128^3 cells and ux, uy, uz; normalized by current velocity L2 norm.\n"
             "Reference: inputs tolerance 1e-4, 3 consecutive checks; runtime convergence monitoring was disabled.",
             ha="center", fontsize=8)
    fig.tight_layout(rect=(0, 0.085, 1, 0.96))
    fig.savefig(args.output, dpi=200)
    plt.close(fig)
    print(f"PlotFiles={len(plots)}, differences={len(rows)}")
    print(f"Final={rows[-1]}; minimum={rows[int(np.argmin(data[:, 2]))]}")
    print(f"Last 10 relative changes: {data[-10:, 2].tolist()}")
    print(f"Intervals: {sorted(set(data[:, 1]))}; checks below tolerance: {np.count_nonzero(data[:, 2] < tolerance)}")
    print(args.output)


if __name__ == "__main__":
    main()
