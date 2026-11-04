#!/usr/bin/env python3
"""本次真实AMReX I/O路径配对诊断；结果与本地现场在全部校验前保留。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time

CASE = Path(__file__).resolve().parents[3]
REPO = CASE.parents[3]
ROOT = Path(os.environ.get('IO_OUTPUT_ROOT', str(CASE / 'runs/temporary/C_AB_IO_path_pair')))
TRIALS = os.environ.get('IO_PATH_TRIALS', 'local_original shared_heap shared_original').split()
assert TRIALS and len(set(TRIALS)) == len(TRIALS)
assert set(TRIALS) <= {'local_original', 'shared_heap', 'shared_original'}
JOB = os.environ['CCS_JOB_ID']
OUT = ROOT / f'job{JOB}'
OUT.mkdir(parents=True)


def save(name, data):
    (OUT / name).write_text(json.dumps(data, indent=2) + '\n')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def publish(source):
    # 沿用uniform512_local_staging的8MiB分块、fsync及SHA256回读协议。
    partial = OUT / '.local_original.partial'
    partial.mkdir()
    manifest = {}
    for src in sorted(source.rglob('*')):
        if src.is_symlink() or not src.is_file():
            continue
        relative = src.relative_to(source)
        dest = partial / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        with src.open('rb') as reader, dest.open('wb', buffering=0) as writer:
            while block := reader.read(8 * 1024**2):
                view = memoryview(block)
                while view:
                    n = writer.write(view)
                    if not n:
                        raise OSError('no copy progress')
                    view = view[n:]
            os.fsync(writer.fileno())
        checksum = digest(src)
        assert dest.stat().st_size == src.stat().st_size and digest(dest) == checksum
        manifest[str(relative)] = {'bytes': src.stat().st_size, 'sha256': checksum}
    (partial / 'transfer_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    partial.rename(OUT / 'local_original')
    return OUT / 'local_original'


save('environment.json', {'job': JOB, 'node': subprocess.check_output(['hostname'], text=True).strip(),
                          'scope': 'same GPU sequential: ' + ', '.join(TRIALS)})
for name in ['paired_paths.py', 'paired_submit.sh', 'heap_write.c']:
    shutil.copy2(Path(__file__).parent / name, OUT / name)
compile_result = subprocess.run(['gcc', '-std=c11', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
                                 str(OUT / 'heap_write.c'), '-o', str(OUT / 'heap_write.so'), '-ldl'],
                                capture_output=True, text=True)
(OUT / 'compile.log').write_text(compile_result.stdout + compile_result.stderr)
compile_result.check_returncode()
roots = [Path('/var/tmp'), Path('/tmp')]
roots = [p for p in roots if p.is_dir() and os.access(p, os.W_OK)]
local_root = max(roots, key=lambda p: shutil.disk_usage(p).free)
filesystem = subprocess.check_output(['stat', '-f', '-c', '%T', str(local_root)], text=True).strip()
assert filesystem not in ('nfs', 'nfs4', 'dpc', 'autofs')
assert shutil.disk_usage(local_root).free > 20 * 1024**3
local = Path(tempfile.mkdtemp(prefix=f'C_AB_IO_{JOB}_', dir=local_root))
save('local_disk.json', {'path': str(local), 'filesystem': filesystem,
                         'free_bytes': shutil.disk_usage(local).free})
base = json.loads((CASE / 'runs/C_AB_t1000_peak_memory_rerun/settings.json').read_text())
base.update(PROBE_END_STEP=os.environ.get('IO_END_STEP', '6400'), PROBE_PLOT_INT='3200', PROBE_REPORT_INT='3200',
            PROBE_GPU_MEMORY_MS=os.environ.get('IO_MEMORY_MS', '0'), PROBE_TRACE_IO='1', PROBE_WALL_SECONDS=os.environ.get('IO_WALL_SECONDS', '1800'))
statuses = []
for trial in TRIALS:
    env = os.environ.copy()
    env.pop('PROBE_HEAP_WRITE_LIBRARY', None)
    env.pop('PROBE_RESTART_ROOT', None)
    env.update(base)
    env['PROBE_OUTPUT_ROOT'] = str(local if trial == 'local_original' else OUT / trial)
    if trial == 'shared_heap':
        env['PROBE_HEAP_WRITE_LIBRARY'] = str(OUT / 'heap_write.so')
    save(trial + '_settings.json', {k: v for k, v in env.items() if k.startswith('PROBE_') or k == 'AMREX_RUN_ARGS'})
    started = time.monotonic()
    with (OUT / (trial + '.log')).open('w') as log:
        result = subprocess.run(['bash', str(CASE / 'scripts/temporary/a5_threshold_probe.sh')],
                                cwd=CASE, env=env, stdout=log, stderr=subprocess.STDOUT)
    directory = Path(env['PROBE_OUTPUT_ROOT']) / f'job{JOB}'
    status = {'trial': trial, 'returncode': result.returncode,
              'seconds': time.monotonic() - started, 'directory': str(directory)}
    if trial == 'local_original' and directory.exists():
        try:
            published = publish(directory)
            status['published'] = str(published)
        except Exception as error:
            status['publish_error'] = repr(error)
    if trial == 'shared_heap':
        log = directory / 'C_mode0/heap_write.log'
        status['intercepted_field_calls'] = len(log.read_text().splitlines()) if log.exists() else 0
        status['interposition_verified'] = status['intercepted_field_calls'] > 0
    statuses.append(status)
    save('status.json', statuses)
    print(json.dumps(status), flush=True)
# 只对成功的共享普通内存组与本地组逐文件比较场数据，不把缺失当作相等。
if (len(statuses) >= 2 and statuses[0]['trial'] == 'local_original'
        and statuses[1]['trial'] == 'shared_heap' and statuses[0].get('published')
        and statuses[0]['returncode'] == statuses[1]['returncode'] == 0):
    first = Path(statuses[0]['published']) / 'C_mode0'
    second = Path(statuses[1]['directory']) / 'C_mode0'
    checks = []
    for source in sorted(first.glob('plt_*/Level_*/Cell_D_*')):
        other = second / source.relative_to(first)
        checks.append({'file': str(source.relative_to(first)),
                       'equal': other.exists() and digest(source) == digest(other)})
    save('local_vs_heap_field_hashes.json', checks)
(OUT / 'completed.txt').write_text('selected trial processes returned; inspect each status\n')

# 任一计算失败或本地回传校验失败均让调度器报告失败。
raise SystemExit(1 if any(s['returncode'] != 0 or 'publish_error' in s for s in statuses) else 0)
