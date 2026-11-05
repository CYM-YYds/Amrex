#!/usr/bin/env python3
"""绘制简化的中文 D2Q9 OSI 原理动画：每个时间步只展示读取、碰撞、写回三帧。

7×7 存储环内只跟踪一个节点（--node，默认中心）。读取位置随相位 p 逐步外移，
越过边界后从对侧回绕，每 7 步回到初始位置。教学示意，不读取求解器数据。
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
from matplotlib.patches import Circle, Rectangle
import numpy as np
from PIL import Image

N = 7
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


def read_cells(node, p):
    """相位 p 下跟踪节点 9 个分量的存储位置：(x - p e_q) mod 7。"""
    return (node - p * E) % N


def wrapped(node, p):
    """各分量在本周期内是否已越过 7×7 边界（按 p mod 7 判断，t+7 与 t 一致）。"""
    raw = node - (p % N) * E
    return ((raw < 0) | (raw >= N)).any(axis=1)


def validate(node):
    """核对图中位置与源码寻址一致、首步为参考图的邻居读取、周期为 7。"""
    for p in range(1, 3 * N):
        for q, e in enumerate(E):
            ref = [osi_coord(node[a], e[a], p) for a in range(2)]
            assert np.array_equal(read_cells(node, p)[q], ref)
    assert np.array_equal(read_cells(node, 1), node - E)
    assert all(np.array_equal(read_cells(node, p), read_cells(node, p + N)) for p in range(N))
    assert (read_cells(node, N) == node).all()
    return '图中读取位置与 OsiIndex.H 寻址一致；首步为相邻节点；7 步一周期'


# 背景箭头与高亮箭头共用同一几何：起点离节点中心 R0，总长 L（含箭头头部）。
R0, L = .08, .26
GRAY_W, BLUE_W = .018, .028
# 编号统一放在格心 + LABEL_R·(−e_q) 处（尾部一侧）：轴向编号落在格边中点，对角编号落在格角，
# 同一行/列的编号自然对齐。0 号放在 1、5 号箭头之间的空隙。
LABEL_R = .32
ZERO_AT = np.array([.19, .08])


def node_arrows(ax, cells, dirs, color, width, zorder):
    """在 cells[i] 节点画方向 E[dirs[i]] 的格点箭头；背景与高亮都走这里。"""
    cells, e = np.asarray(cells, float), E[np.asarray(dirs)]
    start = cells + .5 + R0 * e
    # headwidth/headlength 是 shaft 宽度的倍数，加粗时头部同比例放大、总长不变。
    ax.quiver(start[:, 0], start[:, 1], L * e[:, 0], L * e[:, 1], angles='xy',
              scale_units='xy', scale=1, units='xy', width=width, headwidth=3.5,
              headlength=4, headaxislength=3.5, color=color, zorder=zorder)


def node_dot(ax, cell, color, r):
    ax.add_patch(Circle(np.asarray(cell) + .5, r, color=color, zorder=6))


def label(ax, xy, q, color=BLUE, size=14, pad=True):
    """pad=True 时加白底，盖住编号下方的灰色背景箭头，保证位置统一后仍清晰。"""
    box = dict(boxstyle='square,pad=0.08', facecolor='white', edgecolor='none') if pad else None
    ax.text(*xy, str(q), color=color, fontsize=size, weight='bold',
            ha='center', va='center', zorder=8, bbox=box)


def box(ax, xy, color=INK):
    ax.add_patch(Rectangle(xy, 1, 1, fill=False, edgecolor=color, lw=1.6,
                           linestyle=(0, (4, 3)), zorder=5))


def lattice(ax, node):
    """背景：7×7 节点，每个节点一个灰点和 8 个灰色方向箭头，与参考图风格一致。"""
    for x in range(N):
        for y in range(N):
            ax.add_patch(Rectangle((x, y), 1, 1, facecolor='white', edgecolor=GRAY, lw=1))
    # 跟踪节点用灰色底，但保留其格点箭头，高亮时直接在原位变色。
    ax.add_patch(Rectangle(node, 1, 1, facecolor='#a8a8a8', edgecolor=INK, lw=1.6, zorder=1))
    cells = [(x, y) for x in range(N) for y in range(N) for _ in range(8)]
    dirs = list(range(1, 9)) * (N * N)
    node_gray = '#e2e2e2'
    for c in cells[::8]:
        node_dot(ax, c, node_gray if c == tuple(node) else GRAY, .035)
    node_arrows(ax, [c for c in cells if c != tuple(node)],
                [d for c, d in zip(cells, dirs) if c != tuple(node)], GRAY, GRAY_W, 3)
    node_arrows(ax, [node] * 8, range(1, 9), node_gray, GRAY_W, 3)
    ax.set(xlim=(-.05, N + .05), ylim=(-.05, N + .05), aspect='equal')
    ax.axis('off')


def highlight_node(ax, node):
    """跟踪节点 8 个格点箭头变蓝。"""
    node_arrows(ax, [node] * 8, range(1, 9), BLUE, BLUE_W, 6)
    node_dot(ax, node, BLUE, .05)


def draw_pdfs(ax, node, p):
    """读取/写回帧：把跟踪节点 9 个分量所在格点的对应方向箭头变蓝。"""
    cells = read_cells(node, p)
    if p % N == 0:
        # 9 个分量都在跟踪节点自身。
        box(ax, node)
        highlight_node(ax, node)
        c = node + .5
        for q in range(1, 9):
            # 9 个分量挤在同一格：编号放在各自箭头尖端外侧，同样按 e_q 方格对齐。
            label(ax, c + .40 * E[q], q, size=10, pad=False)
        label(ax, c + ZERO_AT, 0, size=9, pad=False)
        return
    node_arrows(ax, cells[1:], range(1, 9), BLUE, BLUE_W, 6)
    for q, w in enumerate(wrapped(node, p)):
        box(ax, cells[q], ORANGE if w else INK)
        c = cells[q] + .5
        if q == 0:
            node_dot(ax, cells[q], BLUE, .05)
            label(ax, c + ZERO_AT, 0, size=12, pad=False)
        else:
            label(ax, c - LABEL_R * E[q], q)


def draw_collision(ax, node):
    """碰撞帧：在跟踪节点内完成，其 8 个格点箭头变蓝；编号标在相邻格点靠近跟踪节点一侧。"""
    highlight_node(ax, node)
    c = node + .5
    for q in range(1, 9):
        label(ax, c + E[q] - LABEL_R * E[q], q)


def read_caption(node, k, p):
    if k == 0:
        return '从相邻节点读取 9 个分布函数'
    if k == N:
        return '与时间步 t 完全相同：读取位置每 7 步循环一次'
    if p % N == 0:
        return '9 个分布函数都位于跟踪节点自身'
    now, before = wrapped(node, p), wrapped(node, p - 1)
    if (now & ~before).any():
        return '越过 7×7 边界的位置从对侧回绕（橙色框）'
    if now.any():
        return '读取位置继续移动 1 格（橙色框为已回绕）'
    return '读取位置整体向外移动 1 格'


CAPTIONS = {1: '在寄存器中完成碰撞，不访问其他节点',
            2: '碰撞结果写回刚才读取的位置（同址写回）'}


def render(node, k, stage, dpi):
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
    lattice(ax, node)
    if stage == 1:
        draw_collision(ax, node)
    else:
        draw_pdfs(ax, node, p)
    fig.text(.5, .105, read_caption(node, k, p) if stage == 0 else CAPTIONS[stage],
             ha='center', fontsize=17, color=INK)
    fig.text(.5, .045, '7×7：计算域（中心 3×3）加两层幽灵层；只跟踪灰色节点。'
             '\n读取位置 = (x − p·e$_q$) mod 7，p 每个时间步加 1。',
             ha='center', fontsize=11.5, color=MUTED, linespacing=1.6)
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='新建的输出目录，拒绝覆盖')
    parser.add_argument('--duration', type=int, default=2400, help='每帧毫秒，10 的整数倍')
    parser.add_argument('--node', type=int, nargs=2, default=[3, 3], metavar=('X', 'Y'),
                        help='跟踪节点坐标，0 起算；默认中心 (3,3)，限 1..5 以免碰撞帧编号出界')
    parser.add_argument('--dpi', type=int, default=110)
    parser.add_argument('--font', type=Path, default=Path('/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc'),
                        help='中文字体文件')
    args = parser.parse_args()
    if not all(1 <= v <= N - 2 for v in args.node):
        parser.error('--node 坐标须在 1..5')
    node = np.array(args.node)
    if args.output.exists():
        parser.error('输出目录已存在，请使用新目录以保留旧结果')
    if not args.font.is_file():
        parser.error('未找到中文字体，请用 --font 指定字体文件')
    font_manager.fontManager.addfont(str(args.font))
    plt.rcParams.update({'font.family': font_manager.FontProperties(fname=str(args.font)).get_name(),
                         'axes.unicode_minus': False, 'svg.fonttype': 'path',
                         'mathtext.fontset': 'dejavusans'})
    check = validate(node)
    out = args.output.resolve()
    source = out / 'source_frames'
    source.mkdir(parents=True)
    # 时间步 t … t+6 共一个周期，再加 t+7 的读取帧展示“回到起点”。
    timeline = [(k, s) for k in range(N) for s in range(3)] + [(N, 0)]
    for i, (k, s) in enumerate(timeline):
        fig = render(node, k, s, args.dpi)
        fig.savefig(source / f'osi.{i:04d}.png', facecolor='white')
        plt.close(fig)
    with Image.open(source / 'osi.0000.png') as im:
        w, h = im.size
    # GIF 编码复用现有合成器（统一调色板、逐帧来源记录）。
    pack = [sys.executable, str(Path(__file__).resolve().parent / 'pack_animation_gif.py'),
            '--input', str(source), '--output', str(out / 'osi_principle.gif'), '--prefix', 'osi',
            '--crop', '0', '0', str(w), str(h), '--duration', str(args.duration)]
    subprocess.run(pack, check=True)
    manifest = {'check': check, 'frames': [{'frame': i, 'step': k, 'phase': k + 1, 'stage': STAGES[s],
                                            'cells': read_cells(node, k + 1).tolist()} for i, (k, s) in enumerate(timeline)],
                'node': args.node, 'directions': E.tolist(), 'duration_ms': args.duration,
                'input': '用户参考图；无求解器数据，无数值归一化',
                'command': [sys.executable, *sys.argv]}
    (out / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'output': str(out), 'frames': len(timeline), 'check': check}, ensure_ascii=False))


if __name__ == '__main__':
    main()
