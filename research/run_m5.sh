#!/usr/bin/env bash
# Reproduce every milestone 5 number in docs/RESULTS.md from one ITCH file.
#
#   research/run_m5.sh data/itch/12302019.NASDAQ_ITCH50 [out_dir]
#
# Writes, per run, <name>.txt (mm_sim's report) and <name>.csv (the fills), then the
# bootstrap tables. Default out_dir is data/sim/m5 (git-ignored, about 3 MB).
# Takes a few minutes: the seven runs go in parallel, each replays the whole day.

set -euo pipefail  # stop on the first failing command, unset variable or broken pipe

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <itch_file> [out_dir]" >&2
  exit 2
fi
itch="$1"
out="${2:-data/sim/m5}"
symbol=AAPL
sim=./build/release/cpp/mm_sim
python=research/.venv/bin/python

# Always the code as it is now: a stale binary would give old numbers silently.
cmake --build --preset release --target mm_sim >/dev/null
mkdir -p "$out"

# name | latency in µs | strategy flags. The first is the baseline for the comparisons.
runs=(
  "naive_0us|0|"
  "as05_0us|0|--as 0.5"
  "as10_0us|0|--as 1.0"
  "pull20_0us|0|--pull 0.2"
  "pull30_0us|0|--pull 0.3"
  "naive_50us|50|"
  "pull20_50us|50|--pull 0.2"
)

pids=()
for run in "${runs[@]}"; do
  IFS='|' read -r name latency flags <<<"$run"
  # $flags unquoted on purpose: "--as 0.5" must split into two arguments.
  # shellcheck disable=SC2086
  "$sim" "$itch" "$symbol" "$latency" "$out/$name.csv" $flags >"$out/$name.txt" &
  pids+=($!)
done
# `wait` on each pid, not a bare `wait`, so a failed run fails the script.
for pid in "${pids[@]}"; do
  wait "$pid"
done

csvs=()
for run in "${runs[@]}"; do
  csvs+=("$out/${run%%|*}.csv")  # the name: everything before the first |
done

# The main table (10 s, 5-minute blocks) and the robustness checks behind D37.
"$python" research/bootstrap.py "${csvs[@]}" --horizon 10 >"$out/bootstrap_10s.txt"
"$python" research/bootstrap.py "${csvs[@]}" --horizon 1 >"$out/bootstrap_1s.txt"
"$python" research/bootstrap.py "${csvs[@]}" --horizon 10 --block-min 1 >"$out/bootstrap_10s_1min.txt"
"$python" research/bootstrap.py "${csvs[@]}" --horizon 10 --block-min 15 >"$out/bootstrap_10s_15min.txt"

cat "$out/bootstrap_10s.txt"
echo "all reports in $out/"
