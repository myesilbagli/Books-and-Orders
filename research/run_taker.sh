#!/usr/bin/env bash
# taker_sim on every day of one stock (round 7f, D65), one run name, in parallel:
#
#   research/run_taker.sh AAPL 2026 b1_open                       # B1 09:30-10:30, 100 ms, 2 min
#   research/run_taker.sh AAPL 2026 b1_open_1ms --latency-us 1000
#
# Writes data/sim/days/<SYMBOL>/<day>/taker_<run>.csv; then research/taker_summary.py <run>.
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 <symbol> <date prefix> <run name> [taker_sim options...]" >&2
  exit 2
fi
symbol="$1"
prefix="$2"
run="$3"
options=("${@:4}")

cmake --build --preset release --target taker_sim >/dev/null  # never a stale binary
pids=()
for itch in data/itch/"${prefix}"*."${symbol}".dbn.ITCH50; do
  day=$(basename "$itch" | cut -d. -f1)
  dir="data/sim/days/$symbol/$day"
  mkdir -p "$dir"
  ./build/release/cpp/taker_sim "$itch" "$symbol" "$dir/taker_$run.csv" \
    ${options[@]+"${options[@]}"} 2>/dev/null &
  pids+=($!)
done
for pid in "${pids[@]}"; do
  wait "$pid"  # each one, so a failed day fails the script
done
