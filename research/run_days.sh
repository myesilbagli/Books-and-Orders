#!/usr/bin/env bash
# One mm_sim run on every converted day of one stock, then one line per day (6d-1, D60).
#
#   research/run_days.sh AAPL 2026                                    # the baseline: naive, 0 µs
#   research/run_days.sh AAPL 2026 data/sim/days ladder2_0us --levels 2   # [out_dir] [run name] [mm_sim options]
#   LATENCY_US=50 research/run_days.sh AAPL 2026 data/sim/days naive_50us   # latency in µs, default 0
#
# Date prefix: 2026, 202607, 20260204.
#
# Reads data/itch/<YYYYMMDD>.<SYM>.dbn.ITCH50 (research/convert_days.sh writes them).
# Writes <out_dir>/<SYM>/<YYYYMMDD>/<run>.{txt,csv}: mm_sim's report and the fills, with
# every chart reading at each fill, for the signal study. Default out_dir: data/sim/days.

set -euo pipefail  # stop on the first failing command, unset variable or broken pipe

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <symbol> <date prefix> [out_dir] [run name] [mm_sim options...]" >&2
  exit 2
fi
symbol="$1"
prefix="$2"
out="${3:-data/sim/days}"
run="${4:-naive_0us}"
options=("${@:5}")  # everything after the run name goes to mm_sim as is
latency_us="${LATENCY_US:-0}"
sim=./build/release/cpp/mm_sim
python=research/.venv/bin/python

cmake --build --preset release --target mm_sim >/dev/null  # never a stale binary

pids=()
reports=()
for itch in data/itch/"${prefix}"*."${symbol}".dbn.ITCH50; do
  day=$(basename "$itch" | cut -d. -f1)  # 20260204.AAPL.dbn.ITCH50 -> 20260204
  dir="$out/$symbol/$day"
  mkdir -p "$dir"
  # ${options[@]+...}: expands to nothing when there are no options (set -u would object).
  "$sim" "$itch" "$symbol" "$latency_us" "$dir/$run.csv" ${options[@]+"${options[@]}"} >"$dir/$run.txt" &
  pids+=($!)
  reports+=("$dir/$run.txt")
done
# `wait` on each pid, not a bare `wait`, so a failed run fails the script.
for pid in "${pids[@]}"; do
  wait "$pid"
done

"$python" research/day_summary.py "${reports[@]}" | tee "$out/$symbol/summary_$run.txt"
