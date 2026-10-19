#!/usr/bin/env python3
"""从日志、已保存 CSV 或受限格式 PlotFile 绘制 Level-0 速度收敛趋势。"""

import argparse
import csv
import json
import re
import textwrap
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from convergence_logs import read_records


def read_velocity(plot):
    # 保留原入口的严格格式限制，不把专用二进制读取器推广为通用 AMReX 读取器。
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


def read_plotfile_records(run, interval=3200):
    """按原公式重建差值，返回趋势记录；不生成正式收敛状态。"""
    plots = sorted((p for p in run.glob("plt_[0-9]*") if p.name[4:].isdigit()), key=lambda p: int(p.name[4:]))
    if len(plots) < 2:
        raise ValueError("至少需要两个 PlotFile")
    rows, previous, previous_step = [], None, None
    for plot in plots:
        step = int(plot.name[4:])
        velocity = read_velocity(plot)
        norm = np.sqrt(np.sum(velocity * velocity))
        if previous is not None:
            delta = velocity - previous
            delta_norm = np.sqrt(np.sum(delta * delta))
            rows.append((step, step - previous_step, delta_norm / max(norm, np.finfo(float).tiny), norm, delta_norm))
        previous, previous_step = velocity, step
    if {row[1] for row in rows} != {interval}:
        raise ValueError(f"PlotFile 需要连续 {interval} 步间隔，拒绝跨越缺失文件")
    return rows, []


def read_csv_records(paths):
    """读取旧六列日志导出或五列重建 CSV；缺少正式状态时不补造 tolerance/converged。"""
    records = {"trend": {}, "formal": {}}
    required = ["step", "interval", "velocity_l2_relative", "velocity_l2", "delta_velocity_l2"]
    for path in paths:
        with path.open(newline="", encoding="utf-8-sig") as stream:
            reader = csv.DictReader(stream)
            if not set(required).issubset(reader.fieldnames or []):
                raise ValueError(f"{path}: 收敛 CSV 缺少必需字段")
            for row in reader:
                kind = row.get("record", "trend")
                if kind not in records:
                    raise ValueError(f"{path}: 未知记录类型 {kind}")
                record = (int(row["step"]), int(row["interval"]), *(float(row[key]) for key in required[2:]))
                if record[0] < 0 or record[1] <= 0 or not np.isfinite(record).all():
                    raise ValueError(f"{path}: 步数、间隔或指标无效")
                records[kind][record[0]] = record
    if not records["trend"]:
        raise ValueError("CSV 中没有趋势记录")
    return tuple([values[step] for step in sorted(values)] for values in records.values())


def plot_records(trend, formal, output, *, tolerance, sources, layout="relative-and-norm",
                 title="BOX3D_OSI convergence trend", restart_steps=(), dpi=220):
    """统一呈现不同来源的同一定义记录，保留正式记录与重建趋势的区别。"""
    two_panels = layout == "relative-and-norm"
    if two_panels:
        fig, axes = plt.subplots(2, 1, figsize=(10, 7.5), sharex=True, gridspec_kw={"height_ratios": [2.2, 1]})
        ax = axes[0]
        axes[1].plot([r[0] for r in trend], [r[3] for r in trend], color="#4daf4a", lw=1.3)
        axes[1].set(xlabel="Coarse step", ylabel="Level-0 velocity L2 norm")
        axes[1].grid(alpha=0.25)
    else:
        fig, ax = plt.subplots(figsize=(9.5, 5.5))
        ax.set_xlabel("Coarse step")
    trend_intervals = sorted({r[1] for r in trend})
    formal_intervals = sorted({r[1] for r in formal})
    ax.semilogy([r[0] for r in trend], [r[2] for r in trend], color="#377eb8", lw=1.2,
                alpha=0.75, label=f"Trend intervals: {trend_intervals}")
    if formal:
        ax.semilogy([r[0] for r in formal], [r[2] for r in formal], "o-", color="#d95f02",
                    ms=3.8, lw=1.4, label=f"Formal record intervals: {formal_intervals}")
    ax.axhline(tolerance, color="#333333", linestyle="--", lw=1.1, label=f"Reference tolerance = {tolerance:g}")
    for step in sorted(set(restart_steps)):
        ax.axvline(step, color="#777777", linestyle=":", lw=1.1, label=f"Restart step {step}")
    ax.set_ylabel(r"Relative change $\|u^n-u^{n-k}\|_2/\|u^n\|_2$")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=9)
    fig.suptitle(title)
    caption = (f"Sources: {sources}; steps {trend[0][0]}--{trend[-1][0]}; Level-0 velocity L2 change, "
               f"normalized by current velocity L2 norm; intervals {trend_intervals}/{formal_intervals}; "
               f"reference tolerance {tolerance:g}. A trend/threshold crossing does not establish runtime convergence.")
    footer = textwrap.fill(caption, width=145)
    fig.text(0.5, 0.015, footer, ha="center", fontsize=7)
    fig.tight_layout(rect=(0, min(0.3, 0.035 + 0.016 * len(footer.splitlines())), 1, 0.96))
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=dpi)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--log", type=Path, action="append", help="按顺序合并日志，后传入日志覆盖同一步")
    source.add_argument("--csv", type=Path, action="append", dest="csv_inputs", help="读取已保存的收敛 CSV")
    source.add_argument("--plotfile-run", type=Path, help="含受限格式 plt_* 的运行目录")
    parser.add_argument("--plotfile-interval", type=int, default=3200)
    parser.add_argument("--tolerance", type=float, help="日志默认取最后正式记录；CSV/PlotFile 必须显式给定")
    parser.add_argument("--restart-step", type=int, action="append", default=[])
    parser.add_argument("--layout", choices=["relative", "relative-and-norm"], default="relative-and-norm")
    parser.add_argument("--title", default="BOX3D_OSI convergence trend")
    parser.add_argument("--dpi", type=int, default=220)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--csv-output", type=Path)
    args = parser.parse_args()
    if args.plotfile_interval <= 0 or args.dpi <= 0 or any(step < 0 for step in args.restart_step):
        parser.error("间隔、DPI 必须为正数，重启步数必须非负")
    inputs = args.log or args.csv_inputs or [args.plotfile_run]
    csv_output = args.csv_output or args.output.with_suffix(".csv")
    if csv_output.resolve() == args.output.resolve() or any(p.resolve() in {args.output.resolve(), csv_output.resolve()} for p in inputs):
        parser.error("输出必须互相独立且不能覆盖输入")
    if args.log:
        trend, formal = read_records(args.log)
        tolerance = formal[-1][5] if args.tolerance is None else args.tolerance
    else:
        if args.tolerance is None:
            parser.error("CSV 和 PlotFile 数据源须显式给定 --tolerance，不推测正式收敛配置")
        tolerance = args.tolerance
        trend, formal = read_csv_records(args.csv_inputs) if args.csv_inputs else read_plotfile_records(args.plotfile_run, args.plotfile_interval)
    if not np.isfinite(tolerance) or tolerance <= 0 or not all(np.isfinite(row[:5]).all() for row in [*trend, *formal]):
        parser.error("阈值须为正有限数，所有记录指标须有限")
    source_description = ", ".join(str(p.resolve()) for p in inputs)
    plot_records(trend, formal, args.output, tolerance=tolerance, sources=source_description,
                 layout=args.layout, title=args.title, restart_steps=args.restart_step, dpi=args.dpi)
    csv_output.parent.mkdir(parents=True, exist_ok=True)
    with csv_output.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["record", "step", "interval", "velocity_l2_relative", "velocity_l2", "delta_velocity_l2"])
        for kind, rows in [("trend", trend), ("formal", formal)]:
            writer.writerows((kind, *row[:5]) for row in rows)
    final = formal[-1] if formal else trend[-1]
    summary = {"trend_points": len(trend), "formal_points": len(formal), "final_step": final[0],
               "final_relative": float(final[2]), "plot_tolerance": tolerance, "final_over_tolerance": float(final[2] / tolerance),
               "runtime_last_formal_converged": bool(formal[-1][8]) if args.log else None,
               "runtime_tolerance": formal[-1][5] if args.log else None,
               "convergence_verdict": "Runtime formal flag reported only for log input; no inference from CSV or PlotFile",
               "output": str(args.output), "csv_output": str(csv_output)}
    print(json.dumps(summary, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
