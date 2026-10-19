#!/usr/bin/env python3
"""通过当前 ParaView 控制桥提取 Re=1000 最新三维中心线并与 Fig.18(a) 对比。"""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path
import subprocess
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]


def read_profile(path):
    """读取 ParaView 点数据并严格检查采样有效性，不把无效填充值当成速度。"""
    groups = defaultdict(list)
    invalid = 0
    invalid_z = []
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        names = reader.fieldnames or []
        zkey = next((key for key in ('Points:2', 'Points_2', 'Points_Z') if key in names), None)
        if zkey is None or 'ux' not in names or 'vtkValidPointMask' not in names:
            raise ValueError(f'CSV 缺少坐标或 ux: {names}')
        for row in reader:
            if 'vtkValidPointMask' in names and float(row['vtkValidPointMask']) != 1:
                invalid += 1
                invalid_z.append(float(row[zkey]))
                continue
            z, ux = float(row[zkey]), float(row['ux'])
            if not np.isfinite([z, ux]).all():
                raise ValueError('CSV 含非有限采样值')
            groups[z].append(ux)
    if len(groups) < 2:
        raise ValueError('有效采样点不足')
    if any(min(groups) < z < max(groups) for z in invalid_z):
        raise ValueError('中心线内部存在无效采样，拒绝跨越缺口插值')
    return np.array([(z, np.mean(values)) for z, values in sorted(groups.items())]), invalid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--resolution', type=int, default=1024)
    parser.add_argument('--backend', choices=['bridge','batch'], default='bridge')
    parser.add_argument('--reuse-export', action='store_true', help='校验并复用已保存的 ParaView CSV，仅重绘和计算指标')
    args = parser.parse_args()
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
    if float(params['lbm.reynolds_number']) != 1000:
        raise ValueError('本流程固定使用 Re=1000 Fig.18(a)，禁止套用其他 Re')
    suffix = 'paraview' if args.backend=='bridge' else 'paraview_batch'
    output = HERE/f'{run.name}_step{step}_{suffix}'
    output.mkdir(exist_ok=True)
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
    profile,invalid=read_profile(csv_path)
    bounds=export['value']['bounds'];height=bounds[5]-bounds[4];lid=.05
    expected=[float(params['geometry.prob_lo'].split()[i//2]) if i%2==0 else
              float(params['geometry.prob_hi'].split()[i//2]) for i in range(6)]
    if not np.allclose(bounds,expected,rtol=0,atol=1e-12):
        raise ValueError(f'ParaView 范围与输入几何不符: {bounds} vs {expected}')
    simulation=np.column_stack(((profile[:,0]-bounds[4])/height,profile[:,1]/lid))
    reference=HERE/'fig18a_velocity_profiles.csv';references=defaultdict(list)
    with reference.open(newline='') as stream:
        for row in csv.DictReader(stream):
            references[row['series']].append((float(row['z']),float(row['u_over_ulid'])))
    fig,ax=plt.subplots(figsize=(9.4,7.5))
    ax.plot(simulation[:,1],simulation[:,0],color='#2ca02c',lw=2,label=f'job613495 step {step}')
    metrics=[]
    for (name,points),color,marker in zip(references.items(),['#d62728','#1f77b4'],['^','o']):
        points=np.array(sorted(points))
        mask=(points[:,0]>=simulation[0,0])&(points[:,0]<=simulation[-1,0])
        if not mask.any():raise ValueError('参考数据与采样范围没有交集')
        delta=np.interp(points[mask,0],simulation[:,0],simulation[:,1])-points[mask,1]
        metrics.append({'reference':name,'n_used':int(mask.sum()),'n_excluded':int((~mask).sum()),
                        'mae':float(np.mean(abs(delta))),'rmse':float(np.sqrt(np.mean(delta**2))),
                        'max_abs_error':float(max(abs(delta)))})
        ax.plot(points[:,1],points[:,0],'-'+marker,color=color,lw=1.4,ms=5,
                fillstyle='none',label=('Fig.18(a) AMR' if name=='AMR_triangle' else 'Cortes & Miller')+f' ({len(points)} pts)')
    old=defaultdict(list)
    with (HERE/'160000_3D.csv').open(newline='') as stream:
        for row in csv.DictReader(stream):old[float(row['Points_2'])/128].append(float(row['ux'])/.05)
    baseline=np.array([(key,np.mean(value)) for key,value in sorted(old.items())])
    ax.plot(baseline[:,1],baseline[:,0],'--',color='#9467bd',lw=1.3,
            label='Historical step 160000\n(provenance incomplete)')
    ax.set(xlabel=r'$u_x/U_{lid}$',ylabel=r'$z/H$',xlim=(-.4,1.05),ylim=(0,1),
           title=f'3D cavity Re=1000: step {step} vs Jaber Fig.18(a)')
    ax.grid(alpha=.3);ax.legend(fontsize=8,loc='upper left',bbox_to_anchor=(1.02,1))
    fig.text(.5,.025,f'Input: {run.name}/plt_{step}; centerline x/L=y/L=0.5\n'
             f'Normalization: H={height:g}, U_lid=0.05; reference: local digitized fig18a CSV',
             ha='center',fontsize=8)
    fig.tight_layout(rect=(0,.065,1,1));fig.savefig(output/'comparison.png',dpi=220);plt.close(fig)
    with (output/'reference_errors.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(metrics[0]));writer.writeheader();writer.writerows(metrics)
    report={'input':str(source),'step':step,'Re':1000,'backend':args.backend,'export':export['value'],
            'normalization':{'H':height,'U_lid':lid,'z_origin':bounds[4]},
            'input_hashes':{name:hashlib.sha256((run/name).read_bytes()).hexdigest()
                            for name in ('inputs','overrides.txt','manifest.txt')},
            'plot_header_sha256':hashlib.sha256((source/'Header').read_bytes()).hexdigest(),
            'valid_unique_z':len(profile),'invalid_samples':invalid,
            'z_over_H_range':[float(simulation[0,0]),float(simulation[-1,0])],
            'ux_over_ulid_range':[float(min(simulation[:,1])),float(max(simulation[:,1]))],
            'csv_sha256':hashlib.sha256(csv_path.read_bytes()).hexdigest(),
            'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),
            'errors':metrics,'convergence_verdict':'Not assessed; reference agreement is not convergence'}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));print('Output:',output)


if __name__=='__main__':main()
