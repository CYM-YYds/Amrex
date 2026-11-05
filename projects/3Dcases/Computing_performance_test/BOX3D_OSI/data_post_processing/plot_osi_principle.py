#!/usr/bin/env python3
"""绘制中文 D2Q9 OSI 原理逐帧图；仅演示寻址与边界阶段，不求解流场。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import zipfile

os.environ.setdefault('MPLCONFIGDIR', '/tmp/osi_principle_matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Circle, FancyArrowPatch, Rectangle
import numpy as np
from PIL import Image

# 与 LbmLattice.H 的 D2Q9 排列 0 一致，编号始终标在对应方向分量上。
E = np.array([(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1),
              (1, 1), (-1, -1), (1, -1), (-1, 1)])
BLUE, RED, ORANGE = '#167db3', '#b64035', '#c27b12'
INK, GRAY = '#263547', '#c7cdd2'
STAGES = ['读取分布函数', '寄存器内碰撞', '写回原存储地址',
          '检查幽灵区通信', '相位加一：逻辑迁移', '非周期壁面重建']
SOURCES = ['src/OsiIndex.H', 'src/LbmLattice.H', 'src/Kernels.H',
           'src/AmrCoreLBM_advance.cpp', 'docs/osi_algorithm_and_architecture.md']


def address(x, phase):
    """本图使用完整 7×7 分配盒，局部原点为 0；两个轴分别取非负模。"""
    return (np.asarray(x) - phase * E) % 7


def validate_mapping():
    """检查环置换、拉式迁移恒等式，以及可见的 0→6 存储回绕。"""
    cells = [(x, y) for x in range(7) for y in range(7)]
    count = 0
    for p in range(14):
        for q, e in enumerate(E):
            slots = {tuple(address(x, p)[q]) for x in cells}
            assert len(slots) == 49
            for x in cells:
                assert np.array_equal(address(x, p + 1)[q], address(np.array(x) - e, p)[q])
                count += 1
    assert tuple(address((3, 3), 3)[1]) == (0, 3)
    assert tuple(address((3, 3), 4)[1]) == (6, 3)
    # 右壁是 valid 内的边界节点；内部参考为中心，不连接左壁。
    assert tuple(address((4, 3), 4)[1]) == (0, 3)
    return {'permutation_and_pull_identity_checks': count,
            'ring_wrap_q1_p3_to_p4': [[0, 3], [6, 3]],
            'validation_scope': '地址恒等式与图像完整性；不是求解器数值回归'}


def arrow(ax, start, end, color, scale=9, lw=1.6, alpha=1, **kw):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle='-|>', mutation_scale=scale,
                                linewidth=lw, color=color, alpha=alpha, **kw))


def direction(ax, xy, q, color, label=False, alpha=1):
    xy = np.asarray(xy, dtype=float)
    e = E[q]
    if q == 0:
        ax.add_patch(Circle(xy, .052, color=color, alpha=alpha, zorder=5))
        if label:
            ax.text(* (xy + [.10, .13]), '0', color=color, fontsize=9, weight='bold', zorder=8)
    else:
        arrow(ax, xy + .12 * e, xy + .40 * e, color,
              scale=8, lw=1.7, alpha=alpha, zorder=5)
        if label:
            pos = xy + .31 * e + .115 * np.array([-e[1], e[0]])
            ax.text(*pos, str(q), color=color, fontsize=8.4, ha='center', va='center',
                    weight='bold', zorder=8, bbox=dict(facecolor='white', edgecolor='none', pad=.05, alpha=.85))


def grid(ax, logical):
    starts, vectors, centers = [], [], []
    for y in range(7):
        for x in range(7):
            valid = 2 <= x <= 4 and 2 <= y <= 4
            inner = 1 <= x <= 5 and 1 <= y <= 5
            fill = '#f0f7fc' if valid else ('#eef0f2' if inner else '#e3e7eb')
            ax.add_patch(Rectangle((x, y), 1, 1, facecolor=fill,
                                   edgecolor='#bec5cb', linewidth=.8))
            xy = np.array([x + .5, y + .5])
            centers.append(xy)
            for e in E[1:]:
                starts.append(xy + .12 * e)
                vectors.append(.28 * e)
    starts, vectors, centers = map(np.asarray, (starts, vectors, centers))
    ax.quiver(starts[:, 0], starts[:, 1], vectors[:, 0], vectors[:, 1],
              angles='xy', scale_units='xy', scale=1, units='xy', width=.022,
              headwidth=3.5, headlength=4, headaxislength=4, color=GRAY, alpha=.66)
    ax.scatter(centers[:, 0], centers[:, 1], s=6, c=GRAY, alpha=.66)
    if logical:
        # valid 的最外层节点是非周期物理边界节点，边界线沿单元外缘标示。
        ax.add_patch(Rectangle((2, 2), 3, 3, fill=False, edgecolor=ORANGE, lw=2.8, zorder=6))
    else:
        ax.add_patch(Rectangle((0, 0), 7, 7, fill=False, edgecolor=ORANGE, lw=2.5, zorder=6))
    ax.set(xlim=(-.5, 7.4), ylim=(-.4, 7.4), aspect='equal')
    for i in range(7):
        ax.text(i + .5, -.19, str(i), fontsize=8, ha='center', color='#7b8792')
        ax.text(-.20, i + .5, str(i), fontsize=8, va='center', color='#7b8792')
    ax.axis('off')


def cell(ax, xy, color, fill=True, lw=1.8):
    ax.add_patch(Rectangle(xy, 1, 1, facecolor=color if fill else 'none',
                           alpha=.15 if fill else 1, edgecolor=color, lw=lw, zorder=3))


def to_fig(fig, ax, xy):
    return fig.transFigure.inverted().transform(ax.transData.transform(xy))


def render(phase, stage, t, index, total, dpi):
    fig = plt.figure(figsize=(14, 10), dpi=dpi, facecolor='white')
    fig.text(.5, .956, 'OSI 单数组推进：读取 → 碰撞 → 同址写回',
             ha='center', fontsize=22, color=INK, weight='bold')
    fig.text(.5, .917, f'第 {phase} 个演示步  ·  相位 p = {phase + (stage >= 4)}    |    {STAGES[stage]}',
             ha='center', fontsize=14, color=BLUE if stage < 4 else ORANGE)
    for k, name in enumerate(['读取', '碰撞', '写回', '通信检查', '相位更新', '壁面重建']):
        x = .08 + k * .145
        fig.text(x, .867, f'{k+1:02d}  {name}', fontsize=11,
                 color=(BLUE if k < 4 else ORANGE) if k == stage else '#adb5bd',
                 weight='bold' if k == stage else 'normal')
    left = fig.add_axes([.035, .31, .43, .51])
    right = fig.add_axes([.535, .31, .43, .51])
    grid(left, True)
    grid(right, False)
    left.set_title('逻辑坐标：中央 3×3 有效区，外围两层幽灵区', fontsize=12.5, pad=10, color=INK)
    right.set_title('实际存储：每个方向独立映射到 7×7 环', fontsize=12.5, pad=10, color=INK)
    fig.text(.25, .305, '橙框：物理域边界；边缘有效节点需要重建', ha='center', fontsize=10, color=ORANGE)
    fig.text(.75, .305, '底色仅表示初始分区；存储槽可承载任一逻辑节点', ha='center', fontsize=10, color='#647385')
    target = np.array([3, 3])
    new_phase = stage >= 4
    current = phase + 1 if new_phase else phase
    slots = address(target, current)
    old_slots = address(target, phase)
    color = BLUE if stage == 0 else RED if stage <= 3 else BLUE
    cell(left, target, INK)
    for q in range(9):
        direction(left, target + .5, q, color, label=True)
        cell(right, slots[q], color)
        direction(right, slots[q] + .5, q, color, label=True)
    left.text(3.5, 1.65, '关注逻辑节点 (3, 3)', fontsize=10, ha='center', color=INK)
    # 同一分量可能共享空间坐标，但仍是不同的 q 存储分量。
    reg_y = .233
    fig.text(.07, reg_y + .014, '寄存器', fontsize=12, color=INK, ha='left')
    regs = []
    for q in range(9):
        x = .203 + .077 * q
        regs.append(np.array([x + .027, reg_y + .014]))
        active = stage in (0, 1, 2)
        reg_color = BLUE if stage == 0 else RED
        fig.patches.append(Rectangle((x, reg_y - .012), .054, .052, transform=fig.transFigure,
                                     facecolor='#f5f8fb', edgecolor=reg_color if active else '#cbd2d9', lw=1.2))
        fig.text(x + .027, reg_y + .013, f'{q}', ha='center', va='center',
                 fontsize=12, color=reg_color if active else '#aeb6bd')
    fig.canvas.draw()
    if stage in (0, 2):
        # 蓝点是读取副本；红点是写回数据。箭头端点不改变地址或方向编号。
        for q in range(9):
            start = to_fig(fig, right, old_slots[q] + .5)
            end = regs[q]
            progress = t if stage == 0 else 1 - t
            pos = start * (1 - progress) + end * progress
            fig.patches.append(Circle(pos, .0045, transform=fig.transFigure,
                                      facecolor=BLUE if stage == 0 else RED, edgecolor='white', lw=.8, zorder=20))
    if stage == 4:
        # 标出寻址窗口跨环；不把存储中的值画成随 phase 搬运。
        for q in range(1, 9):
            unwrapped_old = target - phase * E[q]
            unwrapped_new = target - (phase + 1) * E[q]
            crossed = np.floor_divide(unwrapped_old, 7) != np.floor_divide(unwrapped_new, 7)
            if np.any(crossed):
                a, b = old_slots[q] + .5, slots[q] + .5
                for axis in np.flatnonzero(crossed):
                    offset = np.zeros(2)
                    offset[axis] = -E[q, axis]
                    arrow(right, a, a + offset * .68, ORANGE, scale=12, lw=2.5, zorder=9)
                    arrow(right, b - offset * .68, b, ORANGE, scale=12, lw=2.5, zorder=9)
        # 本例 p=3→4 时，q1 的寻址从 (0,3) 回绕至 (6,3)。
        wrap_note = 'q1 地址：(0, 3) → (6, 3)，越界后按 7 取模' if phase % 7 == 3 else '只更新相位与寻址窗口；原存储值保持原位'
        fig.text(.75, .812, wrap_note, ha='center', fontsize=10, color=ORANGE)
        arrow(left, (2.5, 3.5), (3.30, 3.5), BLUE, scale=16, lw=3, zorder=10)
        # 域外暂态分量不穿墙接到对侧；物理边界节点将在下一阶段覆盖。
        arrow(left, (5.55, 3.5), (4.65, 3.5), '#8b969f', scale=12, lw=2,
              linestyle='dashed', zorder=10)
        left.plot([4.36, 4.64], [3.36, 3.64], color=RED, lw=2, zorder=12)
        left.plot([4.36, 4.64], [3.64, 3.36], color=RED, lw=2, zorder=12)
        left.text(5.25, 3.86, '域外分量待重建', fontsize=9, color=RED, ha='center')
    if stage == 5:
        boundary = np.array([4, 3])
        # 本例四壁均静止，演示右壁代表节点；其余壁面及角点也按内部参考重建。
        for x in range(2, 5):
            for y in range(2, 5):
                if (x, y) != (3, 3):
                    cell(left, (x, y), ORANGE, fill=False)
        cell(left, boundary, ORANGE)
        arrow(left, (3.7, 3.5), (4.35, 3.5), ORANGE, scale=16, lw=3, zorder=10)
        left.text(4.6, 2.75, '右壁', color=ORANGE, fontsize=10, ha='center')
        wall_slots = address(boundary, current)
        for q in range(9):
            direction(right, wall_slots[q] + .5, q, ORANGE, label=True)
            # 仅画代表节点的九个写入，端点是 p+1 的壁面地址。
            pos = (slots[q] * (1 - t) + wall_slots[q] * t) + .5
            right.add_patch(Circle(pos, .06, color=ORANGE, zorder=10))
    if stage == 0:
        lines = ['按当前相位收集 9 个方向分量，进入同一线程的寄存器。',
                 '蓝点表示读取副本；方向编号与存储分量始终一一对应。']
        formula = r'$f_q(\mathbf{x})=M_q[A_q(\mathbf{x},p)]$'
    elif stage == 1:
        lines = ['在寄存器中计算密度、速度与平衡分布，再完成 BGK 碰撞。',
                 '图中颜色表示处理阶段，不表示分布函数大小。']
        formula = r'$f_q^*=f_q-\omega(f_q-f_q^{eq})$'
    elif stage == 2:
        lines = ['碰撞后的每个分量，写回刚才读取的同一个地址。',
                 '同一个 q 分量读写同址；此时相位保持不变。']
        formula = r'$M_q[A_q(\mathbf{x},p)]\leftarrow f_q^*(\mathbf{x})$'
    elif stage == 3:
        lines = ['本图：单块物理域、四壁非周期；域外幽灵区不从对侧周期补值。',
                 '多块时须先同步同层幽灵副本，再提交相位；壁面节点稍后重建。']
        formula = r'$p\ \longrightarrow\ p+1$'
    elif stage == 4:
        lines = ['相位提交后，中心逻辑节点改读上游分量；无需搬运整片分布函数。',
                 '存储索引可以回绕；穿过物理壁面的暂态分量仍须由边界规则覆盖。']
        formula = r'$A_q(\mathbf{x},p+1)=A_q(\mathbf{x}-\mathbf{e}_q,p)$'
    else:
        lines = ['读取新相位的内部参考值，用非平衡外推重建右壁的全部方向分量。',
                 '演示设四壁静止；壁面写回新相位地址，不连接对侧壁面。']
        formula = r'$f_q(\mathbf{x}_b)=f_q^{eq}(\rho_f,\mathbf{0})+f_q(\mathbf{x}_f)-f_q^{eq}(\rho_f,\mathbf{u}_f)$'
    fig.text(.5, .172, lines[0], ha='center', fontsize=12, color=INK)
    fig.text(.5, .143, lines[1], ha='center', fontsize=11, color='#647385')
    fig.text(.5, .095, formula, ha='center', fontsize=17, color=ORANGE if stage >= 4 else BLUE)
    fig.text(.5, .054, r'寻址：$A_q(\mathbf{x},p)=(\mathbf{x}-p\mathbf{e}_q)\;\mathrm{mod}\;7$'
             '    ｜    浅蓝：有效区  ·  浅灰：第一层幽灵区  ·  深灰：第二层幽灵区',
             ha='center', fontsize=10, color=INK)
    fig.text(.5, .022, f'来源：用户原理图 + BOX3D_OSI 寻址/推进/边界源码；D2Q9 示意，无流场输入、无数值归一化。   帧 {index+1}/{total}',
             ha='center', fontsize=8.2, color='#88929d')
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='新建的输出目录，拒绝覆盖')
    parser.add_argument('--steps', type=int, default=7, help='从 p=1 开始的演示步数，默认7以覆盖一整圈')
    parser.add_argument('--subframes', type=int, default=3, help='读写与边界阶段的插值帧数，至少2')
    parser.add_argument('--duration', type=int, default=350, help='每帧毫秒，须为正的10毫秒整数倍')
    parser.add_argument('--dpi', type=int, default=100)
    parser.add_argument('--font', type=Path, help='可选中文字体路径')
    args = parser.parse_args()
    if args.steps < 4 or args.subframes < 2 or args.dpi < 50 or args.duration <= 0 or args.duration % 10:
        parser.error('steps至少4，subframes至少2，dpi至少50；duration须为正的10毫秒整数倍')
    if args.output.exists():
        parser.error('输出目录已存在，请使用新目录以保留旧结果')
    font_path = args.font or Path('/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc')
    if not font_path.is_file():
        parser.error('未找到中文字体，请用 --font 指定字体文件')
    font_manager.fontManager.addfont(str(font_path))
    font = font_manager.FontProperties(fname=str(font_path)).get_name()
    plt.rcParams.update({'font.family': font, 'axes.unicode_minus': False, 'pdf.fonttype': 42,
                         'svg.fonttype': 'path', 'mathtext.fontset': 'dejavusans'})
    report = validate_mapping()
    out = args.output.resolve()
    out.mkdir(parents=True)
    keydir = out / 'keyframes'
    keydir.mkdir()
    # 阶段稍作停留；每帧都有独立序号，GIF编码不会吞并相同静帧。
    timeline = [(p, s, float(t)) for p in range(1, args.steps + 1) for s in range(6)
                for t in (np.linspace(0, 1, args.subframes) if s in (0, 2, 4, 5) else [0., .5, 1.])]
    records, selected = [], []
    for i, (p, s, t) in enumerate(timeline):
        fig = render(p, s, t, i, len(timeline), args.dpi)
        # 直接给现有 GIF 合成器消费，不复制另一套调色板/编码实现。
        frames = out / 'source_frames'
        frames.mkdir(exist_ok=True)
        path = frames / f'osi.{i:04d}.png'
        fig.savefig(path, dpi=args.dpi, facecolor='white')
        records.append({'frame': i, 'phase_before_step': p, 'address_phase': p + (s >= 4),
                        'stage': s, 'stage_name': STAGES[s], 'progress': t,
                        'target_logical': [3, 3], 'addresses': address((3, 3), p + (s >= 4)).tolist()})
        # p=3→4 包含最容易识别的一次存储回绕，保留完整六阶段矢量图。
        if p == 3 and t == 1:
            name = f'{s+1:02d}'
            shutil.copy2(path, keydir / f'{name}.png')
            fig.savefig(keydir / f'{name}.svg')
            fig.savefig(keydir / f'{name}.pdf')
            selected.append(path)
        plt.close(fig)
        if (i + 1) % 20 == 0:
            print(f'rendered {i+1}/{len(timeline)}', flush=True)
    source_dir = Path(__file__).resolve().parent
    command = [sys.executable, str(source_dir / 'pack_animation_gif.py'),
               '--input', str(out / 'source_frames'), '--output', str(out / 'osi_7x7_wall.gif'),
               '--prefix', 'osi', '--crop', '0', '0', str(14 * args.dpi), str(10 * args.dpi),
               '--duration', str(args.duration)]
    subprocess.run(command, check=True)
    # PNG元数据可变化；检查合成器没有改变未裁剪图像的像素。
    for i in range(len(timeline)):
        a, b = out / 'source_frames' / f'osi.{i:04d}.png', out / 'frames' / f'osi.{i:04d}.png'
        with Image.open(a) as ia, Image.open(b) as ib:
            assert np.array_equal(np.asarray(ia.convert('RGB')), np.asarray(ib.convert('RGB')))
    # source_frames 被合成器JSON引用，保留，以便逐项核验来源。
    tile_w = 700
    tile_h = 500
    sheet = Image.new('RGB', (3 * tile_w, 2 * tile_h), 'white')
    for i, path in enumerate(selected):
        with Image.open(path) as im:
            sheet.paste(im.resize((tile_w, tile_h), Image.Resampling.LANCZOS),
                        ((i % 3) * tile_w, (i // 3) * tile_h))
    sheet.save(out / 'osi_six_stages.png')
    case = source_dir.parent
    frozen = out / 'provenance'
    frozen.mkdir()
    shutil.copy2(__file__, frozen / Path(__file__).name)
    shutil.copy2(source_dir / 'pack_animation_gif.py', frozen / 'pack_animation_gif.py')
    hashes = {}
    for rel in SOURCES:
        source = case / rel
        hashes[rel] = hashlib.sha256(source.read_bytes()).hexdigest()
        target = frozen / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    report.update({'grid': [7, 7], 'valid_indices': [2, 4], 'ghost_layers': 2,
                   'physical_boundary': '四壁静止，非平衡外推；单块示意，无对侧周期补值',
                   'input': '用户参考图与本地源码；无PlotFile/求解器运行数据',
                   'normalization': '无；颜色仅表示阶段', 'directions': E.tolist(),
                   'source_sha256': hashes, 'steps': args.steps, 'frames': records,
                   'duration_ms': args.duration, 'font': str(font_path),
                   'command': [sys.executable, *sys.argv], 'pack_command': command})
    (out / 'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    (out / 'README.md').write_text(
        '# 中文 OSI 原理动画\n\n用途：按用户参考图解释单数组寻址；正式保留的图示材料，不是流场实验。\n\n'
        '中央3×3为逻辑valid，外围两层为ghost；本例valid就是完整物理域。valid边缘节点是壁面节点。'
        '实际存储中的valid/ghost底色只保留初始位置参照，不代表槽位永久所有权。\n\n'
        '读取→BGK碰撞→同址写回→通信检查（单块非周期，无周期补值）→phase加一→'
        '非平衡外推重建。四壁静止；右壁代表节点为(4,3)，内部参考为(3,3)。'
        '右壁重建后全部q写入Aq((4,3),p+1)；图中只高亮代表节点。'
        'ghost中的临时值不能作为跨墙有效流场；壁面节点在新phase下覆盖重建。\n\n'
        'D2Q9编号采用src/LbmLattice.H排列0，和用户参考图编号不同；不影响原理。'
        'p从1开始，演示步不对应生产计算步数。第3步相位3→4，中心q1地址从(0,3)回绕到(6,3)。'
        '相位更新阶段的箭头表示地址选择变化，不表示原始DDF搬运。\n\n'
        'frames/为最终逐帧PNG，source_frames/为GIF合成来源；为保留合成器JSON引用，两者均保留。'
        'keyframes/包含第3步六阶段PNG/SVG/PDF；osi_six_stages.png为总览。'
        'provenance/保存绘图脚本、合成器及源码快照；manifest.json保存逐帧地址、输入与验证范围。\n\n'
        '验证：地址双射、pull恒等式、0→6回绕和图像完整性。不构成生产求解器数值正确性证明。\n\n'
        '复现命令（仓库根执行，换用新的输出目录）：\n\n```bash\n'
        + ' '.join([shlex.quote(a)
                     for a in [sys.executable, *sys.argv]]) + '\n```\n', encoding='utf-8')
    with zipfile.ZipFile(out / 'osi_frames_and_sources.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for directory in ('frames', 'keyframes', 'provenance'):
            for path in sorted((out / directory).rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(out))
        for name in ('README.md', 'manifest.json', 'osi_six_stages.png', 'osi_7x7_wall.gif'):
            archive.write(out / name, name)
    print(json.dumps({'output': str(out), 'frames': len(timeline), 'checks': report['permutation_and_pull_identity_checks']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
