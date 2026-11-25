"""After a read fires, how far does the price go its way, and does that beat a taker's cost?
(round 7e, D64)

    research/.venv/bin/python research/event_study.py

For each B1 / B2 read (D63), from the end of the 15 s bar that completed it (when it was
known): the mid's move in the read's direction, in cents, 10 s to 5 min later. Compared
with a taker's round trip: half the spread in and half out (the spread read off the naive
fills, which rest at the touch) plus the take fee each way. Baselines: every 15 s bar's own
direction (momentum) and its opposite (fade). Mean over events with a 95 % interval from
5 min blocks resampled within each day, whole day and per session of the founder's map.
Descriptive: no primary test. Standard library only.
"""

import argparse
import bisect
import csv
import glob
import os
import random
import statistics
from dataclasses import dataclass, field

HORIZONS_S = (10, 30, 60, 120, 300)
FEE_CENTS = 0.3   # Nasdaq's take fee, about $0.0030 a share, each way
BLOCK_S = 300     # 5 min blocks, as in the signal study (D56)
READ_COLUMNS = {"b1": "B1 trap", "b2": "B2 pullback end"}
SESSIONS_S = [("open", 34_200, 37_800), ("late morning", 37_800, 43_200),
              ("lunch", 43_200, 50_400), ("power", 50_400, 57_600)]  # as turn_reads


def _sec(hhmmss):
    h, m, s = (int(x) for x in hhmmss.split(":"))
    return h * 3600 + m * 60 + s


@dataclass
class Day:
    mids: dict = field(default_factory=dict)    # second -> the mid just before it, dollars
    events: dict = field(default_factory=dict)  # name -> [(second known, +1 / -1)]

    def mid_at(self, t):
        return self.mids.get(t)


def read_day(chart_csv):
    """The 1 s mids and the events of one day's chart CSV (chart_dump, D41, D63)."""
    day = Day(events={name: [] for name in list(READ_COLUMNS.values()) + ["15s momentum"]})
    with open(chart_csv, newline="") as f:
        for r in csv.DictReader(f):
            start = _sec(r["time_et"])
            if r["timeframe"] == "1s":
                day.mids[start + 1] = float(r["close"])  # in force just before the bar ends
            elif r["timeframe"] == "15s":
                known = start + 15
                for column, name in READ_COLUMNS.items():
                    v = r.get(column) or ""
                    if v not in ("", "0"):
                        day.events[name].append((known, int(v)))
                move = float(r["close"]) - float(r["open"])
                if move:
                    day.events["15s momentum"].append((known, 1 if move > 0 else -1))
    return day


def signed_moves(day, events, horizons=HORIZONS_S):
    """Per event: {horizon: cents the mid moved the event's way, None past the data}."""
    out = []
    for t, direction in events:
        start = day.mid_at(t)
        moves = {}
        for h in horizons:
            later = day.mid_at(t + h)
            moves[h] = None if start is None or later is None else \
                round(direction * (later - start) * 100, 6)
        out.append(moves)
    return out


def median_half_spread_cents(fills_csvs):
    """The naive fills rest at the touch, so |price - mid| at a fill is half the spread."""
    halves = []
    for path in fills_csvs:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                halves.append(abs(int(r["price"]) - int(r["mid_x2_before"]) / 2) / 100)
    return statistics.median(halves)


def round_trip_cents(half_spread, fee_cents=FEE_CENTS):
    return 2 * half_spread + 2 * fee_cents


def block_mean_ci(days, resamples=2000, seed=0, level=0.95, block_s=BLOCK_S):
    """days: [[(second, value), ...], ...]. The mean of all values and a percentile interval
    from resampling each day's 5 min blocks with replacement (events in one block move
    together; days stay apart). (None, None, None) without values."""
    blocked = []
    for values in days:
        blocks = {}
        for t, v in values:
            blocks.setdefault(t // block_s, []).append(v)
        if blocks:
            blocked.append([(sum(b), len(b)) for b in blocks.values()])
    n = sum(c for day in blocked for _, c in day)
    if not n:
        return (None, None, None)
    mean = sum(s for day in blocked for s, _ in day) / n
    rng = random.Random(seed)
    means = []
    for _ in range(resamples):
        total = count = 0
        for day in blocked:
            for _ in day:
                s, c = day[rng.randrange(len(day))]
                total += s
                count += c
        means.append(total / count)
    means.sort()
    lo = means[int((1 - level) / 2 * resamples)]
    hi = means[min(resamples - 1, int((1 + level) / 2 * resamples))]
    return (round(mean, 6), round(lo, 6), round(hi, 6))


def _table(days, names, horizons, cost, start_s, end_s, resamples, seed):
    rows = []
    for name in names:
        per_h = {}
        for h in horizons:
            values = []
            for day in days:
                evs = [(t, d) for t, d in day.events[name] if start_s <= t < end_s]
                moves = signed_moves(day, evs, (h,))
                values.append([(t, m[h]) for (t, _), m in zip(evs, moves) if m[h] is not None])
            mean, lo, hi = block_mean_ci(values, resamples, seed)
            flat = [v for day in values for _, v in day]
            beats = sum(v > cost for v in flat) / len(flat) if flat else None
            per_h[h] = (len(flat), mean, lo, hi, beats)
        rows.append((name, per_h))
    return rows


def _print(rows, horizons, label):
    for name, per_h in rows:
        n = per_h[horizons[0]][0]
        print(f"  {name:<18}{label:<14}{n:>6}", end="")
        for h in horizons:
            _, mean, lo, hi, beats = per_h[h]
            cell = "-" if mean is None else f"{mean:+.2f} [{lo:+.2f},{hi:+.2f}] {beats:.0%}"
            print(f"  {cell:>24}", end="")
        print()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--charts", default="data/sim/chart/AAPL", help="chart_dump runs, one folder a day")
    p.add_argument("--days", default="data/sim/days/AAPL", help="naive fills, for the spread")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    charts = sorted(glob.glob(os.path.join(a.charts, "2026-*", "chart.csv")))
    days = [read_day(c) for c in charts]
    for day in days:  # the fade baseline: the opposite of momentum
        day.events["15s fade"] = [(t, -d) for t, d in day.events["15s momentum"]]
    fills = sorted(glob.glob(os.path.join(a.days, "2026*", "naive_0us.csv")))
    half = median_half_spread_cents(fills)
    cost = round_trip_cents(half)
    print(f"{len(days)} days; median half spread {half:.2f} c, taker round trip {cost:.2f} c "
          f"(spread + {FEE_CENTS} c fee each way)")
    print("mean cents the read's way [95 %] and the share of events beating the round trip\n")
    names = ["B1 trap", "B2 pullback end", "15s momentum", "15s fade"]
    head = "".join(f"{f'{h} s':>26}" for h in HORIZONS_S)
    print(f"  {'read':<18}{'':<14}{'events':>6}{head}")
    _print(_table(days, names, HORIZONS_S, cost, 0, 86_400, a.resamples, a.seed),
           HORIZONS_S, "whole day")
    print("\nby session")
    for session, start, end in SESSIONS_S:
        _print(_table(days, names[:2], HORIZONS_S, cost, start, end, a.resamples, a.seed),
               HORIZONS_S, session)


if __name__ == "__main__":
    main()
