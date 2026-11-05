#!/usr/bin/env python3
"""绘制简化的中文 D2Q9 OSI 原理动画：每个时间步只展示读取、碰撞、写回三帧。

7×7 存储环内只跟踪中心节点。读取位置随相位 p 逐步外移，越过边界后从对侧回绕，
每 7 步回到初始位置。教学示意，不读取求解器数据。
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

os.environ.setdefault('MPLCONFIGDIR', '/tmp/osi_principle_matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Circle, FancyArrowPatch, Rectangle
import numpy as np
from PIL import Image

N = 7
CENTER = np.array([3, 3])
# 方向编号与用户参考图一致（常见 D2Q9 编号），不是 LbmLattice.H 的内部排列；
# 编号只是标签，不影响寻址规律。
E = np.array([(0, 0), (1, 0), (0, 1), (-1, 0), (0, -1),
              (1, 1), (-1, 1), (-1, -1), (1, -1)])
BLUE, ORANGE, INK, GRAY, MUTED = '#1f77b4', '#d9822b', '#263547', '#d3d8dc', '#8a96a1'
STAGES = ['读取', '碰撞', '写回']


def osi_coord(logical, velocity, phase):
    """逐行对应 OsiIndex.H::osi_coord_precomputed（lo=0、length=7）。"""
    shifted = logical - velocity * (phase % N)
    if shifted < 0:
        shifted += N
    elif shifted >= N:
        shifted -= N
    return shifted


def read_cells(p):
    """相位 p 下中心节点 9 个分量的存储位置：(x - p e_q) mod 7。"""
    return (CENTER - p * E) % N


def validate():
    """核对图中位置与源码寻址一致、首步为参考图的邻居读取、周期为 7。"""
    for p in range(1, 3 * N):
        for q, e in enumerate(E):
            ref = [osi_coord(CENTER[a], e[a], p) for a in range(2)]
            assert np.array_equal(read_cells(p)[q], ref)
    assert np.array_equal(read_cells(1), CENTER - E)
    assert all(np.array_equal(read_cells(p), read_cells(p + N)) for p in range(N))
    assert (read_cells(N) == CENTER).all()
    return '图中读取位置与 OsiIndex.H 寻址一致；首步为相邻节点；7 步一周期'


def arrow(ax, start, end, color, lw=2.2, scale=14):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle='-|>', mutation_scale=scale,
                                 linewidth=lw, color=color, zorder=6))


def label(ax, xy, q, color=BLUE, size=15):
    ax.text(*xy, str(q), color=color, fontsize=size, weight='bold',
            ha='center', va='center', zorder=7)


def box(ax, xy, color=INK):
    ax.add_patch(Rectangle(xy, 1, 1, fill=False, edgecolor=color, lw=1.6,
                           linestyle=(0, (4, 3)), zorder=5))


def lattice(ax):
    """背景：7×7 节点，每个节点画浅灰的 8 个方向，与参考图风格一致。"""
    starts, vecs = [], []
    for x in range(N):
        for y in range(N):
            ax.add_patch(Rectangle((x, y), 1, 1, facecolor='white', edgecolor=GRAY, lw=1))
            for e in E[1:]:
                starts.append(np.array([x + .5, y + .5]) + .08 * e)
                vecs.append(.26 * e)
    # 一次 quiver 画完 392 个背景箭头，逐个 FancyArrowPatch 渲染太慢。
    starts, vecs = np.array(starts), np.array(vecs)
    ax.quiver(starts[:, 0], starts[:, 1], vecs[:, 0], vecs[:, 1], angles='xy', scale_units='xy',
              scale=1, units='xy', width=.018, headwidth=3.5, headlength=4, headaxislength=3.5,
              color=GRAY)
    ax.add_patch(Rectangle(CENTER, 1, 1, facecolor='#9e9e9e', edgecolor=INK, lw=1.6, zorder=2))
    ax.set(xlim=(-.05, N + .05), ylim=(-.05, N + .05), aspect='equal')
    ax.axis('off')


def draw_pdfs(ax, p):
    """读取/写回帧：中心节点的 9 个分量所在存储位置，箭头为分量方向 e_q。"""
    cells = read_cells(p)
    unwrapped = CENTER - p * E
    if p % N == 0:
        # 9 个分量都在中心节点自身，箭头沿 e_q 指向节点中心。
        c = CENTER + .5
        box(ax, CENTER)
        ax.add_patch(Circle(c, .04, color=BLUE, zorder=7))
        # 0 号放在 1、5 号箭头之间的空隙，避免压到箭头。
        label(ax, c + [.255, .105], 0, size=10)
        for q in range(1, 9):
            arrow(ax, c - .30 * E[q], c - .12 * E[q], BLUE, lw=1.8, scale=10)
            label(ax, c - .40 * E[q] * (1 if q < 5 else .88), q, size=11)
        return
    for q in range(9):
        c = cells[q] + .5
        wrapped = ((unwrapped[q] < 0) | (unwrapped[q] >= N)).any()
        box(ax, cells[q], ORANGE if wrapped else INK)
        if q == 0:
            ax.add_patch(Circle(c, .06, color=BLUE, zorder=7))
            label(ax, c + [.2, .2], 0)
        else:
            arrow(ax, c - .22 * E[q], c + .30 * E[q], BLUE)
            label(ax, c - .36 * E[q], q)


def draw_collision(ax):
    """碰撞帧：在中心节点内完成，8 个方向向外发散。"""
    c = CENTER + .5
    ax.add_patch(Circle(c, .06, color=BLUE, zorder=7))
    for q in range(1, 9):
        arrow(ax, c + .06 * E[q], c + .42 * E[q], BLUE, lw=2.4, scale=12)
        label(ax, c + .72 * E[q], q)


def read_caption(k, p):
    r = p % N
    if k == 0:
        return '从相邻节点读取 9 个分布函数'
    if k == N:
        return '与时间步 t 完全相同：读取位置每 7 步循环一次'
    if r in (2, 3):
        return f'读取位置整体向外移动 1 格（距中心 {r} 格）'
    if r == 4:
        return '越过 7×7 边界的位置从对侧回绕（橙色框）'
    if r in (5, 6):
        return f'回绕后继续移动，逐步靠近中心（距中心 {N - r} 格）'
    return '9 个分布函数都位于中心节点自身'


CAPTIONS = {1: '在寄存器中完成碰撞，不访问其他节点',
            2: '碰撞结果写回刚才读取的位置（同址写回）'}


def render(k, stage, dpi):
    p = k + 1
    fig = plt.figure(figsize=(8, 9.2), dpi=dpi)
    fig.text(.5, .955, '时间步 t' if k == 0 else f'时间步 t + {k}',
             ha='center', fontsize=24, weight='bold', color=INK)
    for i, name in enumerate(STAGES):
        fig.text(.30 + .20 * i, .905, f'{i + 1}. {name}', ha='center', fontsize=17,
                 color=BLUE if i == stage else '#b7bec5', weight='bold' if i == stage else 'normal')
    for x in (.40, .60):
        fig.text(x, .905, '→', ha='center', fontsize=17, color='#b7bec5')
    ax = fig.add_axes([.07, .15, .86, .73])
    lattice(ax)
    if stage == 1:
        draw_collision(ax)
    else:
        draw_pdfs(ax, p)
    fig.text(.5, .105, read_caption(k, p) if stage == 0 else CAPTIONS[stage],
             ha='center', fontsize=17, color=INK)
    fig.text(.5, .045, '7×7：计算域（中心 3×3）加两层幽灵层；只跟踪中心灰色节点。'
             '\n读取位置 = (x − p·e$_q$) mod 7，p 每个时间步加 1。',
             ha='center', fontsize=11.5, color=MUTED, linespacing=1.6)
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='新建的输出目录，拒绝覆盖')
    parser.add_argument('--duration', type=int, default=1200, help='每帧毫秒，10 的整数倍')
    parser.add_argument('--dpi', type=int, default=110)
    parser.add_argument('--font', type=Path, default=Path('/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc'),
                        help='中文字体文件')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('输出目录已存在，请使用新目录以保留旧结果')
    if not args.font.is_file():
        parser.error('未找到中文字体，请用 --font 指定字体文件')
    font_manager.fontManager.addfont(str(args.font))
    plt.rcParams.update({'font.family': font_manager.FontProperties(fname=str(args.font)).get_name(),
                         'axes.unicode_minus': False, 'svg.fonttype': 'path',
                         'mathtext.fontset': 'dejavusans'})
    check = validate()
    out = args.output.resolve()
    source = out / 'source_frames'
    source.mkdir(parents=True)
    # 时间步 t … t+6 共一个周期，再加 t+7 的读取帧展示“回到起点”。
    timeline = [(k, s) for k in range(N) for s in range(3)] + [(N, 0)]
    for i, (k, s) in enumerate(timeline):
        fig = render(k, s, args.dpi)
        fig.savefig(source / f'osi.{i:04d}.png', facecolor='white')
        if s == 0:
            fig.savefig(out / f'read_step{k}.svg', facecolor='white')
        plt.close(fig)
    with Image.open(source / 'osi.0000.png') as im:
        w, h = im.size
    # GIF 编码复用现有合成器（统一调色板、逐帧来源记录）。
    pack = [sys.executable, str(Path(__file__).resolve().parent / 'pack_animation_gif.py'),
            '--input', str(source), '--output', str(out / 'osi_principle.gif'), '--prefix', 'osi',
            '--crop', '0', '0', str(w), str(h), '--duration', str(args.duration)]
    subprocess.run(pack, check=True)
    # 总览：8 个读取帧排成 2×4，便于放进幻灯片对比读取位置的循环。
    reads = [i for i, (_, s) in enumerate(timeline) if s == 0]
    tw, th = w // 2, h // 2
    sheet = Image.new('RGB', (4 * tw, 2 * th), 'white')
    for j, i in enumerate(reads):
        with Image.open(source / f'osi.{i:04d}.png') as im:
            sheet.paste(im.resize((tw, th), Image.Resampling.LANCZOS), ((j % 4) * tw, (j // 4) * th))
    sheet.save(out / 'read_overview.png')
    manifest = {'check': check, 'frames': [{'frame': i, 'step': k, 'phase': k + 1, 'stage': STAGES[s],
                                            'cells': read_cells(k + 1).tolist()} for i, (k, s) in enumerate(timeline)],
                'directions': E.tolist(), 'duration_ms': args.duration,
                'input': '用户参考图；无求解器数据，无数值归一化',
                'command': [sys.executable, *sys.argv]}
    (out / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'output': str(out), 'frames': len(timeline), 'check': check}, ensure_ascii=False))


if __name__ == '__main__':
    main()
