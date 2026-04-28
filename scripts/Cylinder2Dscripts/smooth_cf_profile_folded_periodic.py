#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Sequence

from smooth_cf_profile import (
    default_output_path,
    format_float,
    fourier_lowpass,
    parse_table,
    periodic_integral,
    resolve_column,
    weighted_abs_sin_integral,
)


def unfold_negative_theta(theta: Sequence[float], values: Sequence[float]) -> List[float]:
    if len(theta) != len(values):
        raise ValueError("theta and values must have the same length")
    return [(-value if th < 0.0 else value) for th, value in zip(theta, values)]


def refold_negative_theta(theta: Sequence[float], values: Sequence[float]) -> List[float]:
    if len(theta) != len(values):
        raise ValueError("theta and values must have the same length")
    return [(-value if th < 0.0 else value) for th, value in zip(theta, values)]


def scale_values(values: Sequence[float], scale: float) -> List[float]:
    return [value * scale for value in values]


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
) -> None:
    with path.open("w", encoding="utf-8") as fh:
        for line in header_lines:
            if line.lstrip().startswith("# theta"):
                continue
            fh.write(f"{line}\n")

        fh.write("# smoothing_method      = folded_periodic_fourier_lowpass\n")
        fh.write(f"# keep_modes            = {keep_modes}\n")
        fh.write("# unfold_rule           = negate theta<0 before periodic Fourier\n")
        fh.write("# refold_rule           = negate theta<0 after periodic Fourier\n")
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
            "Smooth a folded tx>=0 Cf(theta) profile by first negating the theta<0 side "
            "to recover a signed periodic signal, then applying periodic Fourier low-pass "
            "filtering, and finally negating theta<0 again to restore the folded plotting convention."
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
        "--preserve-cdv-like",
        action="store_true",
        help="Scale the final folded Cf_smooth so integral Cf_smooth*abs(sin(theta)) dtheta matches the raw value",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    header_lines, column_names, rows = parse_table(args.input)
    theta_index = resolve_column("theta", column_names)
    cf_index = resolve_column(args.column, column_names)

    theta = [row[theta_index] for row in rows]
    cf_folded = [row[cf_index] for row in rows]
    cf_signed = unfold_negative_theta(theta, cf_folded)
    cf_signed_smooth = fourier_lowpass(cf_signed, args.keep_modes)
    cf_folded_smooth = refold_negative_theta(theta, cf_signed_smooth)

    cf_integral_before = periodic_integral(theta, cf_folded)
    cdv_like_before = weighted_abs_sin_integral(theta, cf_folded)

    if args.preserve_cdv_like:
        cdv_like_after_unscaled = weighted_abs_sin_integral(theta, cf_folded_smooth)
        if cdv_like_after_unscaled != 0.0:
            scale = cdv_like_before / cdv_like_after_unscaled
            cf_folded_smooth = scale_values(cf_folded_smooth, scale)

    cf_integral_after = periodic_integral(theta, cf_folded_smooth)
    cdv_like_after = weighted_abs_sin_integral(theta, cf_folded_smooth)

    output = args.output if args.output is not None else default_output_path(args.input)
    write_output(
        output,
        header_lines,
        column_names,
        rows,
        cf_folded_smooth,
        args.keep_modes,
        cf_integral_before,
        cf_integral_after,
        cdv_like_before,
        cdv_like_after,
        args.preserve_cdv_like,
    )

    print(f"input={args.input}")
    print(f"output={output}")
    print(f"rows={len(rows)} keep_modes={args.keep_modes}")
    print("method=folded-periodic-fourier")
    print(f"cf_theta_integral_raw={format_float(cf_integral_before)}")
    print(f"cf_theta_integral_smooth={format_float(cf_integral_after)}")
    print(f"cdv_like_abs_sin_raw={format_float(cdv_like_before)}")
    print(f"cdv_like_abs_sin_smooth={format_float(cdv_like_after)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
