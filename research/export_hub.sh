#!/usr/bin/env bash
# Everything the hub draws for one day (D46, D50, D58): the charts with their structure for
# each swing size c, and every mm_sim run's fills in one folder.
#
#   research/export_hub.sh data/itch/12302019.AAPL.ITCH50 2019-12-30 AAPL               # milestone 5
#   research/export_hub.sh data/itch/20260204.AAPL.dbn.ITCH50 2026-02-04 AAPL data/sim/days/AAPL/20260204
#
# runs_dir (default data/sim/m5, from research/run_m5.sh) holds one <run>.csv per mm_sim run.
# CS="1.5 2" picks the swing sizes (default 1 1.5 2 3). Writes data/sim/chart/<SYMBOL>/<day>/
# and data/hub/. Takes a few seconds on a one-stock slice (D43).
set -euo pipefail

itch=$1
day=$2
symbol=$3
runs_dir=${4:-data/sim/m5}
read -r -a cs <<< "${CS:-1 1.5 2 3}"  # the swing sizes the founder picks from by eye (D48)

cmake --build --preset release > /dev/null
out=data/sim/chart/$symbol/$day  # one folder per day, so days never overwrite each other
pids=()
for c in "${cs[@]}"; do
  mkdir -p "$out/c$c"
  ./build/release/cpp/chart_dump "$itch" "$symbol" "$out/c$c/chart.csv" --c "$c" \
    --swings "$out/c$c/swings.csv" 2> /dev/null &
  pids+=($!)
done
# And once with the founder's swing sizes (no --c), exactly what mm_sim's strategies see:
# the candles, indicators and turn reads (D63; the reads use the 1 min swings) come from it.
./build/release/cpp/chart_dump "$itch" "$symbol" "$out/chart.csv" 2> /dev/null &
pids+=($!)
for pid in "${pids[@]}"; do
  wait "$pid"  # one at a time, so a failed dump stops the script (set -e)
done

runs=()
for f in "$runs_dir"/*.csv; do
  runs+=("$(basename "$f" .csv)=$f")
done
structures=()
for c in "${cs[@]}"; do
  structures+=(--structure "$c=$out/c$c/chart.csv,$out/c$c/swings.csv")
done
research/.venv/bin/python research/hub_export.py "$day" "$symbol" "$out/chart.csv" \
  "${runs[@]}" "${structures[@]}"
