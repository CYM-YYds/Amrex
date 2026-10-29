#!/usr/bin/env python3
"""按参数绘制一条或多条中心线 CSV，与无量纲参考数据对比并导出指标。"""

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import numpy as np


def read_profile(path, *, require_mask=True, z_column=None, velocity_column="ux", round_digits=None):
    """聚合重复坐标；有掩码时拒绝跨越内部无效采样点，旧 CSV 必须显式允许无掩码。"""
    groups = defaultdict(list)
    invalid_z = []
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        names = reader.fieldnames or []
        zkey = z_column or next((key for key in ("Points:2", "Points_2", "Points_Z") if key in names), None)
        has_mask = "vtkValidPointMask" in names
        if zkey not in names or velocity_column not in names or (require_mask and not has_mask):
            raise ValueError(f"{path}: 缺少坐标、速度或采样掩码；旧 CSV 可显式选择 --allow-unmasked")
        for number, row in enumerate(reader, 2):
            z = float(row[zkey])
            if not np.isfinite(z):
                raise ValueError(f"{path}:{number}: 坐标非有限")
            if has_mask and float(row["vtkValidPointMask"]) != 1:
                invalid_z.append(z)
                continue
            velocity = float(row[velocity_column])
            if not np.isfinite(velocity):
                raise ValueError(f"{path}:{number}: 速度非有限")
            key = round(z, round_digits) if round_digits is not None else z
            groups[key].append(velocity)
    if len(groups) < 2:
        raise ValueError(f"{path}: 有效采样点不足")
    if any(min(groups) < z < max(groups) for z in invalid_z):
        raise ValueError(f"{path}: 中心线内部存在无效采样，拒绝跨越缺口插值")
    return np.array([(z, np.mean(values)) for z, values in sorted(groups.items())]), len(invalid_z)


def normalize_profile(points, height, lid_velocity, origin=0.0):
    """归一化为 (z/H, u/U_lid)，长度和速度标度由调用方明确指定。"""
    if not np.isfinite([height, lid_velocity, origin]).all() or height <= 0 or lid_velocity <= 0:
        raise ValueError("H、U_lid 必须为正有限数，坐标原点必须有限")
    return np.column_stack(((points[:, 0] - origin) / height, points[:, 1] / lid_velocity))


def read_references(path):
    """读取已经无量纲化的参考 CSV，保留每个 series 的全部采样点。"""
    groups = defaultdict(list)
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        names = reader.fieldnames or []
        zkey = next((key for key in ("z", "z_over_H") if key in names), None)
        if zkey is None or "u_over_ulid" not in names:
            raise ValueError(f"{path}: 参考 CSV 需要 z/z_over_H 和 u_over_ulid")
        for row in reader:
            point = (float(row[zkey]), float(row["u_over_ulid"]))
            if not np.isfinite(point).all():
                raise ValueError(f"{path}: 参考数据非有限")
            groups[row.get("series") or Path(path).stem].append(point)
    if not groups:
        raise ValueError(f"{path}: 没有参考数据")
    return {name: np.array(sorted(points)) for name, points in groups.items()}


def reference_errors(simulation, reference, *, outside="error"):
    """在参考点处插值仿真曲线；范围外点只在显式 exclude 模式下排除并计数。"""
    if outside not in ("error", "exclude"):
        raise ValueError("范围策略必须为 error 或 exclude")
    mask = (reference[:, 0] >= simulation[0, 0]) & (reference[:, 0] <= simulation[-1, 0])
    if outside == "error" and not mask.all():
        raise ValueError("参考点超出有效采样范围；可显式选择 --outside-reference exclude")
    if not mask.any():
        raise ValueError("参考数据与采样范围没有交集")
    delta = np.interp(reference[mask, 0], simulation[:, 0], simulation[:, 1]) - reference[mask, 1]
    return {"n_used": int(mask.sum()), "n_excluded": int((~mask).sum()),
            "mae": float(np.mean(abs(delta))), "rmse": float(np.sqrt(np.mean(delta**2))),
            "max_abs_error": float(np.max(abs(delta)))}


def plot_profiles(profiles, references, output, *, title="Centerline velocity comparison",
                  reference_source="none", reference_labels=None, xlim=(-0.4, 1.05), ylim=(0, 1),
                  xlabel=r"$u_x/U_{lid}$", ylabel=r"$z/H$", figsize=(6.4, 6.4),
                  linewidth=1.8, markersize=6.5, dpi=220):
    """共用绘图实现；曲线、来源、步数和各自标度由调用方传入，不绑定作业号或 Re。"""
    # 延迟加载绘图库，使提取入口复用数据校验时不依赖绘图环境。
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=figsize)
    colors = ["#222222", "#0072B2", "#009E73", "#CC79A7"]
    for index, profile in enumerate(profiles):
        points = profile["points"]
        ax.plot(points[:, 1], points[:, 0], linestyle=profile.get("linestyle", "-"),
                color=profile.get("color", colors[index % len(colors)]), lw=linewidth,
                label=profile["label"])
    for index, (name, points) in enumerate(references.items()):
        label = (reference_labels or {}).get(name, {
            "AMR_triangle": "Jaber et al. (AMR)",
            "Cortes_and_Miller_circle": "Cortes & Miller",
        }.get(name, name.replace("_", " ")))
        experimental = name.startswith("Experimental")
        ax.plot(points[:, 1], points[:, 0], linestyle="none",
                marker="o" if name.startswith("Cortes") else "^",
                color=["#D55E00", "#0072B2", "#009E73"][index % 3], ms=markersize,
                markeredgewidth=1.1, fillstyle="full" if experimental else "none", label=label)
    ax.set(xlabel=xlabel, ylabel=ylabel, xlim=xlim, ylim=ylim, title=title)
    ax.grid(color="0.9", linewidth=0.6)
    ax.set_axisbelow(True)
    ax.tick_params(direction="in", top=True, right=True)
    ax.legend(fontsize=9, loc="best", frameon=False)
    # 图内只保留步数与归一化；完整输入路径、出处和误差存入同名 JSON。
    steps = "; ".join(f'{p["label"]}: n={p["step"]}' for p in profiles)
    scales = list(dict.fromkeys(
        f'H={p["normalization"]["H"]:g}, U_lid={p["normalization"]["U_lid"]:g}, '
        f'z0={p["normalization"].get("z_origin", 0):g}' for p in profiles))
    footer = steps + "\n" + "; ".join(scales)
    fig.text(0.5, 0.02, footer, ha="center", va="bottom", fontsize=8, color="0.35")
    fig.tight_layout(rect=(0, 0.09, 1, 1))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=dpi)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", nargs=3, action="append", required=True, metavar=("CSV", "LABEL", "STEP"))
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--reference-source", help="参考数据出处说明；默认记录参考 CSV 路径")
    parser.add_argument("--height", type=float, required=True)
    parser.add_argument("--lid-velocity", type=float, required=True)
    parser.add_argument("--origin", type=float, default=0)
    parser.add_argument("--z-column")
    parser.add_argument("--velocity-column", default="ux")
    parser.add_argument("--round-z", type=int, help="兼容旧曲线的坐标聚合精度；默认不舍入")
    parser.add_argument("--allow-unmasked", action="store_true", help="显式接受没有有效性掩码的旧 CSV")
    parser.add_argument("--outside-reference", choices=["error", "exclude"], default="error")
    parser.add_argument("--title", default="Centerline velocity comparison")
    parser.add_argument("--xlim", nargs=2, type=float, default=(-0.4, 1.05))
    parser.add_argument("--ylim", nargs=2, type=float, default=(0, 1))
    parser.add_argument("--xlabel", default=r"$u_x/U_{lid}$")
    parser.add_argument("--ylabel", default=r"$z/H$")
    parser.add_argument("--figsize", nargs=2, type=float, default=(6.4, 6.4), metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--linewidth", type=float, default=1.8)
    parser.add_argument("--markersize", type=float, default=6.5)
    parser.add_argument("--dpi", type=int, default=220)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.round_z is not None and args.round_z < 0:
        parser.error("--round-z 必须非负")
    if (args.dpi <= 0 or not np.isfinite([*args.xlim, *args.ylim, *args.figsize, args.linewidth, args.markersize]).all()
            or args.xlim[0] >= args.xlim[1] or args.ylim[0] >= args.ylim[1]
            or min(*args.figsize, args.linewidth, args.markersize) <= 0):
        parser.error("坐标范围必须为递增有限区间；图幅、线宽、点大小和DPI必须为正数")
    sources = [Path(spec[0]) for spec in args.profile] + ([args.reference] if args.reference else [])
    outputs = [args.output, args.output.with_suffix(".csv"), args.output.with_suffix(".errors.csv"), args.output.with_suffix(".json")]
    if any(output.resolve() in {p.resolve() for p in sources} for output in outputs):
        parser.error("输出不能覆盖输入 CSV")
    profiles, metrics = [], []
    references = read_references(args.reference) if args.reference else {}
    labels = set()
    for filename, label, step_text in args.profile:
        step = int(step_text)
        if step < 0 or label in labels:
            parser.error("步数须非负，每条曲线的标签须唯一")
        labels.add(label)
        raw, invalid = read_profile(filename, require_mask=not args.allow_unmasked, z_column=args.z_column,
                                    velocity_column=args.velocity_column, round_digits=args.round_z)
        points = normalize_profile(raw, args.height, args.lid_velocity, args.origin)
        profiles.append({"label": label, "step": step, "source": str(Path(filename).resolve()), "points": points,
                         "normalization": {"H": args.height, "U_lid": args.lid_velocity, "z_origin": args.origin},
                         "invalid_samples": invalid})
        for name, reference in references.items():
            metrics.append({"profile": label, "step": step, "reference": name,
                            **reference_errors(points, reference, outside=args.outside_reference)})
    reference_source = str(args.reference.resolve()) if args.reference else "none"
    if args.reference_source:
        reference_source += f" ({args.reference_source})"
    plot_profiles(profiles, references, args.output, title=args.title, reference_source=reference_source,
                  xlim=args.xlim, ylim=args.ylim, xlabel=args.xlabel, ylabel=args.ylabel, figsize=args.figsize,
                  linewidth=args.linewidth, markersize=args.markersize, dpi=args.dpi)
    with args.output.with_suffix(".csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["profile", "step", "z_over_H", "u_over_ulid"])
        for profile in profiles:
            writer.writerows((profile["label"], profile["step"], *point) for point in profile["points"])
    if metrics:
        with args.output.with_suffix(".errors.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(metrics[0]))
            writer.writeheader()
            writer.writerows(metrics)
    report = {"profiles": [{key: value for key, value in p.items() if key != "points"} for p in profiles],
              "plot": {"xlim": args.xlim, "ylim": args.ylim, "xlabel": args.xlabel, "ylabel": args.ylabel,
                       "figsize": args.figsize, "linewidth": args.linewidth, "markersize": args.markersize, "dpi": args.dpi},
              "reference": reference_source, "errors": metrics, "outside_reference": args.outside_reference,
              "allow_unmasked": args.allow_unmasked, "round_z": args.round_z,
              "input_hashes": {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              "convergence_verdict": "Not assessed; reference agreement is not convergence"}
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
