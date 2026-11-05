"""本次C阈值优化版正式重跑编排；复用既有本地落盘/校验回传入口，保留以复现。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

CASE = Path(__file__).resolve().parents[3]
REPO = CASE.parents[3]
EXP = CASE / 'runs/C_optimized_t1000_20261105'
JOB = os.environ['CCS_JOB_ID']
OUT = EXP / f'job{JOB}'
OUT.mkdir()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    # 当前工作树已在提交前冻结；作业不再读取正式src构建。
    for name in ('inputs', 'git_head.txt', 'worktree_source.diff', 'experiment.diff', 'purpose.json', 'run_rank.sh'):
        shutil.copy2(EXP / name, OUT / name)
    for folder in ('src', 'config'):
        shutil.copytree(EXP / 'build' / folder, OUT / folder)
    shutil.copytree(Path(__file__).parent, OUT / 'driver')
    (OUT / 'node.txt').write_bytes(subprocess.check_output(['hostname']))
    (OUT / 'gpu.txt').write_bytes(subprocess.check_output(['nvidia-smi']))
    roots = [p for p in (Path('/var/tmp'), Path('/tmp')) if p.is_dir() and os.access(p, os.W_OK)]
    local = max(roots, key=lambda p: shutil.disk_usage(p).free)
    save(OUT / 'local_capacity.json', dict(path=str(local), free_bytes=shutil.disk_usage(local).free, segment_steps=32000))
    assert shutil.disk_usage(local).free > 12 * 1024**3, '32000step AMR segment requires 12 GiB local free space'
    with (OUT / 'compile.log').open('w') as log:
        subprocess.run(['make', '-j8', 'DIM=3', 'PRECISION=FLOAT'], cwd=EXP / 'build', stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(EXP / 'build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex', OUT / 'application.ex')
    shutil.copy2(OUT / 'worktree_source.diff', OUT / 'source.diff')
    save(OUT / 'fingerprints.json', {str(p.relative_to(OUT)): digest(p) for folder in ('src', 'config') for p in (OUT / folder).rglob('*') if p.is_file()} | {'application.ex': digest(OUT / 'application.ex'), 'inputs': digest(OUT / 'inputs')})
    base = dict(PROBE_SNAPSHOT_ROOT=str(OUT), PROBE_RANK_SCRIPT=str(OUT / 'run_rank.sh'),
                PROBE_CASES='C', PROBE_STREAM_MODES='1 0', PROBE_CONVERGENCE='true',
                PROBE_CONVERGENCE_STOP='false', PROBE_REFINEMENT_DIAGNOSTICS='false',
                PROBE_PROFILE='false', PROBE_WALL_SECONDS='21600', PROBE_CHECKPOINT_INT='32000',
                AMREX_RUN_ARGS='amr.max_grid_size=128 256 512 ibm.enabled=false lbm.flow_boundary=cavity lbm.reference_velocity=0.05 lbm.reference_length=1 verification.regrid_valid_check=true')
    # 入口已有启动、rank回执、本地写出和逐文件SHA回读；这里只组合新的构建/检查/长跑流程。
    def launch(stage, end, plot, checkpoint, modes='1 0', begin=0, previous=None):
        settings = dict(base)
        if stage == 'gate':
            settings.update(PROBE_CONVERGENCE='false', AMREX_RUN_ARGS=base['AMREX_RUN_ARGS'] + ' performance.report_int=64')
        settings_file = OUT / f'{stage}_settings.json'
        save(settings_file, settings)
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(('IO_', 'PROBE_')) or key == 'AMREX_RUN_ARGS':
                env.pop(key)
        env.update(IO_OUTPUT_ROOT=str(OUT / stage), IO_PATH_TRIALS='local_original',
                   IO_SETTINGS_FILE=str(settings_file), IO_END_STEP=str(end), IO_PLOT_INT=str(plot),
                   IO_CHECKPOINT_INT=str(checkpoint), IO_STREAM_MODES=modes, IO_TRACE_IO='0',
                   IO_MEMORY_MS='100', IO_WALL_SECONDS='21600', IO_LOCAL_MIN_FREE_GIB='12')
        if previous is not None:
            restart = OUT / f'resume_{stage}'
            restart.mkdir()
            for name in ('application.ex', 'inputs', 'source.diff', 'git_head.txt', 'src', 'config'):
                (restart / name).symlink_to(OUT / name, target_is_directory=(OUT / name).is_dir())
            (restart / 'C').symlink_to(previous, target_is_directory=True)
            env.update(IO_RESTART_ROOT=str(restart), IO_RESTART_STEP=str(begin))
        with (OUT / f'{stage}.log').open('w') as log:
            subprocess.run([sys.executable, str(CASE / 'scripts/temporary/checkpoint_io/paired_paths.py')], env=env, cwd=CASE, stdout=log, stderr=subprocess.STDOUT, check=True)
        trial = OUT / stage / f'job{JOB}'
        published = trial / 'local_original'
        status = json.loads((trial / 'status.json').read_text())
        assert all(x['returncode'] == 0 and 'publish_error' not in x for x in status)
        assert (published / 'transfer_manifest.json').is_file()
        # 只清理本次入口创建、且已逐文件回读校验成功的节点临时目录。
        local_evidence = Path(json.loads((trial / 'local_disk.json').read_text())['path'])
        assert local_evidence.name.startswith(f'C_AB_IO_{JOB}_') and local_evidence.parent in (Path('/tmp'), Path('/var/tmp'))
        shutil.rmtree(local_evidence)
        return published

    gate = launch('gate', 128, 64, 64)
    comparison = OUT / 'gate_valid_comparison.json'
    subprocess.run([sys.executable, str(CASE / 'scripts/temporary/checkpoint_io/compare_checkpoints.py'),
                    str(gate / 'C_mode0/chk00000128'), str(gate / 'C_mode1/chk00000128'), str(comparison)], check=True)
    rows = json.loads(comparison.read_text())['levels']
    assert rows and all(r['same_box_order'] and r['unequal_values'] == 0 and r['nonfinite_values'] == 0 for r in rows)
    (OUT / 'gate_passed.txt').write_text('128step from-zero all-valid DDF exact; both output paths SHA verified\n')
    production = OUT / 'production'
    production.mkdir()
    progress = []
    for mode in (1, 0):
        aggregate = production / f'C_mode{mode}'
        aggregate.mkdir()
        previous = None
        for end in range(32000, 128001, 32000):
            stage = f'production_mode{mode}_step{end:08d}'
            published = launch(stage, end, 3200, 32000, str(mode), end-32000, previous)
            current = published / f'C_mode{mode}'
            for prefix in ('plt_[0-9]*', 'plt_vort_[0-9]*', 'chk[0-9]*'):
                for field in current.glob(prefix):
                    (aggregate / field.name).symlink_to(field, target_is_directory=True)
            with (aggregate / 'run.log').open('a') as log:
                log.write((current / 'run.log').read_text())
            shutil.copy2(current / 'exit_status.json', aggregate / 'exit_status.json')
            previous = current
            progress.append(dict(mode=mode, begin=end-32000, end=end, published=str(current), transfer_verified=True))
            save(OUT / 'progress.json', progress)
    subprocess.run([sys.executable, str(Path(__file__).with_name('summarize.py')), str(OUT), str(production)], check=True)
    (OUT / 'completed.txt').write_text('both modes step128000/time1000;40frames each;4checkpoints each;all transfer SHA verified\n')


try:
    main()
except Exception as error:
    save(OUT / 'failure.json', dict(error=repr(error), note='保留所有本地及共享现场；查看gate/production子目录status与local_disk记录'))
    raise
