# 在编译节点测量标签实际 sizeof；保留作复现证据，实验不再需要时可删除。
from pathlib import Path
import shlex, subprocess, os
os.environ["OMPI_CXX"]="nvcc"
r=Path(__file__).resolve().parents[3]/'runs/temporary/restriction_tag_layout'
for variant in ('baseline','split'):
    d=r/variant
    text=(r/(variant+'_compile.log')).read_text()
    line=next(x for x in text.splitlines() if x.startswith('mpicxx ') and ' -c ' in x and 'src/main.cpp' in x)
    args=shlex.split(line)
    out=args.index('-o'); del args[out:out+2]
    args=[a for a in args if a != '-c']
    args=[a for a in args if a not in ('-MMD','-MP')]
    args[args.index('src/main.cpp')]='sizes.cpp'
    args+=['-o','sizes.ex']
    extra='sizeof(Box3dDetail::RestrictionRegionTag)' if variant=='split' else '0UL'
    (d/'sizes.cpp').write_text('#include "src/AmrCoreLBM.H"\n#include <cstdio>\nint main(){std::printf("block=%zu region=%zu\\n",sizeof(Box3dDetail::RestrictionBlockTag),'+extra+');}\n')
    subprocess.run(args,cwd=d,check=True,stdout=subprocess.DEVNULL)
    result=subprocess.check_output([str(d/'sizes.ex')],cwd=d,text=True)
    (r/(variant+'_sizes.txt')).write_text(result)
    print(variant,result.strip())
