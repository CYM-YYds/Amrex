#!/usr/bin/env python3
# 用途：本次 IBM 参数与重启回归的逐值检查，作为实验复现材料保留。
import json
import re
from pathlib import Path
import numpy as np

CASE = Path(__file__).resolve().parents[3]
ROOT = CASE / 'runs/temporary/ibm_runtime_switch'
BOX = re.compile(rb'\(\((-?\d+),(-?\d+),(-?\d+)\) \((-?\d+),(-?\d+),(-?\d+)\) \(\d+,\d+,\d+\)\) (\d+)\s*$')


def fields(folder, prefix, ghosts):
    result = {}
    for path in sorted(folder.glob(f'Level_*/{prefix}_D_*')):
        with path.open('rb') as stream:
            while header := stream.readline():
                match = BOX.search(header)
                assert header.startswith(b'FAB ') and match, (path, header)
                assert b'(8, (64 11 52' in header, '需要 FP64 FAB'
                assert b'(8, (8 7 6 5 4 3 2 1))' in header, '需要小端 FAB'
                x0, y0, z0, x1, y1, z1, nc = map(int, match.groups())
                shape = (nc, z1-z0+1, y1-y0+1, x1-x0+1)
                count = int(np.prod(shape))
                payload = stream.read(count*8)
                assert len(payload) == count*8
                data = np.frombuffer(payload, dtype='<f8').reshape(shape)
                if ghosts:
                    data = data[:, ghosts:-ghosts, ghosts:-ghosts, ghosts:-ghosts]
                assert np.isfinite(data).all(), (path, 'nonfinite valid field')
                key = (path.parent.name, x0, y0, z0, x1, y1, z1)
                assert key not in result
                result[key] = data.copy()
    assert result, (folder, prefix)
    return result


def compare(left, right):
    summaries = {}
    for prefix in ['f_old', 'f_new']:
        a = fields(ROOT/left/'chk00000064', prefix, 4)
        b = fields(ROOT/right/'chk00000064', prefix, 4)
        assert a.keys() == b.keys(), (left, right, 'mesh mismatch')
        linf = max(float(np.max(np.abs(a[k]-b[k]))) for k in a)
        unequal = sum(int(np.count_nonzero(a[k] != b[k])) for k in a)
        assert linf < 1e-12, (left, right, prefix, linf)
        summaries[prefix] = dict(linf=linf, unequal=unequal)
    return summaries


def main():
    modes = ['implicit', 'sphere_idf', 'sphere_mdf', 'cylinder_mdf', 'off',
             'sphere_idf_restart', 'cylinder_mdf_restart', 'off_restart']
    report = {'valid_fields': {}, 'comparisons': {}}
    for mode in modes:
        run = ROOT/mode
        assert 'finalized' in (run/'run.log').read_text()
        report['valid_fields'][mode] = {
            name: len(fields(run/'chk00000064', name, 4))
            for name in ['f_old', 'f_new']}
        velocities = fields(run/'data/CylinderFlow_Re100_000064', 'Cell', 0)
        report['valid_fields'][mode]['velocity_abs_max'] = max(float(np.abs(a).max()) for a in velocities.values())
        if mode.startswith('off'):
            assert not list((run/'data').glob('CdCl*'))
            assert not list((run/'chk00000064').glob('particles_*'))
        else:
            force = np.loadtxt(run/'data/CdCl_0.dat', ndmin=2)
            assert np.isfinite(force).all()
            assert np.max(np.abs(force[:, 1:])) > 1e-10
            report['valid_fields'][mode]['force_coefficient_abs_max'] = float(np.abs(force[:, 1:]).max())
    report['comparisons']['implicit_vs_explicit'] = compare('implicit', 'sphere_idf')
    for mode in ['sphere_idf', 'cylinder_mdf', 'off']:
        report['comparisons'][mode+'_restart'] = compare(mode, mode+'_restart')
    report['comparisons']['original_vs_default'] = compare('baseline', 'implicit')
    if (ROOT/'legacy_restart/chk00000064').exists():
        report['comparisons']['legacy_restart'] = compare('baseline', 'legacy_restart')
    for name, message in [('invalid_ibm.geometry', 'ibm.geometry must be'),
                          ('invalid_ibm.method', 'ibm.method must be'),
                          ('mismatch', 'Checkpoint IBM configuration mismatch')]:
        assert message in (ROOT/name/'run.log').read_text()
    report['parameter_and_restart_rejection'] = 'passed'
    (ROOT/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
