"""检查生产容器生成的圆盘、MPI/重启逐点一致性及三维回归。"""
import json
import math
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
checks = []
def check(name, passed, **details):
    checks.append(dict(name=name, passed=bool(passed), **details))

def ascii_points(path, dim, attributes):
    lines = path.read_text().splitlines()
    count, sr, si, ar, ai = map(int, lines[:5])
    check(str(path.relative_to(root)) + ':header', (sr, si, ar, ai) == (0, 0, attributes, 0))
    rows = [list(map(float, line.split())) for line in lines[5:]]
    check(str(path.relative_to(root)) + ':size', len(rows) == count and all(len(r) == dim + 2 + attributes for r in rows))
    check(str(path.relative_to(root)) + ':finite', all(math.isfinite(v) for r in rows for v in r))
    # 标记 ID 和原始 CPU 归属不属于几何；按坐标排序后逐值比较全部坐标及属性。
    return sorted(r[:dim] + r[dim + 2:] for r in rows)

statuses = dict(line.split('\t') for line in (root / 'status.tsv').read_text().splitlines()[1:])
negative = {'reject_sphere', 'reject_metadata', 'reject_legacy_sphere'}
for name, code in statuses.items():
    check(name + ':exit', int(code) != 0 if name in negative else int(code) == 0)
for name, message in [('reject_sphere', 'particles.shape must be disk'),
                      ('reject_metadata', 'particle checkpoint dimension or shape mismatch'),
                      ('reject_legacy_sphere', 'DIM=2 requires disk geometry metadata')]:
    check(name + ':reason', message in (root / name / 'run.log').read_text())

reference = None
for name in ('disk_r1', 'disk_r2', 'disk_resume'):
    rows = ascii_points(root / name / 'particle_0_00192', 2, 6)
    check(name + ':marker_count', len(rows) == math.ceil(2 * math.pi * 128))
    # 64² 单位方腔、D=32 coarse cells：物理半径为 1/4，圆心为 (1/2,1/2)。
    radius_error = max(abs(math.hypot(r[0] - .5, r[1] - .5) - .25) for r in rows)
    local_error = max(max(abs(r[0] - .5 - r[5]), abs(r[1] - .5 - r[6])) for r in rows)
    arc_error = abs(sum(r[7] for r in rows) - 2 * math.pi * 128)
    check(name + ':circle', radius_error < 2e-14, max_radius_error=radius_error)
    check(name + ':local_coordinates', local_error < 2e-14, max_error=local_error)
    check(name + ':arc_sum', arc_error < 2e-10, error=arc_error)
    check(name + ':static_force_torque', all(r[2:5] == [0., 0., 0.] for r in rows))
    if reference is None:
        reference = rows
    else:
        error = max(abs(a - b) for x, y in zip(reference, rows) for a, b in zip(x, y))
        check(name + ':pointwise', len(reference) == len(rows) and error == 0, max_abs_error=error)
    aux = ascii_points(root / name / 'cp_0_000192', 2, 1)
    check(name + ':auxiliary_circle', len(aux) == 180 and all(abs(math.hypot(r[0] - .5, r[1] - .5) - .25) < 2e-14 and r[2] == 0 for r in aux))

for name in ('disk_r1', 'disk_r2'):
    log = (root / name / 'run.log').read_text()
    for callback in ('MakeNewLevelFromCoarse', 'RemakeLevel', 'ClearLevel'):
        check(name + ':' + callback, 'AMR_LIFECYCLE ' + callback in log)
for name in ('disk_resume', 'fluid_reference', 'sphere_current', 'sphere_old_restart'):
    log = (root / name / 'run.log').read_text()
    errors = [float(v) for v in re.findall(r'active_linf=([\deE.+-]+)', log)]
    check(name + ':active_ddf', bool(errors) and all(math.isfinite(v) and v <= 1e-12 for v in errors), max_abs_error=max(errors, default=None))

# 三维标记数量、坐标和全部十个属性以原实现 checksum 逐量回归。
def particle_checksums(name):
    log = (root / name / 'run.log').read_text()
    return [line.split(' particle=', 1)[1] for line in log.splitlines() if line.startswith('particle_checksum:')]
old, current = particle_checksums('sphere_old'), particle_checksums('sphere_current')
def checksum_numbers(row):
    return {k: float(v) for k, v in re.findall(r'(count|pos\d+|attr\d+)=([\deE.+-]+)', row)}
checksum_error = max(abs(checksum_numbers(x)[k] - checksum_numbers(y)[k])
                     for x, y in zip(old, current) for k in checksum_numbers(x))
check('sphere:baseline_checksum', bool(old) and len(old) == len(current) and checksum_error <= 1e-12,
      max_abs_error=checksum_error)
# GPU 全局归约的结合顺序可能变化；下面逐标记比较仍要求零误差。
sphere_now = ascii_points(root / 'sphere_current' / 'particle_0_00004', 3, 10)
sphere_legacy = ascii_points(root / 'sphere_old_restart' / 'particle_0_00004', 3, 10)
sphere_error = max(abs(a - b) for x, y in zip(sphere_now, sphere_legacy) for a, b in zip(x, y))
check('sphere:legacy_checkpoint_pointwise', len(sphere_now) == len(sphere_legacy) and sphere_error == 0,
      max_abs_error=sphere_error)
result = dict(passed=all(c['passed'] for c in checks), checks=checks)
(root / 'acceptance.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['passed'] else 1)
