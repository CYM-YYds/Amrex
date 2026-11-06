#!/usr/bin/env python3
# 本任务的宏观场初始化转换；保留以复现模型切换，不复用旧碰撞/IBM/边界源码。
import argparse
from pathlib import Path
import hashlib
import json
import re
import shutil
import numpy as np

BOX = re.compile(r'\(\(([-\d,]+)\) \(([-\d,]+)\) \([\d,]+\)\)')
OLD_EX=np.array([0,1,0,-1,0,1,-1,-1,1])[:,None,None,None]
OLD_EY=np.array([0,0,1,0,-1,1,1,-1,-1])[:,None,None,None]
DIRECTIONS=[(0,0,4/9),(1,0,1/9),(-1,0,1/9),(0,1,1/9),(0,-1,1/9),(1,1,1/36),(-1,-1,1/36),(1,-1,1/36),(-1,1,1/36)]

def coords(match):
    lo,hi=[list(map(int,g.split(','))) for g in match.groups()]
    assert len(lo)==2
    return lo,hi


def main():
    parser=argparse.ArgumentParser(description='旧压力型 DDF→宏观量→BOX Qian 平衡分布，不保留非平衡分量')
    parser.add_argument('source',type=Path)
    parser.add_argument('native_template',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    source,template,out=args.source.resolve(),args.native_template.resolve(),args.output.resolve()
    if out.exists():raise SystemExit(f'拒绝覆盖 {out}')
    header=(source/'Header').read_text();assert header.splitlines()[:2]==['LBMCheckpoint','520000']
    assert (template/'Flow').read_text().startswith('LBMFlowV1 1 ')
    seed_lines=(template/'Level_0/f_old_H').read_text().splitlines()
    rec=next(l.split()[1:] for l in seed_lines if l.startswith('FabOnDisk:'))
    with (template/'Level_0'/rec[0]).open('rb') as stream:
        stream.seek(int(rec[1]));native_header=stream.readline().decode()
    match=BOX.search(native_header);prefix=native_header[:match.start()]
    assert re.search(r'\(\d+, \(64 ',prefix),native_header
    out.mkdir(parents=True)
    (out/'Header').write_text(header)
    for name in ('Lattice','IBM','Flow'):
        shutil.copy2(template/name,out/name)
    shutil.copytree(template/'particles_0',out/'particles_0')
    report={'initialization':'old pressure moments → normalized density and velocity → BOX Qian equilibrium',
            'source_step':520000,'nonequilibrium_retained':False,'old_reference_pressure':1,
            'box_reference_density':1,'source_files':{},'levels':{},'not_a_same_model_restart':True}
    report['source_files']['Header']=hashlib.sha256((source/'Header').read_bytes()).hexdigest()
    for lev in range(4):
        src=source/f'Level_{lev}';dst=out/f'Level_{lev}';dst.mkdir()
        lines=(src/'f_old_H').read_text().splitlines()
        boxes=[BOX.fullmatch(l) for l in lines if BOX.fullmatch(l)]
        records=[l.split()[1:] for l in lines if l.startswith('FabOnDisk:')]
        assert len(boxes)==len(records) and boxes and int(lines[2])==9
        pre=lines[:next(i for i,l in enumerate(lines) if l.startswith('FabOnDisk:'))]
        pre[3]='0'
        fod=[];mins=[];maxs=[];moment_error=0
        with (dst/'f_old_D_00000').open('wb') as target:
            for valid,(filename,offset) in zip(boxes,records):
                path=src/filename
                key=f'Level_{lev}/{filename}'
                if key not in report['source_files']:report['source_files'][key]=hashlib.sha256(path.read_bytes()).hexdigest()
                with path.open('rb') as stream:
                    stream.seek(int(offset));fab_header=stream.readline().decode();m=BOX.search(fab_header)
                    lo,hi=coords(m);bits=int(re.search(r'\(\d+, \((32|64) ',fab_header).group(1));dtype='<f4' if bits==32 else '<f8'
                    shape=(9,1,hi[1]-lo[1]+1,hi[0]-lo[0]+1)
                    payload=stream.read(int(np.prod(shape))*np.dtype(dtype).itemsize)
                data=np.frombuffer(payload,dtype=dtype).reshape(shape).astype('f8')
                low,high=coords(valid)
                data=data[:,:,low[1]-lo[1]:high[1]-lo[1]+1,low[0]-lo[0]:high[0]-lo[0]+1]
                rho=data.sum(axis=0);ux=(data*OLD_EX).sum(axis=0);uy=(data*OLD_EY).sum(axis=0)
                assert np.isfinite(data).all() and (rho>0).all()
                rebuilt=[]
                for ex,ey,w in DIRECTIONS:
                    eu=ex*ux+ey*uy
                    rebuilt.append(w*rho*(1+3*eu+4.5*eu*eu-1.5*(ux*ux+uy*uy)))
                rebuilt=np.asarray(rebuilt,dtype='<f8')
                nr=rebuilt.sum(axis=0)
                nx=sum(rebuilt[q]*DIRECTIONS[q][0] for q in range(9))/nr
                ny=sum(rebuilt[q]*DIRECTIONS[q][1] for q in range(9))/nr
                moment_error=max(moment_error,float(np.abs(nr-rho).max()),float(np.abs(nx-ux).max()),float(np.abs(ny-uy).max()))
                position=target.tell();fod.append(f'FabOnDisk: f_old_D_00000 {position}')
                target.write((prefix+valid.group()+' 9\n').encode());target.write(rebuilt.tobytes())
                mins.append(rebuilt.min(axis=(1,2,3)));maxs.append(rebuilt.max(axis=(1,2,3)))
        tail=''
        for values in (mins,maxs):
            tail+=f'\n{len(values)},9\n'+''.join(','.join(f'{v:.17e}' for v in row)+',\n' for row in values)
        (dst/'f_old_H').write_text('\n'.join(pre+fod)+'\n'+tail)
        assert moment_error<1e-14
        report['levels'][str(lev)]={'fabs':len(boxes),'density_velocity_moment_linf':moment_error}
        report['source_files'][f'Level_{lev}/f_old_H']=hashlib.sha256((src/'f_old_H').read_bytes()).hexdigest()
    (out/'Initialization.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report['levels'],indent=2))

if __name__=='__main__':main()
