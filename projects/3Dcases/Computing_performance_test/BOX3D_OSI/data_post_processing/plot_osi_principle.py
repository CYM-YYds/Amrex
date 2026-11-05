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
from matplotlib.patches import Circle, Rectangle
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


# 背景箭头与高亮箭头共用同一几何：起点离节点中心 R0，总长 L（含箭头头部）。
R0, L = .08, .26
GRAY_W, BLUE_W = .018, .028
# 0 号标在 1、5 号箭头之间的空隙。
ZERO_AT = np.array([.25, .10])


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


def gap(e, r, turn):
    """方向 e 旋转 turn 度后半径 r 处的位置；22.5° 正好落在相邻两箭头之间的空隙。"""
    a = np.arctan2(e[1], e[0]) + np.radians(turn)
    return r * np.array([np.cos(a), np.sin(a)])


def label(ax, xy, q, color=BLUE, size=14):
    ax.text(*xy, str(q), color=color, fontsize=size, weight='bold',
            ha='center', va='center', zorder=8)


def box(ax, xy, color=INK):
    ax.add_patch(Rectangle(xy, 1, 1, fill=False, edgecolor=color, lw=1.6,
                           linestyle=(0, (4, 3)), zorder=5))


def lattice(ax):
    """背景：7×7 节点，每个节点一个灰点和 8 个灰色方向箭头，与参考图风格一致。"""
    for x in range(N):
        for y in range(N):
            ax.add_patch(Rectangle((x, y), 1, 1, facecolor='white', edgecolor=GRAY, lw=1))
    # 中心节点用灰色底，但保留其格点箭头，高亮时直接在原位变色。
    ax.add_patch(Rectangle(CENTER, 1, 1, facecolor='#a8a8a8', edgecolor=INK, lw=1.6, zorder=1))
    cells = [(x, y) for x in range(N) for y in range(N) for _ in range(8)]
    dirs = list(range(1, 9)) * (N * N)
    center_gray = '#e2e2e2'
    colors = [center_gray if c == tuple(CENTER) else GRAY for c in cells[::8]]
    for k, color in enumerate(colors):
        node_dot(ax, cells[8 * k], color, .035)
    node_arrows(ax, [c for c in cells if c != tuple(CENTER)],
                [d for c, d in zip(cells, dirs) if c != tuple(CENTER)], GRAY, GRAY_W, 3)
    node_arrows(ax, [CENTER] * 8, range(1, 9), center_gray, GRAY_W, 3)
    ax.set(xlim=(-.05, N + .05), ylim=(-.05, N + .05), aspect='equal')
    ax.axis('off')


def highlight_center(ax):
    """中心节点 8 个格点箭头变蓝，编号放在箭头尖外侧的空隙。"""
    node_arrows(ax, [CENTER] * 8, range(1, 9), BLUE, BLUE_W, 6)
    node_dot(ax, CENTER, BLUE, .05)


def draw_pdfs(ax, p):
    """读取/写回帧：把中心节点 9 个分量所在格点的对应方向箭头变蓝。"""
    cells = read_cells(p)
    unwrapped = CENTER - p * E
    if p % N == 0:
        # 9 个分量都在中心节点自身。
        box(ax, CENTER)
        highlight_center(ax)
        c = CENTER + .5
        for q in range(1, 9):
            # 编号放在各自箭头尖端外侧，仍在格内。
            u = E[q] / np.linalg.norm(E[q])
            label(ax, c + (.42 if q < 5 else .56) * u, q, size=10)
        label(ax, c + ZERO_AT, 0, size=9)
        return
    node_arrows(ax, cells[1:], range(1, 9), BLUE, BLUE_W, 6)
    for q in range(9):
        wrapped = ((unwrapped[q] < 0) | (unwrapped[q] >= N)).any()
        box(ax, cells[q], ORANGE if wrapped else INK)
        c = cells[q] + .5
        if q == 0:
            node_dot(ax, cells[q], BLUE, .05)
            label(ax, c + ZERO_AT, 0, size=12)
        else:
            # 编号放在箭头尾部一侧的空隙，不压灰色箭头。
            label(ax, c + gap(-E[q], .30, 22.5), q)


def draw_collision(ax):
    """碰撞帧：在中心节点内完成，中心的 8 个格点箭头变蓝；编号标在相邻格点的空隙。"""
    highlight_center(ax)
    c = CENTER + .5
    for q in range(1, 9):
        label(ax, c + E[q] + gap(-E[q], .30, 22.5), q)


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
