#!/usr/bin/env python3
"""临时512³本地落盘/校验转存入口；独立管理分段恢复和临时盘生命周期，结果保留供复现。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time

CASE = Path(__file__).resolve().parents[2]
REPO = CASE.parents[3]
EXP = CASE / 'runs/uniform512_local_staging_t1000'
SOURCE = CASE / 'runs/uniform512_recovery_t1000/job622850'
JOB = os.environ['CCS_JOB_ID']
OUT = EXP / f'job{JOB}'
OUT.mkdir()
START = time.monotonic()
CHUNK = 8 * 1024**2
FREE_REQUIRED = 20 * 1024**3
PROBE_ONLY = os.environ.get('UNIFORM_LOCAL_PROBE_ONLY', '0') == '1'
REUSE_GATE = os.environ.get('UNIFORM_LOCAL_REUSE_GATE_JOB')
OVERALL_SECONDS = int(os.environ.get('UNIFORM_STAGING_WALL_SECONDS', '86400'))


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(CHUNK):
            h.update(block)
    return h.hexdigest()


def publish(local, name):
    """普通主机缓冲分块写入，落盘并读回校验后才公布整批目录。"""
    partial = OUT / f'.{name}.partial'
    partial.mkdir()
    manifest = {}
    for source in sorted(local.rglob('*')):
        if source.is_symlink() or not source.is_file():
            continue
        relative = source.relative_to(local)
        target = partial / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        for attempt in range(3):
            try:
                h = hashlib.sha256()
                with source.open('rb') as reader, target.open('wb', buffering=0) as writer:
                    while block := reader.read(CHUNK):
                        h.update(block)
                        view = memoryview(block)
                        while view:
                            written = writer.write(view)
                            if not written:
                                raise OSError('short write without progress')
                            view = view[written:]
                    os.fsync(writer.fileno())
                assert target.stat().st_size == source.stat().st_size
                assert digest(target) == h.hexdigest(), f'checksum mismatch: {relative}'
                manifest[str(relative)] = dict(bytes=source.stat().st_size, sha256=h.hexdigest())
                break
            except (OSError, AssertionError) as error:
                print(f'export retry {name}/{relative} attempt={attempt+1}: {error}', flush=True)
                if attempt == 2:
                    raise
                time.sleep(2)
    save(partial / 'transfer_manifest.json', manifest)
    final = OUT / name
    partial.rename(final)
    print('published and verified', final, flush=True)
    # 本地对象只在共享副本逐文件校验成功后释放；失败路径保留本地及.partial现场。
    shutil.rmtree(local)
    return final


shutil.copy2(__file__, OUT / 'runner.py')
shutil.copy2(SOURCE / 'candidate.ex', OUT / 'candidate.ex')
shutil.copy2(SOURCE / 'inputs', OUT / 'inputs')
shutil.copytree(SOURCE / 'src', OUT / 'src')
assert digest(OUT / 'candidate.ex') == json.loads((SOURCE / 'executables.json').read_text())['candidate.ex']
alloc = Path(os.environ['CCS_ALLOC_FILE']).read_text().splitlines()
(OUT / 'hostfile').write_text(''.join(f'{s.split()[0]} slots={s.split()[1]}\n'
    for s in alloc if len(s.split()) >= 2))
(OUT / 'node.txt').write_bytes(subprocess.check_output(['hostname']))
(OUT / 'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
roots = [Path('/var/tmp'), Path('/tmp')]
roots = [p for p in roots if p.is_dir() and os.access(p, os.W_OK)]
root = max(roots, key=lambda p: shutil.disk_usage(p).free)
filesystem = subprocess.check_output(['stat', '-f', '-c', '%T', str(root)], text=True).strip()
assert filesystem not in ['nfs', 'nfs4', 'dpc', 'autofs'], filesystem
assert shutil.disk_usage(root).free >= FREE_REQUIRED, 'local disk requires at least 20 GiB free'
LOCAL = Path(tempfile.mkdtemp(prefix=f'box512_stage_{JOB}_', dir=root))
save(OUT / 'local_disk.json', dict(path=str(LOCAL), filesystem=filesystem,
    free_bytes=shutil.disk_usage(root).free, binary_sha256=digest(OUT / 'candidate.ex')))
print('local staging root', LOCAL, flush=True)
common = [str(OUT / 'inputs'), 'stop_time=1000', 'amr.n_cell=512', '512', '512',
    'amr.max_level=0', 'amr.max_grid_size=512', 'amr.regrid_int=0',
    'amr.blocking_factor_x=16', 'amr.blocking_factor_y=16', 'amr.blocking_factor_z=16',
    'geometry.is_periodic=0', '0', '0', 'lbm.reynolds_number=1000',
    'lbm.collide_mode=1', 'lbm.stream_mode=1', 'lbm.uniform_nomask_collision=1',
    'performance.uniform_reduce_sync=1', 'performance.osi_detail_timing=0',
    'amrex.the_arena_init_size=0', 'amrex.the_arena_release_threshold=0',
    'amr.plot_file=plt_', 'amr.begin_plot=0', 'checkpoint.chk_prefix=chk',
    'checkpoint.keep_latest_only=false', 'checkpoint.write_particles=false',
    'verification.osi_seed_pattern=false', 'verification.osi_ab_check=false',
    'verification.final_state_check=true', 'verification.convergence_enabled=true',
    'verification.convergence_stop=false', 'verification.convergence_tolerance=0.0001',
    'verification.convergence_required=3']
records = []


def run_segment(name, begin, end, interval, previous=None):
    assert time.monotonic() - START < OVERALL_SECONDS, 'overall time limit reached'
    assert shutil.disk_usage(root).free >= FREE_REQUIRED, 'local free space fell below 20 GiB'
    local = LOCAL / name
    local.mkdir()
    if previous is not None:
        (local / f'chk{begin:08d}').symlink_to(previous, target_is_directory=True)
    options = [f'checkpoint.begin_step={begin}', f'max_step={end}',
        f'amr.plot_int={interval}', f'checkpoint.chk_int={interval}',
        f'performance.report_int={interval}',
        f'verification.convergence_trend_int={max(1, interval * 5 // 16)}',
        f'verification.convergence_check_int={interval}']
    command = [str(REPO / '.venv/bin/python'), str(CASE / 'scripts/guard_run.py'),
        '--wall-seconds', '1800', '--', 'mpirun', '-hostfile', str(OUT / 'hostfile'),
        '-n', '1', '-npernode', '1', '-x', 'PATH', '-x', 'LD_LIBRARY_PATH',
        '--mca', 'plm_rsh_agent', '/opt/batch/agent/tools/dstart', '/usr/bin/env',
        '-u', 'BASH_ENV', 'CUDA_VISIBLE_DEVICES=0', 'strace', '-ff', '-o', 'io_trace',
        '-e', 'trace=write,writev,pwrite64,fsync,openat', '-e', 'status=failed',
        '-s', '0', '-yy', str(OUT / 'candidate.ex')] + common + options
    save(local / 'command.json', command)
    # 实时小日志留共享目录；大场文件、checkpoint和应用日志首先落本地盘。
    with (OUT / f'{name}-guard.log').open('w') as stream:
        result = subprocess.run(command, cwd=local, stdout=stream, stderr=subprocess.STDOUT)
    assert result.returncode == 0, f'application failed: {name}; local evidence at {local}'
    checkpoint = local / f'chk{end:08d}'
    header = (checkpoint / 'Header').read_text().splitlines()
    assert int(header[2]) == end and float(header[3]) == end / 512
    assert (checkpoint / 'Level_0/f_old_H').is_file()
    for prefix, components in [('plt_', 3), ('plt_vort_', 2)]:
        plot = local / f'{prefix}{end:06d}'
        header = (plot / 'Header').read_text().splitlines()
        assert int(header[1]) == components and float(header[components + 3]) == end / 512
        files = list((plot / 'Level_0').glob('Cell_D_*'))
        assert len(files) == 1 and files[0].stat().st_size >= 512**3 * components * 4
    final = publish(local, name)
    records.append(dict(name=name, begin=begin, end=end, time=end/512,
                        published=str(final), export_verified=True))
    save(OUT / 'status.json', records)
    return final


try:
    if REUSE_GATE:
        verified = EXP / f'job{REUSE_GATE}'
        old_gate = json.loads((verified / 'gate.json').read_text())
        assert all(old_gate.values())
        assert digest(verified / 'candidate.ex') == digest(OUT / 'candidate.ex')
        save(OUT / 'reused_gate.json', dict(job=REUSE_GATE, binary_unchanged=True, gate=old_gate))
    else:
        first = run_segment('probe32', 0, 32, 32)
        second = run_segment('probe64', 32, 64, 64, first / 'chk00000032')
    save(OUT / 'gate.json', dict(local_512_write_passed=True, export_sha256_passed=True,
        shared_checkpoint_restart_passed=True, second_export_passed=True))
    print('LOCAL STAGING GATE PASSED', flush=True)
    if not PROBE_ONLY:
        production = OUT / 'production'
        production.mkdir()
        previous = None
        for end in range(12800, 512001, 12800):
            final = run_segment(f'step{end:08d}', end-12800, end, 12800, previous)
            for prefix, width in [('plt_', 6), ('plt_vort_', 6), ('chk', 8)]:
                name = f'{prefix}{end:0{width}d}'
                (production / name).symlink_to(final / name, target_is_directory=True)
            with (production / 'run.log').open('a') as aggregate:
                aggregate.write((final / 'run.log').read_text())
            previous = final / f'chk{end:08d}'
        (OUT / 'completed.txt').write_text('completed step512000 time1000; all exports SHA256 verified\n')
except Exception as error:
    save(OUT / 'failure.json', dict(error=str(error), local_evidence=str(LOCAL),
        last_verified_checkpoint=records[-1] if records else None))
    raise
