"""读取当前算例的收敛日志，并按绝对步数合并续跑记录。"""

from __future__ import annotations

import re
from pathlib import Path


TREND = re.compile(
    r"CONVERGENCE_TREND step=(\d+) interval=(\d+) "
    r"velocity_l2_relative=([0-9.eE+-]+) velocity_l2=([0-9.eE+-]+) "
    r"delta_velocity_l2=([0-9.eE+-]+)$"
)
FORMAL = re.compile(
    r"CONVERGENCE step=(\d+) interval=(\d+) "
    r"velocity_l2_relative=([0-9.eE+-]+) velocity_l2=([0-9.eE+-]+) "
    r"delta_velocity_l2=([0-9.eE+-]+) tolerance=([0-9.eE+-]+) "
    r"consecutive=(\d+)/(\d+) converged=([01])$"
)


def read_records(logs: list[Path]) -> tuple[list[tuple], list[tuple]]:
    """后面的日志覆盖同一步记录；缺字段时立即报错。"""
    trend_by_step: dict[int, tuple] = {}
    formal_by_step: dict[int, tuple] = {}
    for log in logs:
        for line_number, line in enumerate(log.read_text(errors="replace").splitlines(), 1):
            if line.startswith("CONVERGENCE_TREND "):
                match = TREND.fullmatch(line)
                if match is None:
                    raise ValueError(f"{log}:{line_number}: incomplete trend record")
                step, interval, relative, velocity, delta = match.groups()
                trend_by_step[int(step)] = (
                    int(step), int(interval), float(relative), float(velocity), float(delta)
                )
            elif line.startswith("CONVERGENCE "):
                match = FORMAL.fullmatch(line)
                if match is None:
                    raise ValueError(f"{log}:{line_number}: incomplete formal record")
                step, interval, relative, velocity, delta, tolerance, count, required, converged = match.groups()
                formal_by_step[int(step)] = (
                    int(step), int(interval), float(relative), float(velocity),
                    float(delta), float(tolerance), int(count), int(required), bool(int(converged)),
                )
    if not trend_by_step or not formal_by_step:
        raise ValueError("No complete convergence trend and formal records were found")
    return (
        [trend_by_step[step] for step in sorted(trend_by_step)],
        [formal_by_step[step] for step in sorted(formal_by_step)],
    )
