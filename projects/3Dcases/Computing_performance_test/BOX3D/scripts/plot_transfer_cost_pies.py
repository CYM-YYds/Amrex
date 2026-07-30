#!/usr/bin/env python3
"""Plot corrected BOX3D JaberCycle, Interp, and Average cost pies."""

import os
from pathlib import Path
from typing import List, Tuple

os.environ.setdefault("MPLCONFIGDIR", "/tmp/box3d-matplotlib")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties


OUTPUT_PATH = Path(__file__).resolve().parents[1] / "docs" / "transfer_cost_pies.png"
CJK_FONT_PATH = Path("/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc")
CJK_FONT = FontProperties(fname=str(CJK_FONT_PATH)) if CJK_FONT_PATH.exists() else FontProperties()

# All times are seconds from the 64-window JaberCycle aggregation.
OVERALL = [
    ("Interp", 2286.31),
    ("Average", 1180.21),
    ("Collide", 2124.19),
    ("Stream", 973.21),
    ("Boundary", 792.05),
    ("Comm", 248.16),
    ("Swap", 4.16),
]

# The raw legacy DDF-fill timers include 24.98 s from RemakeLevel/regridding,
# while the Interp total covers JaberCycle only. Remove that overlap so this
# pie closes exactly to the JaberCycle Interp total.
INTERP = [
    ("FillPatchTwoLevels\n(JaberCycle 内)", 1973.016),
    ("interp_scale", 313.291497),
]

# Keep the small remainder: it includes temporary destruction, loop overhead,
# and timer-boundary effects not attributed to the four measured subphases.
AVERAGE = [
    ("MultiFab 分配", 7.346740),
    ("MultiFab::Copy", 417.394012),
    ("average_scale", 446.152262),
    ("average_down", 306.020661),
    ("其他", 3.296325),
]


def autopct(pct: float) -> str:
    return f"{pct:.1f}%" if pct >= 2.0 else ""


def draw_pie(
    ax: plt.Axes,
    title: str,
    entries: List[Tuple[str, float]],
    colors: List[str],
    display_total: float = None,
) -> None:
    labels, values = zip(*entries)
    wedges, _, _ = ax.pie(
        values,
        colors=colors[: len(values)],
        startangle=90,
        counterclock=False,
        autopct=autopct,
        pctdistance=0.72,
        wedgeprops={"linewidth": 1.0, "edgecolor": "white"},
        textprops={"fontsize": 10},
    )
    total = sum(values)
    legend_labels = [f"{label}: {value:.2f} s ({value / total * 100:.2f}%)" for label, value in entries]
    ax.legend(
        wedges,
        legend_labels,
        loc="upper center",
        bbox_to_anchor=(0.5, -0.08),
        frameon=False,
        prop=CJK_FONT,
        handlelength=1.1,
    )
    shown_total = total if display_total is None else display_total
    ax.set_title(f"{title}\n总计 {shown_total:.2f} s", fontproperties=CJK_FONT, fontsize=13, pad=12)


def main() -> None:
    plt.rcParams["axes.unicode_minus"] = False

    figure, axes = plt.subplots(1, 3, figsize=(18, 7.4), constrained_layout=True)
    draw_pie(
        axes[0],
        "JaberCycle 总体耗时",
        OVERALL,
        ["#4C78A8", "#F58518", "#54A24B", "#E45756", "#72B7B2", "#B279A2", "#9D755D"],
        display_total=7608.30,
    )
    draw_pie(
        axes[1],
        "Interp 主要耗时\n(扣除 regrid 重复计入)",
        INTERP,
        ["#4C78A8", "#F58518"],
    )
    draw_pie(
        axes[2],
        "Average 主要耗时",
        AVERAGE,
        ["#9D755D", "#4C78A8", "#F58518", "#54A24B", "#BAB0AC"],
    )

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(OUTPUT_PATH, dpi=220, bbox_inches="tight", facecolor="white")
    print(OUTPUT_PATH)


if __name__ == "__main__":
    main()
