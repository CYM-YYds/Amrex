#!/usr/bin/env python3
"""AI 临时分析：复算 Roofline 小测试的计时与逻辑带宽；保留复现依赖时保留。"""
import json
import math
from pathlib import Path
import re
import statistics
import sys


def fields(line):
    return {key: float(value) for key, value in re.findall(
        r"(\w+)=([-+\d.eE]+)", line)}


root = Path(sys.argv[1]).resolve()
status = dict(line.split("\t") for line in (root / "status.tsv").read_text().splitlines()[1:])
rows = []
for repeat in range(1, 4):
    name = f"baseline_{repeat}"
    assert status[name] == "0", (name, status[name])
    lines = (root / name / "run.log").read_text().splitlines()
    assert any("[Precision] Real=FLOAT bytes=4" in line for line in lines)
    assert any("AMReX (26.06) finalized" in line for line in lines)
    final = [fields(line) for line in lines if line.startswith("[FinalState]")]
    assert len(final) == 1 and all(math.isfinite(v) for v in final[0].values())
    # 排除每轮第一个20步窗口；剩余各窗口等长，GPU毫秒按samples加权。
    perf, gpu, launch = {}, {}, {}
    for line in lines:
        match = re.match(r"step(\d+) ", line)
        if not match or int(match[1]) <= 256020:
            continue
        step = int(match[1])
        if " perf(s):" in line:
            perf[step] = fields(line)
        elif " uniform_stage_gpu(ms/step):" in line:
            gpu[step] = fields(line)
        elif " collide_level:" in line:
            launch[step] = fields(line)
    assert set(perf) == set(gpu) == set(launch) == {256040, 256060, 256080, 256100}
    count = sum(p["steps"] for p in perf.values())
    samples = sum(g["samples"] for g in gpu.values())
    assert count == samples == 80
    assert all(p["uniform_nomask_collision"] == p["uniform_reduce_sync"] == 1 for p in perf.values())
    assert all(p["launch_cells"] == 256**3 * 20 and p["calls"] == 20 for p in launch.values())
    collide_s = sum(g["collide_sum"] * g["samples"] / 1000 for g in gpu.values())
    total_s = sum(p["total"] for p in perf.values())
    # D3Q27单精度每cell读27、写27个DDF；这里是逻辑字节，不是DRAM计数器。
    logical_bytes = count * 256**3 * 27 * 4 * 2
    rows.append({"run": name, "steps": count, "total_s": total_s,
                 "total_ms_per_step": total_s * 1000 / count,
                 "collide_gpu_ms_per_step": collide_s * 1000 / samples,
                 "collide_gpu_s": collide_s,
                 "boundary_gpu_ms_per_step": sum(g["boundary"] * g["samples"] for g in gpu.values()) / samples,
                 "mlups_total": count * 256**3 / total_s / 1e6,
                 "logical_bytes": logical_bytes,
                 "logical_effective_GB_s": logical_bytes / collide_s / 1e9,
                 "final_state": final[0],
                 "windows": [{"step_end": step, "total_s": perf[step]["total"], **gpu[step]} for step in sorted(gpu)]})
ncu_text = (root / "ncu_collision/ncu.log").read_text()
summary = {
    "job": root.name, "node": (root / "node.txt").read_text().strip(),
    "status": status, "baseline": rows,
    "ncu_permission_denied": "ERR_NVGPUCTRPERM" in ncu_text,
    "ncu_report_exists": (root / "ncu_collision/collision.ncu-rep").exists(),
    "mean_total_ms_per_step": statistics.mean(r["total_ms_per_step"] for r in rows),
    "mean_collide_gpu_ms_per_step": statistics.mean(r["collide_gpu_ms_per_step"] for r in rows),
    "aggregate_logical_effective_GB_s": sum(r["logical_bytes"] for r in rows) / sum(r["collide_gpu_s"] for r in rows) / 1e9,
    "aggregate_mlups_total": sum(r["steps"] for r in rows) * 256**3 / sum(r["total_s"] for r in rows) / 1e6,
    "limits": "Logical bytes only; no measured DRAM bandwidth, utilization, executed FLOPs or roofline point. Short uniform-grid probe with automatic CUDA-event sampling; no AMR or numerical-equivalence claim."
}
output = root.parent / "summary.json"
output.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
print(json.dumps(summary, indent=2, ensure_ascii=False))
