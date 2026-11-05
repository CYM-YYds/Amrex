#!/usr/bin/env python3
# 本次 IBM 回归逐值检查；与实验一起保留以复现报告。
import json
import re
import sys
from pathlib import Path
import numpy as np

ROOT = Path(sys.argv[1]).resolve()
BOX = re.compile(r'\(\((-?\d+),(-?\d+),(-?\d+)\) \((-?\d+),(-?\d+),(-?\d+)\) \(\d+,\d+,\d+\)\)')


def fields(folder, prefix):
    result = {}
    for index in sorted(folder.glob(f'Level_*/{prefix}_H')):
        lines = index.read_text().splitlines()
        boxes = [tuple(map(int, BOX.fullmatch(line).groups())) for line in lines if BOX.fullmatch(line)]
        records = [line.split()[1:] for line in lines if line.startswith('FabOnDisk:')]
        assert len(boxes) == len(records) and boxes
        for box, (name, offset) in zip(boxes, records):
            with (index.parent/name).open('rb') as stream:
                stream.seek(int(offset))
                header = stream.readline().decode('ascii')
                match = BOX.search(header)
                assert match and header.startswith('FAB '), header
                bits = int(re.search(r'\(\d+, \((32|64) ', header).group(1))
                dtype = '<f4' if bits == 32 else '<f8'
                nc = int(header[match.end():].strip())
                x0,y0,z0,x1,y1,z1 = map(int, match.groups())
                shape = (nc,z1-z0+1,y1-y0+1,x1-x0+1)
                payload = stream.read(int(np.prod(shape))*np.dtype(dtype).itemsize)
                data = np.frombuffer(payload, dtype=dtype).reshape(shape)
                a,b,c,d,e,f = box
                valid = data[:,c-z0:f-z0+1,b-y0:e-y0+1,a-x0:d-x0+1].copy()
                assert valid.size and np.isfinite(valid).all(), (index,box,'nonfinite valid')
                key = (index.parent.name,)+box
                assert key not in result
                result[key] = valid
    assert result, (folder,prefix)
    return result


def compare(left, right, tolerance):
    a = fields(ROOT/left/'chk00000032','f_old')
    b = fields(ROOT/right/'chk00000032','f_old')
    assert a.keys() == b.keys(), (left,right,'valid topology mismatch')
    error = max(float(np.abs(a[k].astype('f8')-b[k]).max()) for k in a)
    unequal = sum(int(np.count_nonzero(a[k] != b[k])) for k in a)
    assert error <= tolerance, (left,right,error,tolerance)
    return dict(linf=error, unequal=unequal, tolerance=tolerance)


def main():
    statuses = dict(line.split('\t') for line in (ROOT/'status.tsv').read_text().splitlines()[1:])
    reasons = {'reject_metadata':'IBM checkpoint geometry mismatch',
               'reject_checkpoint':'IBM checkpoint enabled mismatch',
               'reject_shape':'particles.shape must be',
               'reject_mode':'IBM coupling requires lbm.collide_mode=1',
               'reject_origin':'IBM requires zero domain origin',
               'reject_geometry':'ibm.radius and ibm.length must be',
               'reject_resolution':'IBM radius must span at least one finest-level cell'}
    report = {'runs':{}, 'comparisons':{}}
    for name, code in statuses.items():
        log = (ROOT/name/'run.log').read_text()
        if name in reasons:
            assert int(code) != 0 and reasons[name] in log, (name,code,log[-300:])
            report['runs'][name] = 'rejected as expected'
            continue
        assert int(code) == 0 and 'finalized' in log, (name,code)
        ddf = fields(ROOT/name/'chk00000032','f_old')
        velocity = fields(ROOT/name/'plt_000032','Cell')
        report['runs'][name] = dict(valid_fabs=len(ddf), valid_values=sum(x.size for x in ddf.values()),
                                   velocity_abs_max=max(float(np.abs(x).max()) for x in velocity.values()))
        active = name.startswith('cylinder') or name.startswith('sphere') or name == 'uniform_cylinder'
        if active:
            force_files = list((ROOT/name).glob('CdCl*.dat')) + list((ROOT/name/'data').glob('CdCl*.dat'))
            assert force_files, (name,'missing force log')
            force = np.loadtxt(force_files[0],ndmin=2)
            assert np.isfinite(force).all() and np.abs(force[:,1:]).max() > 1e-7, (name,'inactive or nonfinite force')
            assert (ROOT/name/'chk00000032/particles_0/Header').exists(), (name,'coupled markers not checkpointed')
            report['runs'][name]['force_coefficient_abs_max'] = float(np.abs(force[:,1:]).max())
    for a,b,tol in [('baseline','off',0), ('baseline','legacy_restart',1e-6),
                    ('off','off_restart',1e-6),('sphere','sphere_restart',1e-6),
                    ('cylinder_osi','cylinder_osi_restart',1e-6),
                    ('cylinder_ab','cylinder_osi',1e-6),
                    ('cylinder_osi','cylinder_lockstep',1e-6),
                    ('cylinder_osi','cylinder_mpi',2e-6),
                    ('cylinder_mpi','cylinder_mpi_overlap',2e-6)]:
        report['comparisons'][a+'_vs_'+b] = compare(a,b,tol)
    effect = compare('uniform_off','uniform_cylinder',float('inf'))
    assert effect['linf'] > 1e-7, 'IBM must change the fluid on an identical uniform grid'
    effect.pop('tolerance')
    report['uniform_fluid_effect'] = effect
    lock = (ROOT/'cylinder_lockstep/run.log').read_text()
    assert 'osi_ab_stage_failure:' not in lock
    assert 'stage=Collision' in lock and 'stage=AfterRefineMeshAllValid' in lock
    report['lockstep'] = 'production stage checks passed (see log for covered/ghost scopes)'
    (ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
