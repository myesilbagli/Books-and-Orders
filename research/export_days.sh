#!/usr/bin/env bash
# The hub's view of every development day of one stock (D58): charts, structure at
# c = 1.5 and 2 (the defaults, D48), and the baseline's fills.
#
#   research/export_days.sh AAPL 2026   # date prefix: 2026, 202607, 20260204
#
# Needs research/convert_days.sh (the ITCH files) and research/run_days.sh (the fills).
# Two swing sizes, not four, keep data/hub at about 15 MB a day.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <symbol> <date prefix>" >&2
  exit 2
fi
symbol="$1"
prefix="$2"

for itch in data/itch/"${prefix}"*."${symbol}".dbn.ITCH50; do
  day=$(basename "$itch" | cut -d. -f1)            # 20260204.AAPL.dbn.ITCH50 -> 20260204
  iso="${day:0:4}-${day:4:2}-${day:6:2}"           # -> 2026-02-04
  CS="1.5 2" research/export_hub.sh "$itch" "$iso" "$symbol" "data/sim/days/$symbol/$day"
done
