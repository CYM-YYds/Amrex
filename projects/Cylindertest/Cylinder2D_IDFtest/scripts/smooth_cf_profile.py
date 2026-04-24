#!/usr/bin/env python3

from __future__ import annotations

import argparse
import cmath
import math
from pathlib import Path
from typing import List, Sequence, Tuple


def default_output_path(path: Path) -> Path:
    return path.with_name(f"{path.stem}_smooth{path.suffix}")


def parse_table(path: Path) -> Tuple[List[str], List[str], List[List[float]]]:
    header_lines: List[str] = []
    column_names: List[str] = []
    rows: List[List[float]] = []

    with path.open("r", encoding="utf-8") as fh:
        for raw_line in fh:
            line = raw_line.rstrip("\n")
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith("#"):
                header_lines.append(line)
                maybe_columns = stripped.lstrip("#").strip().split()
                if maybe_columns and maybe_columns[0].startswith("theta"):
                    column_names = maybe_columns
                continue
            try:
                rows.append([float(part) for part in stripped.split()])
            except ValueError as exc:
                raise ValueError(f"Could not parse numeric row in {path}: {line}") from exc

    if not rows:
        raise ValueError(f"No numeric rows found in {path}")
    if any(len(row) != len(rows[0]) for row in rows):
        raise ValueError(f"Inconsistent numeric column count in {path}")
    if not column_names:
        column_names = [f"col{i + 1}" for i in range(len(rows[0]))]
    if len(column_names) != len(rows[0]):
        raise ValueError(
            f"Header has {len(column_names)} columns but data has {len(rows[0])} columns in {path}"
        )
    return header_lines, column_names, rows


def resolve_column(column: str, column_names: Sequence[str]) -> int:
    if column.isdigit():
        index = int(column) - 1
        if index < 0 or index >= len(column_names):
            raise ValueError(f"Column index {column} is outside 1..{len(column_names)}")
        return index

    for i, name in enumerate(column_names):
        if name == column:
            return i

    matches = [i for i, name in enumerate(column_names) if column in name]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise ValueError(f"Column {column!r} not found. Available columns: {', '.join(column_names)}")
    raise ValueError(f"Column {column!r} is ambiguous. Available columns: {', '.join(column_names)}")


def fourier_lowpass(values: Sequence[float], keep_modes: int) -> List[float]:
    n = len(values)
    if n < 3:
        raise ValueError("Need at least 3 points for Fourier smoothing")
    if keep_modes < 0:
        raise ValueError("--keep-modes must be non-negative")
    max_modes = n // 2
    if keep_modes > max_modes:
        raise ValueError(f"--keep-modes must be <= {max_modes} for {n} points")

    try:
        import numpy as np  # type: ignore
    except ImportError:
        return fourier_lowpass_pure_python(values, keep_modes)

    spectrum = np.fft.fft(np.asarray(values, dtype=float))
    filtered = np.zeros_like(spectrum)
    filtered[0] = spectrum[0]
    for k in range(1, keep_modes + 1):
        filtered[k] = spectrum[k]
        filtered[-k] = spectrum[-k]
    return np.fft.ifft(filtered).real.tolist()


def fourier_lowpass_pure_python(values: Sequence[float], keep_modes: int) -> List[float]:
    n = len(values)
    spectrum: List[complex] = []
    for k in range(n):
        total = 0.0j
        for j, value in enumerate(values):
            angle = -2.0 * math.pi * k * j / n
            total += value * cmath.exp(1.0j * angle)
        spectrum.append(total)

    filtered = [0.0j for _ in range(n)]
    filtered[0] = spectrum[0]
    for k in range(1, keep_modes + 1):
        filtered[k] = spectrum[k]
        filtered[-k] = spectrum[-k]

    smoothed: List[float] = []
    for j in range(n):
        total = 0.0j
        for k, value in enumerate(filtered):
            angle = 2.0 * math.pi * k * j / n
            total += value * cmath.exp(1.0j * angle)
        smoothed.append((total / n).real)
    return smoothed


def split_fourier_lowpass(theta: Sequence[float], values: Sequence[float], keep_modes: int) -> List[float]:
    if len(theta) != len(values):
        raise ValueError("theta and values must have the same length")

    smoothed = list(values)
    negative_indices = [i for i, th in enumerate(theta) if th < 0.0]
    positive_indices = [i for i, th in enumerate(theta) if th > 0.0]

    for indices in (negative_indices, positive_indices):
        if not indices:
            continue
        side_values = [values[i] for i in indices]
        side_keep_modes = min(keep_modes, len(side_values) // 2)
        side_smoothed = fourier_lowpass(side_values, side_keep_modes)
        for index, value in zip(indices, side_smoothed):
            smoothed[index] = value

    return smoothed


def periodic_integral(theta: Sequence[float], values: Sequence[float]) -> float:
    if len(theta) != len(values):
        raise ValueError("theta and values must have the same length")
    if len(theta) < 2:
        return 0.0

    total = 0.0
    for i in range(len(theta) - 1):
        total += 0.5 * (values[i] + values[i + 1]) * (theta[i + 1] - theta[i])

    last_step = theta[1] - theta[0]
    period_end = theta[0] + 2.0 * math.pi
    closing_width = period_end - theta[-1]
    if closing_width <= 0.0 or closing_width > 2.0 * abs(last_step):
        closing_width = last_step
    total += 0.5 * (values[-1] + values[0]) * closing_width
    return total


def weighted_abs_sin_integral(theta: Sequence[float], values: Sequence[float]) -> float:
    weighted = [value * abs(math.sin(th)) for th, value in zip(theta, values)]
    return periodic_integral(theta, weighted)


def scaled_to_preserve(reference: Sequence[float], smoothed: Sequence[float], scale: float) -> List[float]:
    if scale == 0.0:
        return list(smoothed)
    return [value * scale for value in smoothed]


def format_float(value: float) -> str:
    return f"{value:.12g}"


def write_output(
    path: Path,
    header_lines: Sequence[str],
    column_names: Sequence[str],
    rows: Sequence[Sequence[float]],
    smoothed: Sequence[float],
    keep_modes: int,
    cf_integral_before: float,
    cf_integral_after: float,
    cdv_like_before: float,
    cdv_like_after: float,
    preserve_cdv_like: bool,
    method: str,
) -> None:
    with path.open("w", encoding="utf-8") as fh:
        for line in header_lines:
            if line.lstrip().startswith("# theta"):
                continue
            fh.write(f"{line}\n")

        smoothing_method = "split_fourier_lowpass" if method == "split-fourier" else "fourier_lowpass"
        fh.write(f"# smoothing_method      = {smoothing_method}\n")
        fh.write(f"# keep_modes            = {keep_modes}\n")
        if method == "split-fourier":
            fh.write("# split_boundary        = theta=0, no cross-boundary filtering\n")
        fh.write(f"# preserve_cdv_like     = {'yes' if preserve_cdv_like else 'no'}\n")
        fh.write(f"# cf_theta_integral_raw = {format_float(cf_integral_before)}\n")
        fh.write(f"# cf_theta_integral_smooth = {format_float(cf_integral_after)}\n")
        fh.write(f"# cdv_like_abs_sin_raw  = {format_float(cdv_like_before)}\n")
        fh.write(f"# cdv_like_abs_sin_smooth = {format_float(cdv_like_after)}\n")
        fh.write("# " + "\t".join([*column_names, "Cf_smooth"]) + "\n")
        for row, smooth_value in zip(rows, smoothed):
            fh.write("\t".join(format_float(value) for value in [*row, smooth_value]) + "\n")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Smooth a cylinder Cf(theta) profile with Fourier low-pass filtering. "
            "The default split mode is intended for tx>=0 folded Cf definitions. "
            "The original columns are preserved and a Cf_smooth column is appended."
        )
    )
    parser.add_argument("input", type=Path, help="Input Cf_steady2*.dat file")
    parser.add_argument(
        "--output",
        type=Path,
        help="Output path (default: append _smooth before the suffix)",
    )
    parser.add_argument(
        "--column",
        default="Cf",
        help="Cf column name substring or 1-based index (default: Cf)",
    )
    parser.add_argument(
        "--keep-modes",
        type=int,
        default=32,
        help="Number of positive Fourier modes to keep, plus the mean mode (default: 32)",
    )
    parser.add_argument(
        "--method",
        choices=("split-fourier", "periodic-fourier"),
        default="split-fourier",
        help=(
            "Smoothing method. split-fourier filters theta<0 and theta>0 separately "
            "for tx>=0 folded Cf profiles; periodic-fourier treats the full profile "
            "as one periodic signal (default: split-fourier)."
        ),
    )
    parser.add_argument(
        "--preserve-cdv-like",
        action="store_true",
        help="Scale Cf_smooth so integral Cf_smooth*abs(sin(theta)) dtheta matches the raw value",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    header_lines, column_names, rows = parse_table(args.input)
    theta_index = resolve_column("theta", column_names)
    cf_index = resolve_column(args.column, column_names)

    theta = [row[theta_index] for row in rows]
    cf = [row[cf_index] for row in rows]
    if args.method == "split-fourier":
        smoothed = split_fourier_lowpass(theta, cf, args.keep_modes)
    else:
        smoothed = fourier_lowpass(cf, args.keep_modes)

    cf_integral_before = periodic_integral(theta, cf)
    cdv_like_before = weighted_abs_sin_integral(theta, cf)

    if args.preserve_cdv_like:
        cdv_like_after_unscaled = weighted_abs_sin_integral(theta, smoothed)
        if cdv_like_after_unscaled != 0.0:
            smoothed = scaled_to_preserve(cf, smoothed, cdv_like_before / cdv_like_after_unscaled)

    cf_integral_after = periodic_integral(theta, smoothed)
    cdv_like_after = weighted_abs_sin_integral(theta, smoothed)

    output = args.output if args.output is not None else default_output_path(args.input)
    write_output(
        output,
        header_lines,
        column_names,
        rows,
        smoothed,
        args.keep_modes,
        cf_integral_before,
        cf_integral_after,
        cdv_like_before,
        cdv_like_after,
        args.preserve_cdv_like,
        args.method,
    )

    print(f"input={args.input}")
    print(f"output={output}")
    print(f"rows={len(rows)} keep_modes={args.keep_modes}")
    print(f"method={args.method}")
    print(f"cf_theta_integral_raw={format_float(cf_integral_before)}")
    print(f"cf_theta_integral_smooth={format_float(cf_integral_after)}")
    print(f"cdv_like_abs_sin_raw={format_float(cdv_like_before)}")
    print(f"cdv_like_abs_sin_smooth={format_float(cdv_like_after)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
