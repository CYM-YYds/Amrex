#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


REPO_ROOT = Path(__file__).resolve().parents[4]
CASE_ROOT = REPO_ROOT / "projects" / "Cylindertest"
BASE_CASE = CASE_ROOT / "Cylinder2D_IDFtest"
OUT_TSV = BASE_CASE / "delta_kernel_cf_profiles_k12.tsv"
OUT_SUMMARY = BASE_CASE / "delta_kernel_cf_profiles_k12_summary.md"
KEEP_MODES = 12
THETA_TOL = 5.0e-10


RUNS = [
    {
        "kernel": "delta3pSmoothed",
        "source": BASE_CASE / "delta3psmooth_data" / "Cf_steady2_step523000_smooth_k12.dat",
        "smooth": BASE_CASE / "delta3psmooth_data" / "Cf_steady2_step523000_smooth_k12.dat",
        "generate": False,
    },
    {
        "kernel": "delta2p",
        "source": CASE_ROOT / "Cylinder2D_IDFtest_delta2p" / "Cf_steady2_step523000.dat",
        "generate": True,
    },
    {
        "kernel": "delta3p",
        "source": CASE_ROOT / "Cylinder2D_IDFtest_delta3p" / "Cf_steady2_step544000.dat",
        "generate": True,
    },
    {
        "kernel": "delta4p",
        "source": CASE_ROOT / "Cylinder2D_IDFtest_delta4p" / "Cf_steady2_step545000.dat",
        "generate": True,
    },
]


def rel(path: Path) -> str:
    return str(path.relative_to(REPO_ROOT))


def run_command(args: Sequence[str]) -> None:
    print("+ " + " ".join(args))
    subprocess.run(args, cwd=str(REPO_ROOT), check=True)


def derived_paths(source: Path) -> Tuple[Path, Path]:
    retheta = source.with_name(f"{source.stem}_retheta{source.suffix}")
    smooth = source.with_name(f"{source.stem}_smooth_k12{source.suffix}")
    return retheta, smooth


def missing_inputs() -> List[Path]:
    missing: List[Path] = []
    for run in RUNS:
        source = run["source"]
        if not source.exists():
            missing.append(source)
    return missing


def is_stale(output: Path, inputs: Sequence[Path]) -> bool:
    if not output.exists():
        return True
    output_mtime = output.stat().st_mtime
    return any(path.stat().st_mtime > output_mtime for path in inputs)


def ensure_generated_files() -> Dict[str, Path]:
    smooth_paths: Dict[str, Path] = {}
    sort_script = REPO_ROOT / "scripts" / "Cylinder2Dscripts" / "sort_by_redefined_theta.py"
    smooth_script = REPO_ROOT / "scripts" / "Cylinder2Dscripts" / "smooth_cf_profile_folded_periodic.py"

    for run in RUNS:
        kernel = str(run["kernel"])
        source = run["source"]
        if not run["generate"]:
            smooth_paths[kernel] = run["smooth"]
            continue

        retheta, smooth = derived_paths(source)
        if is_stale(retheta, [source]):
            run_command(
                [
                    sys.executable,
                    str(sort_script),
                    str(source),
                    "--output",
                    str(retheta),
                ]
            )
        else:
            print(f"up-to-date: {rel(retheta)}")

        if is_stale(smooth, [source, retheta]):
            run_command(
                [
                    sys.executable,
                    str(smooth_script),
                    str(retheta),
                    "--keep-modes",
                    str(KEEP_MODES),
                    "--output",
                    str(smooth),
                ]
            )
        else:
            print(f"up-to-date: {rel(smooth)}")

        smooth_paths[kernel] = smooth

    return smooth_paths


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
        raise ValueError(f"No column header found in {path}")
    if len(column_names) != len(rows[0]):
        raise ValueError(
            f"Header has {len(column_names)} columns but data has {len(rows[0])} columns in {path}"
        )
    return header_lines, column_names, rows


def column_index(names: Sequence[str], name: str, path: Path) -> int:
    try:
        return list(names).index(name)
    except ValueError as exc:
        raise ValueError(f"Column {name!r} not found in {path}; columns: {', '.join(names)}") from exc


def parse_header_value(header_lines: Sequence[str], key: str) -> str:
    prefix = f"# {key}"
    for line in header_lines:
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1].strip().split()[0]
    return ""


def load_profile(kernel: str, path: Path) -> Dict[str, object]:
    header, names, rows = parse_table(path)
    theta_new_i = column_index(names, "theta_new(rad)", path)
    theta_i = column_index(names, "theta(rad)", path)
    cf_i = column_index(names, "Cf(from_tangent_force)", path)
    cf_smooth_i = column_index(names, "Cf_smooth", path)
    return {
        "kernel": kernel,
        "path": path,
        "header": header,
        "theta_new": [row[theta_new_i] for row in rows],
        "theta": [row[theta_i] for row in rows],
        "cf": [row[cf_i] for row in rows],
        "cf_smooth": [row[cf_smooth_i] for row in rows],
    }


def check_theta_grid(base: Dict[str, object], other: Dict[str, object]) -> None:
    base_theta = base["theta_new"]
    other_theta = other["theta_new"]
    if len(base_theta) != len(other_theta):
        raise ValueError(
            f"Row count mismatch: {base['kernel']} has {len(base_theta)} rows, "
            f"{other['kernel']} has {len(other_theta)} rows"
        )
    for i, (lhs, rhs) in enumerate(zip(base_theta, other_theta), start=1):
        if abs(lhs - rhs) > THETA_TOL:
            raise ValueError(
                f"theta_new mismatch at row {i}: {base['kernel']}={lhs:.12g}, "
                f"{other['kernel']}={rhs:.12g}"
            )


def fmt(value: float) -> str:
    return f"{value:.12g}"


def write_tsv(profiles: Sequence[Dict[str, object]]) -> None:
    base = profiles[0]
    for profile in profiles[1:]:
        check_theta_grid(base, profile)

    columns = ["theta_new(rad)", "theta(rad)"]
    for profile in profiles:
        kernel = profile["kernel"]
        columns.extend([f"{kernel}_Cf", f"{kernel}_Cf_smooth"])

    row_count = len(base["theta_new"])
    with OUT_TSV.open("w", encoding="utf-8") as fh:
        fh.write("\t".join(columns) + "\n")
        for i in range(row_count):
            values: List[str] = [
                fmt(base["theta_new"][i]),
                fmt(base["theta"][i]),
            ]
            for profile in profiles:
                values.append(fmt(profile["cf"][i]))
                values.append(fmt(profile["cf_smooth"][i]))
            fh.write("\t".join(values) + "\n")


def write_summary(profiles: Sequence[Dict[str, object]]) -> None:
    lines = [
        "# Delta Kernel Cf Profiles k12 Summary",
        "",
        f"- Output TSV: `{rel(OUT_TSV)}`",
        f"- Rows: {len(profiles[0]['theta_new'])}",
        f"- keep_modes: {KEEP_MODES}",
        "",
        "| Kernel | Source smooth file | steady step | Cd_raw | Cd_p | Cd_v | cf integral raw | cf integral smooth | cdv-like raw | cdv-like smooth |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for profile in profiles:
        header = profile["header"]
        lines.append(
            "| {kernel} | `{path}` | {step} | {cd_raw} | {cd_p} | {cd_v} | {cf_raw} | {cf_smooth} | {cdv_raw} | {cdv_smooth} |".format(
                kernel=profile["kernel"],
                path=rel(profile["path"]),
                step=parse_header_value(header, "steady_stop_step"),
                cd_raw=parse_header_value(header, "Cd_raw(sum force)"),
                cd_p=parse_header_value(header, "Cd_p(integral Cp)"),
                cd_v=parse_header_value(header, "Cd_v(integral Cf)"),
                cf_raw=parse_header_value(header, "cf_theta_integral_raw"),
                cf_smooth=parse_header_value(header, "cf_theta_integral_smooth"),
                cdv_raw=parse_header_value(header, "cdv_like_abs_sin_raw"),
                cdv_smooth=parse_header_value(header, "cdv_like_abs_sin_smooth"),
            )
        )
    lines.append("")
    OUT_SUMMARY.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    missing = missing_inputs()
    if missing:
        print("Missing input files:", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 1

    smooth_paths = ensure_generated_files()
    profiles = [load_profile(str(run["kernel"]), smooth_paths[str(run["kernel"])]) for run in RUNS]
    write_tsv(profiles)
    write_summary(profiles)
    print(f"wrote: {rel(OUT_TSV)}")
    print(f"wrote: {rel(OUT_SUMMARY)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
