#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Tuple


def load_cd_history(path: Path) -> List[Tuple[int, float]]:
    records: List[Tuple[int, float]] = []
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                step = int(parts[0])
                cd = float(parts[1])
            except ValueError:
                continue
            records.append((step, cd))
    if not records:
        raise ValueError(f"No usable step/Cd data found in {path}")
    return records


def sample_every(records: List[Tuple[int, float]], interval: int) -> List[Tuple[int, float]]:
    samples: List[Tuple[int, float]] = []
    for step, cd in records:
        if step % interval == 0:
            samples.append((step, cd))
    return samples


def first_converged_step(samples: List[Tuple[int, float]], rel_tol: float) -> Tuple[int, float, int, float, float] | None:
    for (prev_step, prev_cd), (step, cd) in zip(samples, samples[1:]):
        if prev_cd == 0.0:
            continue
        rel_change = abs(cd - prev_cd) / abs(prev_cd)
        if rel_change < rel_tol:
            return step, cd, prev_step, prev_cd, rel_change
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Detect the first converged step from CdCl.dat using sampled Cd changes."
    )
    parser.add_argument("input", type=Path, help="Path to CdCl.dat")
    parser.add_argument(
        "--interval",
        type=int,
        default=1000,
        help="Sampling interval in steps (default: 1000)",
    )
    parser.add_argument(
        "--rel-tol",
        type=float,
        default=1e-3,
        help="Relative Cd change tolerance (default: 1e-3, i.e. 0.1%%)",
    )
    args = parser.parse_args()

    records = load_cd_history(args.input)
    samples = sample_every(records, args.interval)
    if len(samples) < 2:
        raise ValueError(
            f"Need at least two sampled points at interval {args.interval}, found {len(samples)}"
        )

    result = first_converged_step(samples, args.rel_tol)

    print(f"input={args.input}")
    print(f"records={len(records)} sampled_points={len(samples)} interval={args.interval}")
    print(f"criterion=abs(Cd_n - Cd_n-1) / abs(Cd_n-1) < {args.rel_tol:.6g}")

    if result is None:
        print("converged=no")
        return 1

    step, cd, prev_step, prev_cd, rel_change = result
    print("converged=yes")
    print(f"previous_sample_step={prev_step} previous_cd={prev_cd:.12g}")
    print(f"converged_step={step} converged_cd={cd:.12g}")
    print(f"relative_change={rel_change:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
