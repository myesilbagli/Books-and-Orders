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
                "bb_upper", "bb_lower", "bb_pct", "rsi", "stoch_k", "stoch_d", "atr"]
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
    """The chart CSV (D41, D49) as {timeframe: [bar, ...]}, oldest first, for every
    timeframe in the file (1s, 15s, 1m, 5m)."""
    chart = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            bar = {"time": chart_time(midnight, r["time_et"]), "volume": int(r["volume"])}
            bar.update({k: _number(r[k]) for k in CHART_FIELDS})
            bar["structure"] = int(r["structure"]) if r["structure"] != "" else None
            chart.setdefault(r["timeframe"], []).append(bar)
    return chart


LABELS = {("H", 1): "HH", ("H", -1): "LH", ("H", 0): "EH", ("H", None): "H",
          ("L", 1): "HL", ("L", -1): "LL", ("L", 0): "EL", ("L", None): "L"}


def read_swings(path, midnight):
    """chart_dump --swings (D49) as {timeframe: [swing, ...]}, each with its chart label
    (HH, HL, LH, LL; H or L for the first of its kind; EH, EL for an equal one)."""
    swings = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            change = int(r["change"]) if r["change"] != "" else None
            swings.setdefault(r["timeframe"], []).append({
                "time": chart_time(midnight, r["time_et"]),          # the bar that made it
                "confirmed": chart_time(midnight, r["confirmed_et"]),  # when it was known
                "price": float(r["price"]), "kind": r["kind"],
                "label": LABELS[(r["kind"], change)]})
    return swings


def score_changes(bars):
    """The bars where the structure score changed, oldest first; the hub carries each one
    forward. Small: a few hundred points instead of 25 K bars per c."""
    changes, last = [], None
    for b in bars:
        if b["structure"] is not None and b["structure"] != last:
            changes.append({"time": b["time"], "score": b["structure"]})
            last = b["structure"]
    return changes


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


def export(day, symbol, chart_csv, runs, out_dir, structures=None):
    """runs: {name: fills_csv}. structures: {c: (chart_csv, swings_csv)}, one chart_dump
    run per swing size c (D48). Adds to what's already in out_dir."""
    structures = structures or {}
    midnight = day_start(day)
    day_id = f"{day}_{symbol}"
    folder = os.path.join(out_dir, day_id)
    os.makedirs(folder, exist_ok=True)

    for timeframe, bars in read_chart(chart_csv, midnight).items():
        _write_json(os.path.join(folder, f"chart_{timeframe}.json"), bars)
    for name, path in runs.items():
        _write_json(os.path.join(folder, f"fills_{name}.json"), read_fills(path, midnight))
    for c, (c_chart, c_swings) in structures.items():
        swings = read_swings(c_swings, midnight)
        for timeframe, bars in read_chart(c_chart, midnight).items():
            _write_json(os.path.join(folder, f"structure_{timeframe}_c{c}.json"),
                        {"swings": swings.get(timeframe, []), "scores": score_changes(bars)})

    index_path = os.path.join(out_dir, "index.json")
    index = {"days": []}
    if os.path.exists(index_path):
        with open(index_path) as f:
            index = json.load(f)
    days = {d["id"]: d for d in index["days"]}
    entry = days.setdefault(day_id, {"id": day_id, "date": day, "symbol": symbol, "runs": []})
    entry["runs"] = sorted(set(entry["runs"]) | set(runs))
    entry["structure_c"] = sorted(set(entry.get("structure_c", [])) | set(structures), key=float)
    index["days"] = sorted(days.values(), key=lambda d: d["id"])
    _write_json(index_path, index)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("day", help="YYYY-MM-DD")
    p.add_argument("symbol")
    p.add_argument("chart_csv", help="chart_dump's output")
    p.add_argument("runs", nargs="+", help="name=fills.csv, one per mm_sim run")
    p.add_argument("--structure", action="append", default=[],
                   help="c=chart.csv,swings.csv from chart_dump --c c --swings; repeatable")
    p.add_argument("--out", default="data/hub")
    a = p.parse_args()
    runs = dict(r.split("=", 1) for r in a.runs)
    structures = {}
    for item in a.structure:
        c, files = item.split("=", 1)
        structures[c] = tuple(files.split(","))
    export(a.day, a.symbol, a.chart_csv, runs, a.out, structures)
    print(f"wrote {a.out}/{a.day}_{a.symbol}: chart + {len(runs)} run(s)")


if __name__ == "__main__":
    main()
