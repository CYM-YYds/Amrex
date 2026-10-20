"""比较访问器替换前后的 CPU 指令及现有 CUDA 内核 SASS，不依赖运行时计时。"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

case = Path(__file__).resolve().parents[1]
repo = case.parents[3]
evidence = Path(sys.argv[1]).resolve()
cxx = os.environ.get('CXX', '/home/HPCBase/compilers/gcc/11.3.0/bin/g++')
cuobjdump = os.environ.get('CUOBJDUMP', '/home/HPCBase/compilers/cuda/12.8.0/bin/cuobjdump')
output = evidence / 'codegen'
output.mkdir(exist_ok=True)
checks = []

def instructions(assembly, name):
    match = re.search(rf'^{name}:\s*$(.*?)^\s*\.size\s+{name},', assembly, re.M | re.S)
    if not match:
        raise ValueError('missing assembly function: ' + name)
    return [line.strip() for line in match[1].splitlines()
            if line.strip() and not line.strip().startswith(('.', '//'))]

def sass(path):
    text = subprocess.check_output([cuobjdump, '--dump-sass', str(path)], text=True)
    functions = {}
    current = None
    for line in text.splitlines():
        match = re.search(r'Function\s*:\s*(\S+)', line)
        if match:
            current = match[1]
            functions[current] = []
        elif current and re.match(r'\s*/\*[0-9a-fA-F]+\*/', line):
            instruction = re.sub(r'/\*.*?\*/', '', line).strip()
            if instruction:
                functions[current].append(instruction)
    if not functions:
        raise ValueError('no CUDA functions found in ' + str(path))
    return functions

for dimension in (2, 3):
    config = output / 'AMReX_Config.H'
    config.write_text(f'#define AMREX_SPACEDIM {dimension}\n#define BL_SPACEDIM {dimension}\n#define AMREX_Linux 1\n#define NDEBUG 1\n#define AMREX_GPU_MAX_THREADS 256\n')
    assemblies = {}
    for version in ('baseline', 'current'):
        source = evidence / 'baseline/src' if version == 'baseline' else case / 'src'
        host = 'AMREX_D_PICK(0, 0, e_host[q][2])' if version == 'baseline' else 'lattice_ez_host(q)'
        device_table = 'AMREX_D_PICK(0, 0, e[q][2])' if version == 'baseline' else 'lattice_ez(q)'
        probe = output / 'probe.cpp'
        probe.write_text('#include "LbmLattice.H"\n'
                         f'extern "C" int probe_host(int q) {{ return {host}; }}\n'
                         f'extern "C" int probe_device_table(int q) {{ return {device_table}; }}\n')
        target = output / f'{version}{dimension}d.s'
        subprocess.run([cxx, '-std=c++20', '-O3', '-S', '-Wno-attributes', '-Wno-psabi',
                        f'-I{output}', f'-I{source}', f'-I{repo}/amrex-26.06/Src/Base',
                        str(probe), '-o', str(target)], check=True, capture_output=True)
        assemblies[version] = target.read_text()
    for name in ('probe_host', 'probe_device_table'):
        checks.append(dict(dimension=dimension, kind='CPU', name=name,
                           passed=instructions(assemblies['baseline'], name) == instructions(assemblies['current'], name)))
    for before in sorted((evidence / f'baseline/objects{dimension}d').glob('*.o')):
        after = case / f'tmp_build_dir/o/{dimension}d.gnu.TPROF.MPI.CUDA.EXE' / before.name
        a, b = sass(before), sass(after)
        (output / f'baseline{dimension}d_{before.stem}.json').write_text(json.dumps(a, indent=2))
        (output / f'current{dimension}d_{before.stem}.json').write_text(json.dumps(b, indent=2))
        changes = sorted(name for name in a.keys() | b.keys() if a.get(name) != b.get(name))
        digest = lambda value: hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()
        checks.append(dict(dimension=dimension, kind='CUDA_SASS', name=before.name,
                           passed=not changes, functions_before=len(a), functions_after=len(b),
                           changed_functions=changes, before_sha256=digest(a), after_sha256=digest(b)))
    # 完整程序补充覆盖 AMR、main 和验证实现中间接使用格子访问器的内核。
    before_exe = evidence / f'baseline/app{dimension}d.ex'
    after_exe = case / f'main{dimension}d.gnu.TPROF.MPI.CUDA.ex'
    a, b = sass(before_exe), sass(after_exe)
    (output / f'baseline{dimension}d_full.json').write_text(json.dumps(a, indent=2))
    (output / f'current{dimension}d_full.json').write_text(json.dumps(b, indent=2))
    changes = sorted(name for name in a.keys() | b.keys() if a.get(name) != b.get(name))
    checks.append(dict(dimension=dimension, kind='CUDA_SASS_FULL', name=after_exe.name,
                       passed=not changes, functions_before=len(a), functions_after=len(b),
                       changed_functions=changes))
report = dict(passed=all(c['passed'] for c in checks), checks=checks)
(output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
sys.exit(0 if report['passed'] else 1)
