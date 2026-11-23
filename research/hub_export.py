"""Turn the simulator's CSVs into the JSON the hub draws (D46).

    research/.venv/bin/python research/hub_export.py 2019-12-30 AAPL data/sim/chart/AAPL_chart.csv \\
        naive_0us=data/sim/m5/naive_0us.csv as10_0us=data/sim/m5/as10_0us.csv

Writes data/hub/<date>_<symbol>/chart_1s.json, chart_1m.json, fills_<run>.json and
updates data/hub/index.json. Computes nothing new: the bars and indicators are the C++
`MarketState`'s (D44), and the markouts are bootstrap.py's formula, only in cents a share.
Standard library only.
"""

import argparse
import calendar
import csv
import json
import os
from datetime import date

import bootstrap as bs

CHART_FIELDS = ["open", "high", "low", "close", "vwap", "ema_fast", "ema_slow", "sma",
                "bb_upper", "bb_lower", "bb_pct", "rsi", "stoch_k", "stoch_d"]
HORIZONS = (1, 10, 60)


def day_start(day):
    """Midnight of `day` (YYYY-MM-DD) in UTC seconds.

    The chart library shows UTC. Writing Eastern wall-clock times as if they were UTC
    makes its axis read 09:30 at 09:30 ET, with no time zone handling in the page. ITCH
    time is already Eastern, so nothing here converts zones either.
    """
    return calendar.timegm(date.fromisoformat(day).timetuple())


def chart_time(midnight, hhmmss):
    h, m, s = (int(x) for x in hhmmss.split(":"))
    return midnight + h * 3600 + m * 60 + s


def _number(text):
    return float(text) if text != "" else None  # empty = warming up: null, never 0


def read_chart(path, midnight):
    """The chart CSV (D41) as {"1s": [bar, ...], "1m": [...]}, oldest first."""
    chart = {"1s": [], "1m": []}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            bar = {"time": chart_time(midnight, r["time_et"]), "volume": int(r["volume"])}
            bar.update({k: _number(r[k]) for k in CHART_FIELDS})
            chart[r["timeframe"]].append(bar)
    return chart


def read_fills(path, midnight):
    """Our fills (D37, D45) with markouts in cents a share at 1, 10 and 60 s."""
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            fill = bs.Fill(timestamp_ns=int(r["timestamp_ns"]), side=r["side"], price=int(r["price"]),
                           quantity=int(r["quantity"]), mid_x2_before=int(r["mid_x2_before"]),
                           mid_x2_1s=int(r["mid_x2_1s"]), mid_x2_10s=int(r["mid_x2_10s"]),
                           mid_x2_60s=int(r["mid_x2_60s"]))
            row = {"time": midnight + fill.timestamp_ns / 1e9, "side": fill.side,
                   "price": fill.price / 10_000, "quantity": fill.quantity,
                   "touch_share": float(r["touch_share"]), "shares_left": int(r["shares_left"])}
            for h in HORIZONS:
                # Same number the bootstrap uses (spread + adverse selection), per share, in
                # cents: 1 cent = 100 ticks.
                row[f"markout_{h}s"] = bs.fill_pnl_ticks(fill, h) / fill.quantity / 100
            rows.append(row)
    return rows


def _write_json(path, value):
    with open(path, "w") as f:
        json.dump(value, f, separators=(",", ":"))  # compact: the 1 s chart is 25 K bars


def export(day, symbol, chart_csv, runs, out_dir):
    """runs: {name: fills_csv}. Adds to what's already in out_dir."""
    midnight = day_start(day)
    day_id = f"{day}_{symbol}"
    folder = os.path.join(out_dir, day_id)
    os.makedirs(folder, exist_ok=True)

    chart = read_chart(chart_csv, midnight)
    _write_json(os.path.join(folder, "chart_1s.json"), chart["1s"])
    _write_json(os.path.join(folder, "chart_1m.json"), chart["1m"])
    for name, path in runs.items():
        _write_json(os.path.join(folder, f"fills_{name}.json"), read_fills(path, midnight))

    index_path = os.path.join(out_dir, "index.json")
    index = {"days": []}
    if os.path.exists(index_path):
        with open(index_path) as f:
            index = json.load(f)
    days = {d["id"]: d for d in index["days"]}
    entry = days.setdefault(day_id, {"id": day_id, "date": day, "symbol": symbol, "runs": []})
    entry["runs"] = sorted(set(entry["runs"]) | set(runs))
    index["days"] = sorted(days.values(), key=lambda d: d["id"])
    _write_json(index_path, index)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("day", help="YYYY-MM-DD")
    p.add_argument("symbol")
    p.add_argument("chart_csv", help="chart_dump's output")
    p.add_argument("runs", nargs="+", help="name=fills.csv, one per mm_sim run")
    p.add_argument("--out", default="data/hub")
    a = p.parse_args()
    runs = dict(r.split("=", 1) for r in a.runs)
    export(a.day, a.symbol, a.chart_csv, runs, a.out)
    print(f"wrote {a.out}/{a.day}_{a.symbol}: chart + {len(runs)} run(s)")


if __name__ == "__main__":
    main()
