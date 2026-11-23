#!/usr/bin/env bash
# Every Databento MBO day of one stock -> ITCH 5.0, each checked by a full replay (D54).
#
#   research/convert_days.sh AAPL 2026   # [date prefix: 2026, 202607, 20260204; default every day]
#
# Reads data/databento/<SYM>_<YYYYMMDD>_XNAS_mbo.dbn.zst, writes data/itch/<YYYYMMDD>.<SYM>.dbn.ITCH50
# (git-ignored), skipping days already converted. About 20 s a day, 3 GB of memory at most.
# A day passes when the replay ends clean with 0 decode failures, 0 unknown orders,
# 0 rejected, 0 crossed while trading, and 0 orders left resting. The last needs the whole
# day: the 30 Dec 2019 file was bought only to 19:00 ET (D52), so it ends with orders resting.

set -euo pipefail  # stop on the first failing command, unset variable or broken pipe

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <symbol> [date prefix]" >&2
  exit 2
fi
symbol="$1"
prefix="${2:-}"
python=research/.venv/bin/python
replay=./build/release/cpp/itch_replay

cmake --build --preset release --target itch_replay >/dev/null  # never a stale binary
mkdir -p data/itch

failed=0
for dbn in data/databento/"${symbol}"_"${prefix}"*_XNAS_mbo.dbn.zst; do
  day=$(basename "$dbn" | cut -d_ -f2)  # AAPL_20260204_XNAS_mbo.dbn.zst -> 20260204
  itch="data/itch/${day}.${symbol}.dbn.ITCH50"
  if [[ ! -f "$itch" ]]; then
    # Write to a temporary name first, so an interrupted run never leaves half a day behind.
    "$python" research/dbn_to_itch.py "$dbn" "$symbol" "$itch.part" 2>"$itch.log"
    mv "$itch.part" "$itch"
  fi
  report=$("$replay" "$itch" "$symbol")
  # Each check is one line of the replay report; all must read as below.
  if grep -q "End (clean)" <<<"$report" \
      && grep -Eq "^decode failures: +0$" <<<"$report" \
      && grep -Eq "^unknown orders: +0$" <<<"$report" \
      && grep -Eq "^rejected: +0$" <<<"$report" \
      && grep -Eq "^crossed, trading: +0$" <<<"$report" \
      && grep -Eq "^resting orders: +0 " <<<"$report"; then
    echo "$day ok   $(grep -E '^messages:' <<<"$report" | tr -s ' ')"
  else
    echo "$day FAILED"; echo "$report"; failed=1
  fi
done
exit "$failed"
