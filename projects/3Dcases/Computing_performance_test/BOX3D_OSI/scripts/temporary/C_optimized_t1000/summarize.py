"""本次C优化版长跑的完成核验与性能汇总；保留作为结果唯一复现入口。"""
from pathlib import Path
import json
import math
import re
import sys

out, production = map(lambda x: Path(x).resolve(), sys.argv[1:])
rows = []
for mode in (1, 0):
    directory = production / f'C_mode{mode}'
    status = json.loads((directory / 'exit_status.json').read_text())
    assert status['exit_code'] == 0 and status['launcher_returncode'] == 0
    log = (directory / 'run.log').read_text()
    assert re.search(r'^AMReX \([^\n]*\) finalized\s*$', log, re.M)
    windows = []
    for line in log.splitlines():
        if ' perf(s): ' in line:
            values = {k: float(v) for k, v in re.findall(r'(\w+)=([-+\deE.]+)', line)}
            assert all(math.isfinite(v) for v in values.values())
            values['step'] = int(re.match(r'step(\d+)', line).group(1))
            assert values['steps'] == 3200
            windows.append(values)
    assert [w['step'] for w in windows] == list(range(3200, 128001, 3200))
    assert len(list(directory.glob('plt_[0-9]*'))) == 40
    assert len(list(directory.glob('plt_vort_[0-9]*'))) == 40
    assert len(list(directory.glob('chk[0-9]*'))) == 4
    for step in range(3200, 128001, 3200):
        for prefix, components in (('plt_', 3), ('plt_vort_', 2)):
            plot = directory / f'{prefix}{step:06d}'
            header = (plot / 'Header').read_text().splitlines()
            assert int(header[1]) == components and float(header[components + 3]) == step / 128
            assert int(header[components + 4]) == 2
            for lev in range(3):
                assert (plot / f'Level_{lev}/Cell_H').is_file()
                files = list((plot / f'Level_{lev}').glob('Cell_D_*'))
                assert files and all(p.stat().st_size > 0 for p in files)
    for step in range(32000, 128001, 32000):
        checkpoint = directory / f'chk{step:08d}'
        header = (checkpoint / 'Header').read_text().splitlines()
        assert int(header[2]) == step and float(header[3]) == step / 128
        for lev in range(3):
            assert (checkpoint / f'Level_{lev}/f_old_H').is_file()
    totals = {key: sum(w[key] for w in windows) for key in ('interp', 'average', 'collide', 'stream', 'comm', 'boundary', 'swap', 'solv', 'total')}
    updates = sum(w['MLUPS_total'] * w['total'] * 1e6 for w in windows)
    convergence = [line for line in log.splitlines() if line.startswith('CONVERGENCE step=')]
    rows.append(dict(mode=mode, label='OSI' if mode else 'A-B', windows=windows, totals=totals,
                     MLUPS_total_last=windows[-1]['MLUPS_total'], MLUPS_solv_last=windows[-1]['MLUPS_solv'],
                     MLUPS_total_pooled=updates / totals['total'] / 1e6,
                     final_convergence=convergence[-1] if convergence else None,
                     field_frames=40, vorticity_frames=40, checkpoints=4, path=str(directory)))
summary = dict(rows=rows, job=out.name, node=(out / 'node.txt').read_text().strip(),
               metric='totals=sum40x3200step perf(s); last MLUPS matches historical table; pooled MLUPS separately reported',
               provenance='fingerprints.json; same frozen executable, sequential OSI then AB, single rank/A100; three planned restarts per mode at32000,64000,96000',
               validation='128step all-valid preflight; all published files size/SHA readback verified by transfer_manifest.json; both final finite checks; complete fields/checkpoints',
               limits='one trajectory per mode; AMR evolves separately; no assertion of long-run field equality or convergence')
(out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
lines = ['# 最新优化C组：OSI/A-B完整重跑', '',
         '两组从静止到128000步/t1000，同一最新冻结程序、同节点单A100、FLOAT/D3Q27、Re1000、C阈值，两组均按32000步分段并在32000/64000/96000恢复。每3200步保留速度及Q/涡量，共各40帧；checkpoint每32000步，共4份。', '',
         '| 模式 | 累计推进total/s | 末3200步MLUPS_total | 全程累计MLUPS_total |', '|---|---:|---:|---:|']
for row in rows:
    lines.append(f'| {row["label"]} | {row["totals"]["total"]:.2f} | {row["MLUPS_total_last"]:.2f} | {row["MLUPS_total_pooled"]:.2f} |')
lines += ['', '累计耗时含重网格，排除初始化、流场/checkpoint输出和回传；历史性能表用末窗口MLUPS，不混用全程累计值。', '',
          '所有文件已由回传入口逐项SHA回读校验；本报告检查两组退出0、40个性能窗口、全部场文件及checkpoint头。128步预检逐值一致不能代替全程数值等价；各组真实收敛标记见summary.json。', '',
          '该程序来源为提交前工作树冻结，额外在副本中补齐A-B128线程；正式源码不改。比较按OSI→A-B固定顺序执行一次，不作为重复统计的稳定加速结论。', '',
          '复现汇总（仓库根执行）：', '', '```bash',
          f'.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/C_optimized_t1000/summarize.py {out} {production}', '```']
(out / 'report.md').write_text('\n'.join(lines) + '\n')
print(json.dumps([{k: r[k] for k in ('label', 'MLUPS_total_last', 'MLUPS_total_pooled')} for r in rows], indent=2))
