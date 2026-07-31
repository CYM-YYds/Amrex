#!/usr/bin/env python3
"""Create a timing overview from a complete BOX3D submit log."""

from __future__ import annotations

import argparse
import math
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/box3d-matplotlib")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties


PHASES = ("interp", "collide", "stream", "average", "comm", "boundary", "swap")
PHASE_LABELS = {
    "interp": "Interp",
    "collide": "Collide",
    "stream": "Stream",
    "average": "Average",
    "comm": "Comm",
    "boundary": "Boundary",
    "swap": "Swap",
}
PHASE_COLORS = {
    "interp": "#4C78A8",
    "collide": "#F58518",
    "stream": "#E45756",
    "average": "#54A24B",
    "comm": "#72B7B2",
    "boundary": "#B279A2",
    "swap": "#9D755D",
}
DETAILS = (
    "interp_fillpatch",
    "interp_scale",
    "average_alloc",
    "average_copy",
    "average_scale",
    "average_down",
)
DETAIL_LABELS = {
    "interp_fillpatch": "Interp: FillPatch",
    "interp_scale": "Interp: scale",
    "average_alloc": "Average: alloc",
    "average_copy": "Average: copy",
    "average_scale": "Average: scale",
    "average_down": "Average: down",
}
DETAIL_COLORS = ["#4C78A8", "#76A5D2", "#BAB0AC", "#F2B880", "#F58518", "#54A24B"]
CJK_FONT_PATH = Path("/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc")
CJK_FONT = FontProperties(fname=str(CJK_FONT_PATH)) if CJK_FONT_PATH.exists() else FontProperties()
STEP_RE = re.compile(r"step(\d+)")


@dataclass
class Window:
    step: int
    compute: float = math.nan
    regrid: float = math.nan
    solv: float = math.nan
    mlups_solv: float = math.nan
    mlups_total: float = math.nan
    phase: dict[str, float] = field(default_factory=dict)
    detail: dict[str, float] = field(default_factory=dict)
    average_scale_cells: float = math.nan


def parse_values(line: str) -> dict[str, float]:
    values: dict[str, float] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            values[key] = float(value)
        except ValueError:
            pass
    return values


def parse_log(path: Path) -> list[Window]:
    windows: dict[int, Window] = {}

    for line in path.read_text(errors="replace").splitlines():
        step_match = STEP_RE.search(line)
        if step_match is None:
            continue
        step = int(step_match.group(1))
        window = windows.setdefault(step, Window(step=step))

        if " compute_time:" in line:
            fields = line.replace(":", ": ").split()
            for index, field in enumerate(fields[:-1]):
                if field == "compute_time:":
                    window.compute = float(fields[index + 1]) / 1000.0
                elif field == "regrid_time:":
                    window.regrid = float(fields[index + 1]) / 1000.0
                elif field == "JaberCycle_time:":
                    window.solv = float(fields[index + 1]) / 1000.0
        elif " perf(s):" in line:
            values = parse_values(line)
            window.phase = {name: values[name] for name in PHASES}
            window.mlups_solv = values["MLUPS_solv"]
            window.mlups_total = values["MLUPS_total"]
        elif " perf_detail(s):" in line:
            values = parse_values(line)
            # Detail fields evolved as the interpolation/restriction paths
            # changed. Missing legacy fields are zero for plotting purposes.
            window.detail = {name: values.get(name, 0.0) for name in DETAILS}
        elif " perf_count:" in line:
            values = parse_values(line)
            window.average_scale_cells = values["average_scale_cells"]

    complete = [window for window in windows.values() if window.phase and window.detail]
    if not complete:
        raise ValueError("No complete perf(s)/perf_detail(s) windows were found.")
    return sorted(complete, key=lambda window: window.step)


def correlation(x: list[float], y: list[float]) -> float:
    x_mean = sum(x) / len(x)
    y_mean = sum(y) / len(y)
    numerator = sum((a - x_mean) * (b - y_mean) for a, b in zip(x, y))
    x_norm = math.sqrt(sum((a - x_mean) ** 2 for a in x))
    y_norm = math.sqrt(sum((b - y_mean) ** 2 for b in y))
    return numerator / (x_norm * y_norm) if x_norm and y_norm else math.nan


def plot(windows: list[Window], log_path: Path, output_path: Path) -> None:
    steps = [window.step for window in windows]
    phase_values = {name: [window.phase[name] for window in windows] for name in PHASES}
    details = {name: sum(window.detail[name] for window in windows) for name in DETAILS}
    phase_totals = {name: sum(phase_values[name]) for name in PHASES}
    solv = [window.solv for window in windows]
    compute = [window.compute for window in windows]
    regrid = [window.regrid for window in windows]
    mlups_solv = [window.mlups_solv for window in windows]
    mlups_total = [window.mlups_total for window in windows]
    average_cells = [window.average_scale_cells / 1.0e9 for window in windows]
    stream = phase_values["stream"]
    corr = correlation(average_cells, stream)

    plt.rcParams["axes.unicode_minus"] = False
    figure, axes = plt.subplots(3, 2, figsize=(16, 16), constrained_layout=True)
    figure.suptitle(
        f"BOX3D Performance Overview: {log_path.name} ({len(windows)} x 1000-step windows)",
        fontsize=16,
        fontweight="bold",
    )

    ax = axes[0, 0]
    ax.stackplot(
        steps,
        [phase_values[name] for name in PHASES],
        labels=[PHASE_LABELS[name] for name in PHASES],
        colors=[PHASE_COLORS[name] for name in PHASES],
        alpha=0.9,
    )
    ax.set_title("阶段耗时构成", fontproperties=CJK_FONT)
    ax.set_xlabel("Step")
    ax.set_ylabel("s / 1000 steps")
    ax.legend(loc="upper left", ncol=2, fontsize=8)

    ax = axes[0, 1]
    ax.plot(steps, solv, color="#4C78A8", label="JaberCycle2")
    ax.plot(steps, compute, color="#F58518", label="Compute total")
    ax.set_title("每窗口总耗时", fontproperties=CJK_FONT)
    ax.set_xlabel("Step")
    ax.set_ylabel("s / 1000 steps")
    ax.legend(loc="upper left")
    regrid_ax = ax.twinx()
    regrid_ax.plot(steps, regrid, color="#54A24B", linestyle="--", label="Regrid")
    regrid_ax.set_ylabel("Regrid s / 1000 steps", color="#54A24B")
    regrid_ax.tick_params(axis="y", labelcolor="#54A24B")

    ax = axes[1, 0]
    ax.plot(steps, mlups_solv, color="#4C78A8", label="MLUPS_solv")
    ax.plot(steps, mlups_total, color="#F58518", label="MLUPS_total")
    ax.set_title("吞吐趋势", fontproperties=CJK_FONT)
    ax.set_xlabel("Step")
    ax.set_ylabel("MLUPS")
    ax.legend(loc="lower right")
    ax.grid(alpha=0.25)

    ax = axes[1, 1]
    ax.bar(steps, average_cells, width=700, color="#72B7B2", alpha=0.8, label="average_scale_cells")
    ax.set_title(f"AMR 工作量与 Stream (Pearson r={corr:.4f})", fontproperties=CJK_FONT)
    ax.set_xlabel("Step")
    ax.set_ylabel("Average scale cells / 1e9", color="#3B7C85")
    ax.tick_params(axis="y", labelcolor="#3B7C85")
    stream_ax = ax.twinx()
    stream_ax.plot(steps, stream, color="#E45756", linewidth=1.8, label="Stream")
    stream_ax.set_ylabel("Stream s / 1000 steps", color="#C43C4E")
    stream_ax.tick_params(axis="y", labelcolor="#C43C4E")

    ax = axes[2, 0]
    phase_order = sorted(PHASES, key=phase_totals.get)
    values = [phase_totals[name] for name in phase_order]
    colors = [PHASE_COLORS[name] for name in phase_order]
    bars = ax.barh([PHASE_LABELS[name] for name in phase_order], values, color=colors)
    total_solv = sum(values)
    for bar, value in zip(bars, values):
        ax.text(value, bar.get_y() + bar.get_height() / 2, f" {value:.0f} s ({value / total_solv:.1%})", va="center")
    ax.set_title("全程 JaberCycle2 阶段累计", fontproperties=CJK_FONT)
    ax.set_xlabel("Accumulated seconds")

    ax = axes[2, 1]
    detail_order = list(DETAILS)
    values = [details[name] for name in detail_order]
    bars = ax.barh([DETAIL_LABELS[name] for name in detail_order], values, color=DETAIL_COLORS)
    for bar, value in zip(bars, values):
        ax.text(value, bar.get_y() + bar.get_height() / 2, f" {value:.0f} s", va="center")
    ax.set_title("传输子阶段累计（同步子计时，非严格可加）", fontproperties=CJK_FONT)
    ax.set_xlabel("Accumulated seconds")

    for ax in axes.flat:
        ax.spines[["top", "right"]].set_visible(False)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=200, facecolor="white")
    print(output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="BOX3D submit log containing perf(s) windows")
    parser.add_argument("--output", type=Path, help="PNG output path")
    args = parser.parse_args()

    output = args.output or args.log.with_name(f"{args.log.stem}_performance_overview.png")
    plot(parse_log(args.log), args.log, output)


if __name__ == "__main__":
    main()
