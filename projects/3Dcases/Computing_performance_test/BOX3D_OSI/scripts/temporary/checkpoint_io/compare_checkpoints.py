#!/usr/bin/env python3
"""本次A-B/OSI检查点逐值比较；保留为数值结果复现材料。"""
import argparse
import json
import re
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('ab', type=Path)
p.add_argument('osi', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--uncovered', action='store_true', help='只比较双方共同uncovered valid，单独报告mask差异')
a = p.parse_args()
pattern = r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'

def boxes(text):
    return [(tuple(map(int, m[0].split(','))), tuple(map(int, m[1].split(','))))
            for m in re.findall(pattern, text)]

def load(root, lev):
    folder = root / f'Level_{lev}'
    text = (folder / 'f_old_H').read_text()
    valid = boxes(text)
    entries = re.findall(r'FabOnDisk: (\S+) (\d+)', text)
    assert len(valid) == len(entries)
    result = []
    for box, (name, offset) in zip(valid, entries):
        path = folder / name
        with path.open('rb') as stream:
            stream.seek(int(offset))
            header = stream.readline().decode('ascii')
            raw = boxes(header)[0]
            nq = int(header.split()[-1])
            assert nq == 27
            dtype = '<f4' if '((8, (32 ' in header else '<f8'
            shape = (nq,) + tuple(hi-lo+1 for lo,hi in zip(*raw))[::-1]
            data = np.memmap(path, mode='r', dtype=dtype, offset=stream.tell(), shape=shape)
        result.append((box, raw[0], data))
    return result

def count(box):
    return int(np.prod([hi-lo+1 for lo,hi in zip(*box)]))

headers = [x.joinpath('Header').read_text().splitlines() for x in (a.ab,a.osi)]
assert headers[0][2:5] == headers[1][2:5]
report = {'ab': str(a.ab.resolve()), 'osi': str(a.osi.resolve()),
          'step_time_finest': headers[0][2:5], 'layouts': [h[1] for h in headers],
          'scope': 'canonical f_old, same-level common valid cells; excludes ghosts; includes covered coarse cells; not a composite AMR error norm', 'levels': []}
if a.uncovered:
    report['scope'] = 'same-level common uncovered valid; covered by either next-fine layout excluded; not a composite AMR error norm'
for lev in range(int(headers[0][4])+1):
    left,right = load(a.ab,lev),load(a.osi,lev)
    n=neq=nonfinite=mask_disagree=0; ss=ref=0.; maximum=0.
    covered=[]
    for root in (a.ab,a.osi):
        fine=load(root,lev+1) if a.uncovered and lev<int(headers[0][4]) else []
        covered.append([(tuple(v//2 for v in box[0]),tuple(v//2 for v in box[1])) for box,_,_ in fine])
    for ba,oa,da in left:
        for bb,ob,db in right:
            lo=tuple(max(x,y) for x,y in zip(ba[0],bb[0]))
            hi=tuple(min(x,y) for x,y in zip(ba[1],bb[1]))
            if any(x>y for x,y in zip(lo,hi)): continue
            for z in range(lo[2],hi[2]+1,4):
                end=min(z+3,hi[2]);low=(lo[0],lo[1],z);high=(hi[0],hi[1],end)
                def take(d,o):
                    sl=tuple(slice(l-b,h-b+1) for l,h,b in zip(low,high,o))[::-1]
                    return np.asarray(d[(slice(None),)+sl],dtype=np.float64)
                x,y=take(da,oa),take(db,ob)
                if a.uncovered:
                    masks=[]
                    for regions in covered:
                        mask=np.ones(x.shape[1:],dtype=bool)
                        for lower,upper in regions:
                            ll=tuple(max(v,w) for v,w in zip(low,lower))
                            hh=tuple(min(v,w) for v,w in zip(high,upper))
                            if any(v>w for v,w in zip(ll,hh)):continue
                            slices=tuple(slice(v-b,w-b+1) for v,w,b in zip(ll,hh,low))[::-1]
                            mask[slices]=False
                        masks.append(mask)
                    mask_disagree+=int(np.count_nonzero(masks[0]!=masks[1]))
                    common=masks[0]&masks[1]
                    x,y=x[:,common],y[:,common]
                    if not x.size:continue
                finite=np.isfinite(x)&np.isfinite(y)
                nonfinite+=int(finite.size-np.count_nonzero(finite))
                delta=x-y;n+=delta.size;neq+=int(np.count_nonzero(x!=y))
                maximum=max(maximum,float(np.max(np.abs(delta))))
                ss+=float(np.sum(delta*delta));ref+=float(np.sum(y*y))
    na=sum(count(t[0]) for t in left);nb=sum(count(t[0]) for t in right)
    row={'level':lev,'ab_boxes':len(left),'osi_boxes':len(right),'ab_valid_cells':na,'osi_valid_cells':nb,
         'common_valid_cells':n//27,'ab_valid_cells_outside_compared_scope':na-n//27,'osi_valid_cells_outside_compared_scope':nb-n//27,
         'compared_values':n,'unequal_values':neq,'nonfinite_values':nonfinite,
         'max_abs_f_difference':maximum,'rms_f_difference':(ss/n)**.5 if n else None,
         'relative_l2_f_difference_osi_denominator':(ss/ref)**.5 if ref else (0.0 if n and ss==0 else None),
         'mask_disagreement_on_common_valid_cells':mask_disagree,
         'same_box_order': [t[0] for t in left]==[t[0] for t in right]}
    report['levels'].append(row)
    print(json.dumps(row),flush=True)
a.output.write_text(json.dumps(report,indent=2)+'\n')
