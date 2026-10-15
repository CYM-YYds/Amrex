#!/usr/bin/env python3
"""逐步核对锁步诊断的字段、层级和阶段完整性。"""

import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import sys

STEP = re.compile(r"^STEP (\d+)starts")
FIELD = re.compile(r"(?:^|\s)([A-Za-z_]+)=([^\s]+)")
REQUIRED = (
    "Initial", "Collision", "Communication", "CommunicationGhost",
    "Stream", "BoundaryUncovered", "Swap",
)


def fields(line, names):
    found = dict(FIELD.findall(line))
    missing = set(names) - found.keys()
    if missing:
        raise ValueError(f"缺少字段 {sorted(missing)}: {line}")
    return found


def read_log(path, expected_steps):
    current = None
    step_data = defaultdict(lambda: {"begin": [], "finite": [], "stages": [], "sources": [], "done": []})
    signatures = Counter()
    finalized = False
    for line in path.read_text(errors="replace").splitlines():
        match = STEP.match(line)
        if match:
            current = int(match.group(1))
            step_data[current]
            continue
        if "AMReX (26.06) finalized" in line:
            finalized = True
        if line.startswith("osi_ab_uncovered_begin:"):
            f = fields(line, ("step", "stage", "finest_level"))
            step = int(f["step"])
            if step != current:
                raise ValueError(f"步骤边界错误: {line}")
            step_data[step]["begin"].append(int(f["finest_level"]))
        elif line.startswith("osi_ab_uncovered_finite:"):
            f = fields(line, ("step", "stage", "lev", "finite"))
            step = int(f["step"])
            if step != current or f["finite"] != "1":
                raise ValueError(f"有限值或步骤错误: {line}")
            if f["stage"] != "StepEntryUncovered":
                raise ValueError(f"有限值阶段错误: {line}")
            step_data[step]["finite"].append(int(f["lev"]))
        elif line.startswith("osi_ab_stage:"):
            f = fields(line, ("stage", "lev", "phase", "linf"))
            if current is None or float(f["linf"]) != 0.0:
                raise ValueError(f"阶段缺失步骤或 linf 非零: {line}")
            lev, phase = int(f["lev"]), int(f["phase"])
            step_data[current]["stages"].append((lev, f["stage"], phase))
            signatures[(current, lev, f["stage"], phase)] += 1
        elif line.startswith("osi_ab_stream_sources:"):
            f = fields(line, (
                "stage", "lev", "phase", "targets", "sources", "source_valid",
                "source_ghost", "source_physical_ghost", "source_internal_ghost",
                "source_interface", "source_covered", "mismatches", "mismatch_valid",
                "mismatch_ghost", "mismatch_physical_ghost", "mismatch_internal_ghost",
                "mismatch_interface", "mismatch_covered", "linf",
            ))
            if current is None or f["stage"] != "AfterCommunication":
                raise ValueError(f"通信源记录缺少步骤或阶段错误: {line}")
            step_data[current]["sources"].append(int(f["lev"]))
        elif line.startswith("osi_ab: step="):
            f = fields(line, ("step", "phase", "staged_check"))
            if int(f["step"]) != current or f["staged_check"] != "passed":
                raise ValueError(f"锁步完成记录错误: {line}")
            step_data[current]["done"].append(int(f["phase"]))
    if not finalized:
        raise ValueError("缺少 AMReX finalized")
    expected = set(range(1, expected_steps + 1))
    if set(step_data) != expected:
        raise ValueError(f"步骤不完整: expected={sorted(expected)} actual={sorted(step_data)}")
    for step, data in step_data.items():
        if len(data["begin"]) != 1 or len(data["done"]) != 1:
            raise ValueError(f"step={step} 缺少唯一入口或完成记录")
        finest = data["begin"][0]
        if sorted(data["finite"]) != list(range(finest + 1)):
            raise ValueError(f"step={step} 有限值层级不完整")
        counts = Counter((lev, stage) for lev, stage, _ in data["stages"])
        for lev in range(finest + 1):
            if counts[(lev, "StepEntryUncovered")] != 1:
                raise ValueError(f"step={step} lev={lev} 入口 uncovered 检查不完整")
        for lev in {lev for lev, _, _ in data["stages"]}:
            passes = counts[(lev, "Initial")]
            if passes == 0:
                raise ValueError(f"step={step} lev={lev} 缺少 Initial")
            for stage in REQUIRED:
                if counts[(lev, stage)] != passes:
                    raise ValueError(f"step={step} lev={lev} stage={stage} 应有 {passes} 条，实际 {counts[(lev, stage)]}")
            if data["sources"].count(lev) != passes:
                raise ValueError(f"step={step} lev={lev} 通信源记录不完整")
            if lev > 0 and counts[(lev, "InterpolationGhost")] != passes:
                raise ValueError(f"step={step} lev={lev} 插值 ghost 检查不完整")
    return signatures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    actual = read_log(args.log, args.steps)
    if args.reference:
        reference = read_log(args.reference, args.steps)
        # 旧日志的 Boundary 与 BoundaryUncovered 参数相同，前者已删除。
        reference = Counter({key: count for key, count in reference.items()
                             if key[2] != "Boundary"})
        if actual != reference:
            missing = reference - actual
            extra = actual - reference
            raise ValueError(f"阶段序列与基线不同: missing={missing.most_common(5)} extra={extra.most_common(5)}")
    print(f"PASS {args.log}: {args.steps} steps, {sum(actual.values())} complete stage records")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
