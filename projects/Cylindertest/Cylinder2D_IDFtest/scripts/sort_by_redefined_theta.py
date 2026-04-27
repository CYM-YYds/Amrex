#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import List, Sequence, Tuple


def default_output_path(path: Path) -> Path:
    return path.with_name(f"{path.stem}_retheta{path.suffix}")


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
                if maybe_columns and any("theta" in name for name in maybe_columns):
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


def redefined_theta(theta: float) -> float:
    return math.fmod(math.fmod(math.pi - theta + math.pi, 2.0 * math.pi) + 2.0 * math.pi, 2.0 * math.pi) - math.pi


def format_float(value: float) -> str:
    return f"{value:.12g}"


def write_output(
    path: Path,
    header_lines: Sequence[str],
    column_names: Sequence[str],
    sorted_rows: Sequence[Tuple[float, Sequence[float]]],
) -> None:
    with path.open("w", encoding="utf-8") as fh:
        for line in header_lines:
            if line.lstrip().startswith("# theta"):
                continue
            fh.write(f"{line}\n")
        fh.write("# redefined_theta_formula = (mod(pi - theta(rad) + pi, 2*pi) - pi)\n")
        fh.write("# sort_key = theta_new(rad), ascending\n")
        fh.write("# " + "\t".join(["theta_new(rad)", *column_names]) + "\n")
        for theta_new, row in sorted_rows:
            values = [theta_new, *row]
            fh.write("\t".join(format_float(value) for value in values) + "\n")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Sort a .dat table by theta_new = mod(pi - theta(rad) + pi, 2*pi) - pi. "
            "The original theta column and all original data columns are preserved."
        )
    )
    parser.add_argument("input", type=Path, help="Input .dat file")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output file path. Defaults to <input_stem>_retheta.dat",
    )
    parser.add_argument(
        "--theta-column",
        default="theta(rad)",
        help="Theta column name, partial name, or 1-based index. Default: theta(rad)",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    header_lines, column_names, rows = parse_table(args.input)
    theta_index = resolve_column(args.theta_column, column_names)
    sorted_rows = sorted(
        ((redefined_theta(row[theta_index]), row) for row in rows),
        key=lambda item: item[0],
    )
    write_output(args.output or default_output_path(args.input), header_lines, column_names, sorted_rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
