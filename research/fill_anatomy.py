"""Where the market maker's loss comes from: the queue and the trend (round 7a, D59).

    research/.venv/bin/python research/fill_anatomy.py data/sim/days/AAPL/*/naive_0us.csv

Each fills CSV sits in data/sim/days/<SYM>/<YYYYMMDD>/; its 1 min swings are read from
data/sim/chart/<SYM>/<YYYY-MM-DD>/c1.5/swings.csv (research/export_days.sh writes them).

Five splits of the same fills, each "flagged vs rest" with signal_study's machinery
(difference within each day, stratified block bootstrap, D56):
  level broke      the level broke as we filled (nothing left at our price): the queue
  oracle leg       against the ZigZag leg drawn with hindsight: NOT tradable, an upper bound
  confirmed leg    against the leg since the last confirmed swing: the same swings, real time
  1m structure     against a +-2 structure score (round 2's A)
  strong trend     against a +-2 score with ER 1m >= 0.6
and a cross table, level broke x oracle leg. Standard library only.
"""

import argparse
import bisect
import csv
import os
from dataclasses import dataclass

import bootstrap as bs
import signal_study as ss

SECOND_NS = 1_000_000_000
BAR_NS = 60 * SECOND_NS  # the swings are 1 min swings
STRONG_ER = 0.6          # the hub's opening threshold (D58); not yet picked by eye


@dataclass(frozen=True)
class Swing:
    kind: str          # "H" or "L"
    made_ns: int       # start of the bar that made the high or low, ns since midnight ET
    confirmed_ns: int  # when it was known (the end of the confirming bar)


def _ns(hhmmss):
    h, m, s = (int(x) for x in hhmmss.split(":"))
    return (h * 3600 + m * 60 + s) * SECOND_NS


def read_swings(path, timeframe="1m"):
    with open(path, newline="") as f:
        return [Swing(r["kind"], _ns(r["time_et"]), _ns(r["confirmed_et"]))
                for r in csv.DictReader(f) if r["timeframe"] == timeframe]


def _direction(kind):
    return 1 if kind == "L" else -1  # after a low the price goes up to the next high


def oracle_leg(swings, t_ns, bar_ns=BAR_NS):
    """+1 on an up leg, -1 on a down leg, as a chart drawn after the close shows it.
    None before the first swing, after the last (its leg never finished), and inside a
    turning bar (the fill may have come before or after the extreme)."""
    i = bisect.bisect_right([s.made_ns for s in swings], t_ns) - 1  # last swing made by t
    if i < 0 or i + 1 >= len(swings) or t_ns < swings[i].made_ns + bar_ns:
        return None
    return _direction(swings[i].kind)


def delayed_leg(swings, t_ns, lag_ns, bar_ns=BAR_NS):
    """The hindsight leg, learned `lag_ns` after its turning bar ends (7c). Until then the
    trader still believes the leg before it. lag 0 is oracle_leg. The same fills are left
    out as there, plus the first leg's until its lag has passed (nothing was believed)."""
    leg = oracle_leg(swings, t_ns, bar_ns)
    if leg is None:
        return None
    i = bisect.bisect_right([s.made_ns for s in swings], t_ns) - 1
    if t_ns >= swings[i].made_ns + bar_ns + lag_ns:
        return leg
    return -leg if i > 0 else None  # legs alternate: the one before ran the other way


def confirmed_leg(swings, t_ns):
    """The leg as known at t: from the last swing confirmed by then. No look-ahead."""
    known = [s for s in swings if s.confirmed_ns <= t_ns]
    if not known:
        return None
    return _direction(max(known, key=lambda s: s.made_ns).kind)


def _leg_reading(column):
    def reading(ind, mid):
        return ind[column]  # -1 / +1 / None: against it is flagged
    return reading


def _strong_trend(ind, mid):
    score, er = ind["structure_1m"], ind["er_1m"]
    if score is None or er is None:
        return None
    return int(score / 2) if abs(score) == 2 and er >= STRONG_ER else 0


SIGNALS = [
    ss.Signal("level broke", "nothing left at our price as we filled",
              group=lambda ind: ind["level_broke"]),
    ss.Signal("oracle leg", "against the hindsight ZigZag leg (1 min, c = 1.5)",
              _leg_reading("oracle_leg")),
    ss.Signal("confirmed leg", "against the leg since the last confirmed swing",
              _leg_reading("confirmed_leg")),
    ss.Signal("1m structure", "against a 1 min +-2 score", ss.structure_reading("1m")),
    ss.Signal("strong trend", f"against a 1 min +-2 score with ER >= {STRONG_ER}", _strong_trend),
]


def read_day(fills_csv, swings):
    """signal_study's rows, with the queue and the two legs added to each fill's readings."""
    rows = ss.read_rows(fills_csv)
    with open(fills_csv, newline="") as f:
        left = [int(r["shares_left"]) for r in csv.DictReader(f)]
    out = []
    for r, shares_left in zip(rows, left):
        t = r.fill.timestamp_ns
        ind = dict(r.indicators, level_broke=shares_left == 0,
                   oracle_leg=oracle_leg(swings, t), confirmed_leg=confirmed_leg(swings, t))
        out.append(ss.Row(fill=r.fill, indicators=ind))
    return out


def cross_table(days, horizon=10):
    """(shares, cents a share) pooled over the days, for level broke/held x against/with the
    oracle leg. Descriptive: where the shares and the loss sit."""
    sums = {(q, t): [0, 0] for q in ("broke", "held") for t in ("against", "with")}
    for rows in days:
        for r in rows:
            leg = r.indicators["oracle_leg"]
            if leg is None:
                continue
            cell = sums[("broke" if r.indicators["level_broke"] else "held",
                         "against" if ss.flagged(r.fill.side, leg) else "with")]
            cell[0] += r.fill.quantity
            cell[1] += bs.fill_pnl_ticks(r.fill, horizon)
    return {k: (sh, round(pnl / sh / 100, 6) if sh else None) for k, (sh, pnl) in sums.items()}


def _swings_for(fills_csv, chart_root):
    day = os.path.basename(os.path.dirname(os.path.abspath(fills_csv)))  # 20260204
    iso = f"{day[:4]}-{day[4:6]}-{day[6:]}"
    return read_swings(os.path.join(chart_root, iso, "c1.5", "swings.csv"))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("csv", nargs="+", help="fills CSVs, one per day, in data/sim/days/<SYM>/<day>/")
    p.add_argument("--charts", default="data/sim/chart/AAPL", help="chart_dump output per day")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    days = [read_day(path, _swings_for(path, a.charts)) for path in sorted(a.csv)]
    ss.report(f"{len(days)} days", ss.study_days(days, a.resamples, a.seed, signals=SIGNALS),
              a.resamples, SIGNALS, "7a", len(days))
    for h in ss.HORIZONS:
        table = cross_table(days, h)
        total = sum(sh for sh, _ in table.values())
        print(f"\nlevel x oracle leg, {h} s: share of the shares with a leg, cents a share")
        for k, (sh, c) in table.items():
            print(f"  {k[0]:<6} {k[1]:<8} {sh / total:>6.1%} {ss._f(c):>7}")


if __name__ == "__main__":
    main()
