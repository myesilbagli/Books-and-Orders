#!/usr/bin/env bash
# The baseline (naive, 0 µs) on every converted day of one stock, then one line per day (6d-1).
#
#   research/run_days.sh AAPL 2026 [out_dir]   # date prefix: 2026, 202607, 20260204
#
# Reads data/itch/<YYYYMMDD>.<SYM>.dbn.ITCH50 (research/convert_days.sh writes them).
# Writes <out_dir>/<SYM>/<YYYYMMDD>/naive_0us.{txt,csv}: mm_sim's report and the fills, with
# every chart reading at each fill, for the signal study. Default out_dir: data/sim/days.

set -euo pipefail  # stop on the first failing command, unset variable or broken pipe

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 <symbol> <date prefix> [out_dir]" >&2
  exit 2
fi
symbol="$1"
prefix="$2"
out="${3:-data/sim/days}"
sim=./build/release/cpp/mm_sim
python=research/.venv/bin/python

cmake --build --preset release --target mm_sim >/dev/null  # never a stale binary

pids=()
reports=()
for itch in data/itch/"${prefix}"*."${symbol}".dbn.ITCH50; do
  day=$(basename "$itch" | cut -d. -f1)  # 20260204.AAPL.dbn.ITCH50 -> 20260204
  dir="$out/$symbol/$day"
  mkdir -p "$dir"
  "$sim" "$itch" "$symbol" 0 "$dir/naive_0us.csv" >"$dir/naive_0us.txt" &
  pids+=($!)
  reports+=("$dir/naive_0us.txt")
done
# `wait` on each pid, not a bare `wait`, so a failed run fails the script.
for pid in "${pids[@]}"; do
  wait "$pid"
done

"$python" research/day_summary.py "${reports[@]}" | tee "$out/$symbol/summary_naive_0us.txt"
