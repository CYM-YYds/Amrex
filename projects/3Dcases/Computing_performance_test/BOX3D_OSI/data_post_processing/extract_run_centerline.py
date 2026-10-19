#!/usr/bin/env python3
"""通过 ParaView 提取三维中心线并校验来源；绘图单独使用 plot_centerline.py。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

from plot_centerline import normalize_profile, read_profile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--resolution', type=int, default=1024)
    parser.add_argument('--backend', choices=['bridge','batch'], default='bridge')
    parser.add_argument('--reuse-export', action='store_true', help='只校验已保存的 ParaView CSV 与元数据，不导出或绘图')
    parser.add_argument('--output-dir', type=Path, help='提取输出目录；默认沿用运行名和步数目录')
    parser.add_argument('--lid-velocity', type=float, default=0.05, help='报告归一化使用的顶盖速度，默认0.05')
    args = parser.parse_args()
    if args.resolution <= 0 or not np.isfinite(args.lid_velocity) or args.lid_velocity <= 0:
        parser.error('采样段数与顶盖速度必须为正有限数')
    run = args.run.resolve()
    candidates = [p for p in run.glob('plt_*') if p.name[4:].isdigit() and (p/'Header').is_file()]
    if not candidates: raise ValueError('没有完整 PlotFile Header')
    source = max(candidates, key=lambda p: int(p.name[4:]))
    step = int(source.name[4:])
    params = {}
    for line in (run/'inputs').read_text().splitlines():
        line = line.split('#',1)[0]
        if '=' in line:
            key,value=line.split('=',1); params[key.strip()]=value.strip()
    for item in (run/'overrides.txt').read_text().split():
        if '=' in item:
            key,value=item.split('=',1);params[key]=value
    reynolds = float(params['lbm.reynolds_number'])
    suffix = 'paraview' if args.backend=='bridge' else 'paraview_batch'
    output = args.output_dir.resolve() if args.output_dir else HERE/f'{run.name}_step{step}_{suffix}'
    output.mkdir(parents=True,exist_ok=True)
    csv_path=output/'centerline.csv'
    command=[sys.executable,str(REPO/'.agents/skills/paraview/scripts/paraview_bridge_send.py'),'current','export_centerline',
             '--input',str(source),'--output',str(csv_path),'--resolution',str(args.resolution),
             '--max-level','2','--timeout','180']
    metadata=output/'paraview_export.json'
    if args.backend=='batch':
        command=['bash',str(REPO/'.agents/skills/paraview/scripts/run_paraview_batch.sh'),
                 '--input',str(source),'--output',str(csv_path),'--metadata',str(metadata),
                 '--resolution',str(args.resolution),'--max-level','2']
    if args.reuse_export:
        export=json.loads(metadata.read_text())
        if (export['value']['input']!=str(source) or
                export['value']['resolution']!=args.resolution):
            raise ValueError('已有导出的来源或采样段数不匹配')
    else:
        reply=subprocess.run(command,capture_output=True,text=True,timeout=200)
        (output/'export.log').write_text(reply.stdout+'\n'+reply.stderr)
        if reply.returncode:
            raise RuntimeError(reply.stdout+'\n'+reply.stderr)
        export=json.loads(reply.stdout) if args.backend=='bridge' else json.loads(metadata.read_text())
        if not export['ok'] or not csv_path.is_file():
            raise ValueError('ParaView 未成功导出远端 CSV')
        metadata.write_text(json.dumps(export,indent=2)+'\n')
    if not export.get('ok') or export['value'].get('output') != str(csv_path) or not csv_path.is_file():
        raise ValueError('导出失败、CSV 缺失或元数据输出路径不匹配')
    profile,invalid=read_profile(csv_path)
    bounds=export['value']['bounds'];height=bounds[5]-bounds[4];lid=args.lid_velocity
    expected=[float(params['geometry.prob_lo'].split()[i//2]) if i%2==0 else
              float(params['geometry.prob_hi'].split()[i//2]) for i in range(6)]
    if not np.allclose(bounds,expected,rtol=0,atol=1e-12):
        raise ValueError(f'ParaView 范围与输入几何不符: {bounds} vs {expected}')
    simulation=normalize_profile(profile,height,lid,bounds[4])
    report={'input':str(source),'step':step,'Re':reynolds,'backend':args.backend,'export':export['value'],
            'normalization':{'H':height,'U_lid':lid,'z_origin':bounds[4]},
            'input_hashes':{name:hashlib.sha256((run/name).read_bytes()).hexdigest()
                            for name in ('inputs','overrides.txt','manifest.txt')},
            'plot_header_sha256':hashlib.sha256((source/'Header').read_bytes()).hexdigest(),
            'valid_unique_z':len(profile),'invalid_samples':invalid,
            'z_over_H_range':[float(simulation[0,0]),float(simulation[-1,0])],
            'ux_over_ulid_range':[float(min(simulation[:,1])),float(max(simulation[:,1]))],
            'csv_sha256':hashlib.sha256(csv_path.read_bytes()).hexdigest(),
            'convergence_verdict':'Not assessed; extraction validates samples only'}
    (output/'extraction_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));print('Output:',output)


if __name__=='__main__':main()
