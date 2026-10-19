#!/usr/bin/env python3
"""通过 ParaView 提取最新三维中心线，与对应 Re 的 Fig.18(a/b) 对比。"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np

from plot_centerline import normalize_profile, plot_profiles, read_profile, read_references, reference_errors

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]


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
    reynolds = float(params['lbm.reynolds_number'])
    if reynolds not in (1000, 3200):
        raise ValueError('仅支持 Re=1000 Fig.18(a) 或 Re=3200 Fig.18(b)')
    panel = 'a' if reynolds == 1000 else 'b'
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
    simulation=normalize_profile(profile,height,lid,bounds[4])
    reference=HERE/f'fig18{panel}_velocity_profiles.csv'
    references=read_references(reference)
    profiles=[{'label':run.name.split('_')[-1],'step':step,'points':simulation,'source':str(source),
               'normalization':{'H':height,'U_lid':lid,'z_origin':bounds[4]}}]
    metrics=[{'reference':name,**reference_errors(simulation,points,outside='exclude')}
             for name,points in references.items()]
    reference_labels={name:(f'Fig.18({panel}) AMR' if name.startswith('AMR') else
                           'Experiment' if name.startswith('Experimental') else 'Cortes & Miller')
                      for name in references}
    if reynolds == 1000:
        old,_=read_profile(HERE/'160000_3D.csv',require_mask=False)
        profiles.append({'label':'Historical (provenance incomplete)','step':160000,
                         'points':normalize_profile(old,128,.05),'source':str(HERE/'160000_3D.csv'),
                         'normalization':{'H':128,'U_lid':.05,'z_origin':0},'linestyle':'--','color':'#9467bd'})
    plot_profiles(profiles,references,output/'comparison.png',
                  title=f'3D cavity Re={reynolds:g}: step {step} vs Jaber Fig.18({panel})',
                  reference_source=str(reference),reference_labels=reference_labels)
    if reynolds == 3200:
        shutil.copyfile(output/'comparison.png',output/f'Re3200_3D_overlap_{step}_fig18b.png')
    with (output/'reference_errors.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(metrics[0]));writer.writeheader();writer.writerows(metrics)
    report={'input':str(source),'step':step,'Re':reynolds,'backend':args.backend,'export':export['value'],
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
