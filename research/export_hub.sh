#!/usr/bin/env bash
# Everything the hub draws for one day (D46, D50): the charts with their structure for
# each swing size c, and every milestone 5 run's fills.
#
#   research/export_hub.sh data/itch/12302019.AAPL.ITCH50 2019-12-30 AAPL
#
# Needs the fills from research/run_m5.sh in data/sim/m5/. Writes data/sim/chart/<SYMBOL>/
# and data/hub/. Takes a few seconds on a one-stock slice (D43).
set -euo pipefail

itch=$1
day=$2
symbol=$3
cs=(1 1.5 2 3)  # the swing sizes the founder picks from by eye (D48)

cmake --build --preset release > /dev/null
out=data/sim/chart/$symbol
pids=()
for c in "${cs[@]}"; do
  mkdir -p "$out/c$c"
  ./build/release/cpp/chart_dump "$itch" "$symbol" "$out/c$c/chart.csv" --c "$c" \
    --swings "$out/c$c/swings.csv" 2> /dev/null &
  pids+=($!)
done
for pid in "${pids[@]}"; do
  wait "$pid"  # one at a time, so a failed dump stops the script (set -e)
done

runs=()
for f in data/sim/m5/*.csv; do
  runs+=("$(basename "$f" .csv)=$f")
done
structures=()
for c in "${cs[@]}"; do
  structures+=(--structure "$c=$out/c$c/chart.csv,$out/c$c/swings.csv")
done
# Candles and indicators don't depend on c: take them from the c = 2 run.
research/.venv/bin/python research/hub_export.py "$day" "$symbol" "$out/c2/chart.csv" \
  "${runs[@]}" "${structures[@]}"
