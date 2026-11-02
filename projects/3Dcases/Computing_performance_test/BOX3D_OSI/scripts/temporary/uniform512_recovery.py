#!/usr/bin/env python3
"""本轮临时512³恢复验证与正式重跑；保留本脚本用于复现，任务结束后按需清理。"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess

CASE = Path(__file__).resolve().parents[2]
REPO = CASE.parents[3]
EXP = CASE / 'runs/uniform512_recovery_t1000'
OUT = EXP / f"job{os.environ['CCS_JOB_ID']}"
OUT.mkdir()
shutil.copy2(__file__, OUT / 'runner.py')
shutil.copy2(EXP / 'inputs', OUT / 'inputs')
shutil.copy2(EXP / 'baseline.ex', OUT / 'baseline.ex')
previous_gate = os.environ.get('UNIFORM_PRECHECK_JOB')
if previous_gate:
    previous = EXP / f'job{previous_gate}'
    for source in (EXP / 'build/src').iterdir():
        if source.is_file():
            assert source.read_bytes() == (previous / 'src' / source.name).read_bytes()
    shutil.copytree(previous / 'src', OUT / 'src')
    shutil.copy2(previous / 'candidate.ex', OUT / 'candidate.ex')
    (OUT / 'reused_build.txt').write_text(str(previous) + '\n')
else:
    # 缓存仅复用AMReX依赖，强制重编本算例，避免旧时间戳混用其他冻结版本。
    for source in (EXP / 'build/src').iterdir():
        if source.is_file():
            source.touch()
    shutil.copytree(EXP / 'build/src', OUT / 'src')
    with (OUT / 'compile.log').open('w') as stream:
        subprocess.run(['make', '-j8', 'DIM=3', 'PRECISION=FLOAT'],
                       cwd=EXP / 'build', stdout=stream, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(EXP / 'build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex', OUT / 'candidate.ex')
(OUT / 'executables.json').write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest()
    for p in OUT.glob('*.ex')}, indent=2) + '\n')
(OUT / 'node.txt').write_bytes(subprocess.check_output(['hostname']))
(OUT / 'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
alloc = Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
(OUT / 'hostfile').write_text(''.join(f'{s.split()[0]} slots={s.split()[1]}\n'
    for s in alloc if len(s.split()) >= 2))
common = [str(OUT / 'inputs'), 'stop_time=1000', 'checkpoint.begin_step=0',
    'amr.max_level=0', 'amr.regrid_int=0', 'amr.blocking_factor_x=16',
    'amr.blocking_factor_y=16', 'amr.blocking_factor_z=16', 'geometry.is_periodic=0', '0', '0',
    'lbm.reynolds_number=1000', 'lbm.collide_mode=1', 'lbm.stream_mode=1',
    'lbm.uniform_nomask_collision=1', 'performance.uniform_reduce_sync=1',
    'amrex.the_arena_init_size=0', 'amrex.the_arena_release_threshold=0',
    'performance.osi_detail_timing=0',
    'amr.plot_file=plt_', 'amr.begin_plot=0', 'checkpoint.chk_prefix=chk',
    'checkpoint.keep_latest_only=false', 'checkpoint.write_particles=false',
    'verification.osi_seed_pattern=false', 'verification.osi_ab_check=false',
    'verification.convergence_stop=false', 'verification.convergence_tolerance=0.0001',
    'verification.convergence_required=3', 'verification.final_state_check=true']
status = []


def run(name, mode, options):
    directory = OUT / name
    directory.mkdir()
    command = [str(REPO / '.venv/bin/python'), str(CASE / 'scripts/guard_run.py'),
        '--wall-seconds', '32400', '--', 'mpirun', '-hostfile', str(OUT / 'hostfile'),
        '-n', '1', '-npernode', '1', '-x', 'PATH', '-x', 'LD_LIBRARY_PATH',
        '--mca', 'plm_rsh_agent', '/opt/batch/agent/tools/dstart',
        '/usr/bin/env', '-u', 'BASH_ENV', 'CUDA_VISIBLE_DEVICES=0',
        'strace', '-ff', '-o', 'io_trace', '-e', 'trace=write,writev,pwrite64,fsync,openat',
        '-e', 'status=failed', '-s', '0', '-yy', str(OUT / f'{mode}.ex')] + common + options
    (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    monitor = subprocess.Popen(['nvidia-smi', '--query-gpu=timestamp,index,uuid,memory.used,memory.total',
        '--format=csv', '-l', '1'], stdout=(directory / 'gpu_memory_samples.csv').open('w'),
        stderr=(directory / 'gpu_memory_samples.stderr').open('w'))
    try:
        with (directory / 'guard.log').open('w') as stream:
            result = subprocess.run(command, cwd=directory, stdout=stream, stderr=subprocess.STDOUT)
    finally:
        monitor.terminate()
        monitor.wait()
    status.append(dict(name=name, exit_code=result.returncode))
    (OUT / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
    assert result.returncode == 0, (name, result.returncode)
    print('completed', name, flush=True)
    return directory


small = ['amr.n_cell=64', '64', '64', 'amr.max_grid_size=32', 'amr.plot_int=-1',
    'performance.report_int=32', 'verification.convergence_enabled=false',
    'verification.osi_seed_pattern=true']
if previous_gate:
    reference = previous / 'small_reference'
    resume = previous / 'small_restart'
else:
    reference = run('small_reference', 'baseline', small + ['max_step=64', 'checkpoint.chk_int=32'])
    resume = run('small_restart', 'candidate', small + ['max_step=64', 'checkpoint.chk_int=-1',
        'checkpoint.begin_step=32', f'checkpoint.chk_prefix={reference}/chk',
        f'verification.ddf_reference_checkpoint={reference}/chk00000064'])
norms = [dict(re.findall(r'(\w+)=([^\s]+)', line))
         for line in (resume / 'run.log').read_text().splitlines()
         if line.startswith('ddf_cell_norm_level:')]
assert len(norms) == 1 and all(float(norms[0][k]) == 0
    for k in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l1', 'active_l2'])
large = ['amr.n_cell=512', '512', '512', 'amr.max_grid_size=512',
    'verification.convergence_enabled=true', 'verification.convergence_trend_int=16',
    'verification.convergence_check_int=32', 'performance.report_int=16']
if previous_gate:
    probe = previous / 'large_write'
else:
    probe = run('large_write', 'candidate', large + ['max_step=32', 'amr.plot_int=32',
        'checkpoint.chk_int=32'])
for prefix in ['plt_', 'plt_vort_']:
    header = (probe / f'{prefix}000032/Header').read_text().splitlines()
    assert float(header[int(header[1]) + 3]) == 32 / 512
    assert (probe / f'{prefix}000032/Level_0/Cell_H').exists()
links = OUT / 'restart_links'
links.mkdir()
(links / 'chk00000032').symlink_to(probe / 'chk00000032', target_is_directory=True)
restored = run('large_restart', 'candidate', large + ['max_step=64', 'amr.plot_int=64',
    'checkpoint.chk_int=64', 'checkpoint.begin_step=32', f'checkpoint.chk_prefix={links}/chk'])
assert 'all_finite=0' not in (restored / 'run.log').read_text()
(OUT / 'gate.json').write_text(json.dumps(dict(small_restart_exact=True,
    uniform512_checkpoint_read_write_passed=True, convergence_enabled=True), indent=2) + '\n')
print('GATE PASSED; starting full run', flush=True)
production = run('production', 'candidate', large + ['max_step=512000',
    'amr.plot_int=12800', 'checkpoint.chk_int=12800', 'performance.report_int=12800',
    'verification.convergence_trend_int=4000', 'verification.convergence_check_int=12800'])
header = (production / 'chk00512000/Header').read_text().splitlines()
assert int(header[2]) == 512000 and float(header[3]) == 1000
for prefix in ['plt_', 'plt_vort_']:
    header = (production / f'{prefix}512000/Header').read_text().splitlines()
    assert float(header[int(header[1]) + 3]) == 1000
(OUT / 'completed.txt').write_text('completed step512000 time1000\n')
