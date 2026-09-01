#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <osi-log> <ab-log>" >&2
  exit 2
fi

osi_log=$1
ab_log=$2
for log in "$osi_log" "$ab_log"; do
  if [[ ! -r "$log" ]]; then
    echo "cannot read log: $log" >&2
    exit 2
  fi
  if ! grep -q "AMReX .* finalized" "$log"; then
    echo "run did not finalize cleanly: $log" >&2
    exit 1
  fi
  if grep -Eq "Abort|Assertion|Backtrace|SIG(SEGV|ABRT)|(^|[^A-Za-z])NaN([^A-Za-z]|$)" "$log"; then
    echo "failure marker found in: $log" >&2
    exit 1
  fi
done

if ! grep -q "full_ddf_arrays=1" "$osi_log"; then
  echo "OSI run did not preserve the single-full-DDF allocation contract" >&2
  exit 1
fi

for expected in \
  "generation=1 finest_level=1" \
  "generation=2 finest_level=0" \
  "generation=3 finest_level=1"; do
  if ! grep -q "regrid_observe: ${expected}" "$osi_log"; then
    echo "OSI log did not exercise required regrid transition: $expected" >&2
    exit 1
  fi
  if ! grep -q "regrid_observe: ${expected}" "$ab_log"; then
    echo "A-B log did not exercise required regrid transition: $expected" >&2
    exit 1
  fi
done

for callback in "remake lev=1" "clear lev=1" "make_new_from_coarse lev=1"; do
  if ! grep -q "regrid_callback: ${callback}" "$osi_log"; then
    echo "OSI log did not execute required callback: $callback" >&2
    exit 1
  fi
  if ! grep -q "regrid_callback: ${callback}" "$ab_log"; then
    echo "A-B log did not execute required callback: $callback" >&2
    exit 1
  fi
done

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/box3d-osi-stage6-check.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

extract_active_checksums() {
  sed -n \
    's/^.*ddf_checksum: \(step=[^ ]* lev=[^ ]*\) valid_sum=[^ ]* \(active_sum=.*\)$/\1 \2/p' \
    "$1"
}

extract_active_checksums "$osi_log" > "$tmp_dir/osi.active"
extract_active_checksums "$ab_log" > "$tmp_dir/ab.active"

osi_records=$(wc -l < "$tmp_dir/osi.active")
ab_records=$(wc -l < "$tmp_dir/ab.active")
if [[ "$osi_records" -eq 0 || "$osi_records" -ne "$ab_records" ]]; then
  echo "checksum record count mismatch: OSI=$osi_records A-B=$ab_records" >&2
  exit 1
fi
if [[ $(grep -c 'active_q26=' "$tmp_dir/osi.active") -ne "$osi_records" ]]; then
  echo "OSI checksum records do not all contain active_q0...active_q26" >&2
  exit 1
fi

if ! diff -u "$tmp_dir/ab.active" "$tmp_dir/osi.active"; then
  echo "dynamic regrid active DDF checksums differ" >&2
  exit 1
fi

echo "OSI stage-6 dynamic regrid check passed: $osi_records step/level records match"
