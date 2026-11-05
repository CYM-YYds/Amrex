#!/usr/bin/env python3
"""绘制传统两套网格（A-B）LBM 算法的中文 D2Q9 示意动画：左右两个 3×3 网格块。

左块：中心节点碰撞，8 个分量沿各自方向迁移到相邻节点（向外）。
右块：周围 8 个节点各自碰撞，指向中心的分量迁移到中心节点（向内）。
两块同步展示“碰撞 → 迁移”两帧。格点风格、箭头几何和颜色复用 plot_osi_principle.py，
GIF 合成复用 pack_animation_gif.py。教学示意，不读取求解器数据。
"""

import argparse
import json
from pathlib import Path
import subprocess
import sys

# 复用 OSI 原理动画的格点绘制、颜色与字体环境，保证同一组汇报图风格一致。
from plot_osi_principle import BLUE, E, INK, MUTED, ZERO_AT, box, label, lattice, node_arrows, node_dot, BLUE_W
import matplotlib.pyplot as plt
from matplotlib import font_manager
import numpy as np
from PIL import Image

N3 = 3
CENTER = np.array([1, 1])
LIGHT = '#9ecae1'
# 编号放在箭头尖端外侧（格心 + TIP·e_q），避开同格其余箭头。
TIP = .42
STAGES = ['碰撞', '迁移']
CAPTIONS = {('push', 0): '中心节点碰撞，得到 9 个分布函数',
            ('push', 1): '8 个分量沿各自方向迁移到相邻节点',
            ('pull', 0): '周围 8 个节点各自碰撞，深蓝为指向中心的分量',
            ('pull', 1): '这 8 个分量迁移到中心节点'}
TITLES = {'push': '中心节点碰撞后向外迁移', 'pull': '周围节点碰撞后向中心迁移'}


def validate():
    """左块迁移目标 c+e_q、右块来源 c−e_q 都在 3×3 内且互不重复。"""
    for sign in (1, -1):
        cells = {tuple(CENTER + sign * E[q]) for q in range(1, 9)}
        assert len(cells) == 8 and all(0 <= v < N3 for c in cells for v in c)
    return '左块迁移目标 c+e_q 与右块来源 c−e_q 均为 8 个不同的相邻节点'
def star(ax, cell, colors):
    """在 cell 节点把 8 个格点箭头原位变色（colors[q-1]），并画蓝色节点点。"""
    node_arrows(ax, [cell] * 8, range(1, 9), colors, BLUE_W, 6)
    node_dot(ax, cell, BLUE, .05)


def tip_label(ax, cell, q):
    label(ax, np.asarray(cell) + .5 + TIP * E[q], q, size=13, pad=False)


def rest(ax):
    """静止分量 0 始终留在中心节点。"""
    node_dot(ax, CENTER, BLUE, .05)
    label(ax, CENTER + .5 + ZERO_AT, 0, size=12, pad=False)


def center_star(ax):
    star(ax, CENTER, [BLUE] * 8)
    for q in range(1, 9):
        tip_label(ax, CENTER, q)
    rest(ax)


def draw_block(ax, scheme, stage):
    lattice(ax, CENTER, n=N3)
    if (scheme, stage) in (('push', 0), ('pull', 1)):
        # 左块碰撞前 / 右块迁移后：分量都在中心节点。
        center_star(ax)
        return
    for q in range(1, 9):
        if scheme == 'push':
            # 左块迁移后：分量 q 位于相邻节点 c+e_q，方向不变。
            cell = CENTER + E[q]
            node_arrows(ax, [cell], [q], BLUE, BLUE_W, 6)
        else:
            # 右块碰撞：来源节点 c−e_q 整体碰撞（浅蓝），指向中心的分量 q 用深蓝。
            cell = CENTER - E[q]
            star(ax, cell, [BLUE if d == q else LIGHT for d in range(1, 9)])
        box(ax, cell)
        tip_label(ax, cell, q)
    rest(ax)


def render(stage, dpi):
    fig = plt.figure(figsize=(12, 7.2), dpi=dpi)
    fig.text(.5, .945, '传统两套网格（A-B）算法', ha='center', fontsize=24, weight='bold', color=INK)
    for i, name in enumerate(STAGES):
        fig.text(.44 + .12 * i, .885, f'{i + 1}. {name}', ha='center', fontsize=17,
                 color=BLUE if i == stage else '#b7bec5', weight='bold' if i == stage else 'normal')
    fig.text(.50, .885, '→', ha='center', fontsize=17, color='#b7bec5')
    for x0, scheme in ((.05, 'push'), (.53, 'pull')):
        ax = fig.add_axes([x0, .19, .42, .6])
        draw_block(ax, scheme, stage)
        fig.text(x0 + .21, .815, TITLES[scheme], ha='center', fontsize=16, color=INK, weight='bold')
        fig.text(x0 + .21, .135, CAPTIONS[(scheme, stage)], ha='center', fontsize=15, color=INK)
    fig.text(.5, .055, '传统算法读网格 A、把迁移结果写入网格 B，避免覆盖尚未读取的数据；每个时间步交换 A、B。',
             ha='center', fontsize=12, color=MUTED)
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='新建的输出目录，拒绝覆盖')
    parser.add_argument('--duration', type=int, default=2400, help='每帧毫秒，10 的整数倍')
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
                         'axes.unicode_minus': False})
    check = validate()
    out = args.output.resolve()
    source = out / 'source_frames'
    source.mkdir(parents=True)
    for stage in range(len(STAGES)):
        fig = render(stage, args.dpi)
        fig.savefig(source / f'ab.{stage:04d}.png', facecolor='white')
        plt.close(fig)
    with Image.open(source / 'ab.0000.png') as im:
        w, h = im.size
    pack = [sys.executable, str(Path(__file__).resolve().parent / 'pack_animation_gif.py'),
            '--input', str(source), '--output', str(out / 'ab_principle.gif'), '--prefix', 'ab',
            '--crop', '0', '0', str(w), str(h), '--duration', str(args.duration),
            '--keep-colors', *[c.lstrip('#') for c in (BLUE, LIGHT, INK)]]
    subprocess.run(pack, check=True)
    manifest = {'check': check, 'stages': STAGES, 'directions': E.tolist(), 'duration_ms': args.duration,
                'input': '用户描述；无求解器数据，无数值归一化', 'command': [sys.executable, *sys.argv]}
    (out / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'output': str(out), 'frames': len(STAGES), 'check': check}, ensure_ascii=False))


if __name__ == '__main__':
    main()
