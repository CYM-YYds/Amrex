#!/usr/bin/env python3
"""汇总本次 A5 阈值初测；保留用于报告复现，不再需要时可删除。"""
import argparse
import csv
import json
from pathlib import Path


def fields(line):
    return dict(item.split("=", 1) for item in line.split() if "=" in item)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    records = []
    checks = {}
    for name in ("A", "B", "C"):
        directory = args.run / name
        if not (directory / "run.log").exists():
            continue
        text = (directory / "run.log").read_text(errors="replace")
        time = None
        current = {}
        snapshots = []
        regrids = 0
        vort = []
        finite = []
        final = []
        threshold_checks = []
        limits = {"A": (0.0625, 0.125), "B": (0.125, 0.25), "C": (0.25, 0.5)}[name]
        for line in text.splitlines():
            if line.startswith("REFINEMENT_VORT "):
                vort.append(fields(line))
            elif line.startswith("REFINEMENT_CDF ") and vort:
                item = fields(line)
                lev = int(item["lev"])
                if lev < 2 and float(item["threshold"]) == limits[lev]:
                    raw = vort[-1]
                    threshold_checks.append(
                        raw["lev"] == item["lev"] and raw["time"] == item["time"]
                        and raw["tag_cells"] == item["ge_cells"])
            elif line.startswith("REFINEMENT_REGRID "):
                regrids += 1
                time = float(fields(line)["time"])
                current = {}
            elif line.startswith("REGRID_DIAG lev=") and time is not None:
                item = fields(line)
                lev = int(item["lev"])
                current[lev] = item
                cells = int(item["valid_cells"])
                domain = (128 * 2**lev)**3
                records.append(dict(case=name, time=time, level=lev,
                                    boxes=int(item["boxes"]), valid_cells=cells,
                                    domain_cells=domain, coverage=cells/domain,
                                    raw_tags=item.get("tag_cells", "")))
                if lev == 2:
                    snapshots.append((time, dict(current)))
            elif line.startswith("regrid_valid_check "):
                finite.append(fields(line))
            if "FinalState" in line:
                final.append(line)
        summary = dict(regrids=regrids,
                       all_regrid_ddf_finite=bool(finite) and all(
                           v["all_valid_finite"] == "1" for v in finite),
                       regrid_finite_checks=len(finite),
                       threshold_count_checks=len(threshold_checks),
                       threshold_counts_match=bool(threshold_checks) and all(threshold_checks),
                       vorticity_nonfinite=sum(int(v["nonfinite"]) for v in vort),
                       final_state=final,
                       finalized="finalized" in text)
        guard = directory / "exit_status.json"
        if guard.exists():
            summary["guard"] = json.loads(guard.read_text())
        checkpoint = directory / "chk00003200" / "Header"
        if checkpoint.exists():
            header = checkpoint.read_text().splitlines()
            summary["checkpoint"] = dict(step=int(header[2]), time=float(header[3]),
                                         finest_level=int(header[4]))
        if snapshots:
            time, last = snapshots[-1]
            summary["last_regrid_time"] = time
            summary["levels"] = {}
            for lev, item in last.items():
                value = dict(item)
                value["coverage"] = int(item["valid_cells"])/(128*2**lev)**3
                summary["levels"][lev] = value
            summary["last_raw_vorticity"] = {
                lev: next((v for v in reversed(vort) if int(v["lev"]) == lev), None)
                for lev in (0, 1)}
        checks[name] = summary
    if records:
        with (args.run / "coverage.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    (args.run / "summary.json").write_text(json.dumps(checks, indent=2)+"\n")
    print(json.dumps(checks, indent=2))


if __name__ == "__main__":
    main()
