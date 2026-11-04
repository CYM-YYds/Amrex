"""本次示意图的复现脚本：直接读取源码 ORDER0 方向表；随图片保留作为生成依据。"""
from pathlib import Path
from itertools import product
import re
import xml.etree.ElementTree as ET

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

CASE = Path(__file__).resolve().parents[3]
SOURCE = CASE / 'src/LbmLattice.H'
OUTPUT = CASE / 'docs/images'
COLORS = {1: '#1b9e3c', 2: '#2b6bd6', 3: '#d62f2f'}


def draw(model):
    # 不另存一套方向状态，编号始终来自相应宏的行序。
    block = SOURCE.read_text().split(f'#define LBM_{model}_ORDER0(X)', 1)[1].split('\n\n', 1)[0]
    velocities = [tuple(map(int, xyz)) for xyz in re.findall(r'X\(\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),', block)]
    count = int(model.split('Q')[1])
    assert len(velocities) == len(set(velocities)) == count
    assert velocities[0] == (0, 0, 0)
    assert all(tuple(-t for t in v) in velocities for v in velocities)
    is2d = model == 'D2Q9'

    def project(v):
        x, y, z = v
        return np.array([x, y]) if is2d else np.array([x + .50*y, 1.10*z + .38*y])

    fig, ax = plt.subplots(figsize=(8, 8))
    if is2d:
        ax.plot([-1, 1, 1, -1, -1], [-1, -1, 1, 1, -1], color='#222222', lw=1.65, zorder=2)
        for a, b in [((-1, 0, 0), (1, 0, 0)), ((0, -1, 0), (0, 1, 0))]:
            ax.plot(*np.array([project(a), project(b)]).T, color='#888888', lw=.75, ls='--', alpha=.55, zorder=1)
    else:
        for p in product([-1, 1], repeat=3):
            for k in range(3):
                if p[k] == -1:
                    q = list(p)
                    q[k] = 1
                    ax.plot(*np.array([project(p), project(q)]).T, color='#222222', lw=1.65, zorder=2)
        for k in range(3):
            for side in [-1, 1]:
                for direction in range(3):
                    if direction == k:
                        continue
                    a, b = np.zeros(3), np.zeros(3)
                    a[k] = b[k] = side
                    a[direction], b[direction] = -1, 1
                    ax.plot(*np.array([project(a), project(b)]).T, color='#888888', lw=.75, ls='--', alpha=.55, zorder=1)
    for q, v in enumerate(velocities[1:], 1):
        end = project(v)
        ax.annotate('', xy=end, xytext=(0, 0), arrowprops=dict(arrowstyle='-|>', color=COLORS[sum(t != 0 for t in v)], lw=1.1, mutation_scale=13, shrinkA=2, shrinkB=0), zorder=4)
        ax.annotate(str(q), xy=end, xytext=end / np.linalg.norm(end)*13, textcoords='offset points', fontsize=10, ha='center', va='center', color='#111111', bbox=dict(facecolor='white', edgecolor='none', alpha=.85, pad=.45), zorder=5)
    ax.scatter([0], [0], color='purple', s=20, zorder=6)
    ax.annotate('0', xy=(0, 0), xytext=(10, 6), textcoords='offset points', color='purple', fontsize=10, zorder=7, bbox=dict(facecolor='white', edgecolor='none', alpha=.85, pad=.45))
    ax.text(0, -1.99 if not is2d else -1.55, model, fontsize=20, ha='center')
    ax.set_aspect('equal')
    ax.axis('off')
    ax.set_xlim((-1.95, 1.95) if not is2d else (-1.6, 1.6))
    ax.set_ylim((-2.1, 1.85) if not is2d else (-1.7, 1.5))
    filename = f'{model}_with_face_cross.svg' if not is2d else 'D2Q9.svg'
    path = OUTPUT / filename
    fig.savefig(path, bbox_inches='tight', transparent=True, metadata={'Description': f'Source: src/LbmLattice.H, LBM_{model}_ORDER0; labels are zero-based e_host[q] indices. Projection: (x+0.50*y, 1.10*z+0.38*y) for 3D; (x,y) for 2D.'})
    path.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines()) + '\n')
    fig.savefig(f'/tmp/{model}_source_numbered.png', dpi=120, bbox_inches='tight', facecolor='white')
    assert ET.parse(path).getroot().tag == '{http://www.w3.org/2000/svg}svg'
    plt.close(fig)
    print(f'{model}: verified {count} directions; {path}')


if __name__ == '__main__':
    for model in ('D2Q9', 'D3Q19', 'D3Q27'):
        draw(model)
