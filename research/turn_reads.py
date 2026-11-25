"""Which real-time reads see the 1 min trend leg early? Each read on the delayed-oracle
curve (round 7d-1, D62; B1 and B2 from round 7d-2, D63).

    research/.venv/bin/python research/turn_reads.py data/sim/days/AAPL/*/naive_0us.csv

For each read: fills against it vs the rest at 1 s and 10 s (within days, block bootstrap,
D56), its **equivalent lag** (how late a trader knowing the hindsight leg would have to be
to do as well, from round 7c's curve, computed in the same run), its coverage (share of
shares it gives a direction to) and its agreement with the hindsight leg where both have
one. Then the same per session of the founder's map (open, late morning, lunch, power) for
the price-action reads. Descriptive: no primary test. Standard library only.
"""

import argparse

import fill_anatomy as fa
import oracle_curve as oc
import signal_study as ss

CURVE_LAGS_S = (0, 5, 15, 30, 60, 120, 300)


def column_reading(column):
    """A read stored as -1 / 0 / +1 in a fill's readings."""
    def reading(ind, mid):
        return ind[column]
    return reading


def strong_trend(tf, er_min=0.6):
    """+-1 for a +-2 structure with an efficient path (ER >= er_min), 0 otherwise."""
    def reading(ind, mid):
        score, er = ind[f"structure_{tf}"], ind[f"er_{tf}"]
        if score is None or er is None:
            return None
        return int(score / 2) if abs(score) == 2 and er >= er_min else 0
    return reading


READS = [
    ("1m structure", ss.structure_reading("1m")),
    ("15s structure", ss.structure_reading("15s")),
    ("1s structure", ss.structure_reading("1s")),
    ("15s strong trend", strong_trend("15s")),
    ("1s EMA trend", ss._trend("1s")),
    ("1m EMA trend", ss._trend("1m")),
    ("confirmed 1m leg", column_reading("confirmed_leg")),
    ("B1 trap", column_reading("b1")),               # 7d-2: price action at levels (D63)
    ("B2 pullback end", column_reading("b2")),
]
PRICE_ACTION = ("B1 trap", "B2 pullback end")

HOUR_NS = 3600 * fa.SECOND_NS
SESSIONS = [  # the founder's playbook session map: (name, start, end), ns since midnight ET
    ("open", int(9.5 * HOUR_NS), int(10.5 * HOUR_NS)),
    ("late morning", int(10.5 * HOUR_NS), 12 * HOUR_NS),
    ("lunch", 12 * HOUR_NS, 14 * HOUR_NS),
    ("power", 14 * HOUR_NS, 16 * HOUR_NS),
]


def in_session(days, start_ns, end_ns):
    """Each day's fills from start_ns (included) to end_ns (not)."""
    return [[r for r in rows if start_ns <= r.fill.timestamp_ns < end_ns] for rows in days]


def equivalent_lag(diff, curve):
    """Seconds of lag at which the delayed oracle's gap equals `diff`, by straight lines
    between the curve's points, read only up to where the curve first reaches 0. None when
    `diff` is worth nothing (>= 0, or past every point)."""
    points = []
    for lag, d in curve:
        points.append((lag, d))
        if d >= 0:
            break
    if diff >= 0:
        return None
    if diff <= points[0][1]:
        return 0.0
    for (l0, d0), (l1, d1) in zip(points, points[1:]):
        if d0 <= diff <= d1 and d1 > d0:
            return l0 + (l1 - l0) * (diff - d0) / (d1 - d0)
    return None


def coverage_and_agreement(days, reading):
    """(share of shares the read gives a direction, share of those with a hindsight leg
    whose direction is the leg's)."""
    total = directed = compared = agree = 0
    for rows in days:
        for r in rows:
            q = r.fill.quantity
            total += q
            d = reading(r.indicators, r.fill.mid_x2_before / 2)
            if d not in (-1, 1):
                continue
            directed += q
            leg = r.indicators["oracle_leg"]
            if leg is not None:
                compared += q
                agree += q if d == leg else 0
    return (directed / total if total else 0.0, agree / compared if compared else None)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("csv", nargs="+", help="fills CSVs, one per day, in data/sim/days/<SYM>/<day>/")
    p.add_argument("--charts", default="data/sim/chart/AAPL")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    paths = sorted(a.csv)
    all_swings = [fa._swings_for(path, a.charts) for path in paths]
    days = [oc.with_lags(fa.read_day(path, sw), sw, CURVE_LAGS_S) for path, sw in zip(paths, all_swings)]

    curve_signals = oc.signals(CURVE_LAGS_S)
    read_signals = [ss.Signal(name, f"against {name}", reading, primary=False) for name, reading in READS]
    results = ss.study_days(days, a.resamples, a.seed, signals=curve_signals + read_signals)

    def whole(name, h):
        return next(r for r in results if r.signal == name and r.horizon == h and r.period == "whole day")

    curve = [(L, whole(f"lag {L} s", 1).diff) for L in CURVE_LAGS_S]
    print(f"{len(days)} days; the curve at 1 s: " + ", ".join(f"{L} s {d:+.2f}" for L, d in curve))
    print("against the read minus the rest, cents a share, within days, 95 % interval\n")
    print(f"  {'read':<18}{'covers':>7}{'agrees':>8}{'1 s diff':>9}{'95 %':>17}{'equiv. lag':>12}"
          f"{'10 s diff':>10}{'95 %':>17}  worse 1 s")
    for name, reading in READS:
        cov, agree = coverage_and_agreement(days, reading)
        r1, r10 = whole(name, 1), whole(name, 10)
        lag = equivalent_lag(r1.diff, curve)
        lag_text = "nothing" if lag is None else f"{lag:.0f} s"
        print(f"  {name:<18}{cov:>7.0%}{'-' if agree is None else f'{agree:.0%}':>8}{r1.diff:>+9.2f}"
              f"{f'[{r1.low:+.2f}, {r1.high:+.2f}]':>17}{lag_text:>12}{r10.diff:>+10.2f}"
              f"{f'[{r10.low:+.2f}, {r10.high:+.2f}]':>17}  {r1.days_worse}/{r1.days}")

    # The price-action reads by session: where in the day does each one work, if at all?
    pa = [s for s in read_signals if s.name in PRICE_ACTION]
    print("\nby session (price-action reads): against minus rest, cents a share, 95 %")
    print(f"  {'read':<18}{'session':<14}{'covers':>7}{'1 s diff':>9}{'95 %':>17}"
          f"{'10 s diff':>10}{'95 %':>17}  worse 1 s")
    for name, start, end in SESSIONS:
        part = in_session(days, start, end)
        res = ss.study_days(part, a.resamples, a.seed, signals=pa)
        for s in pa:
            reading = dict(READS)[s.name]
            cov, _ = coverage_and_agreement(part, reading)
            r1, r10 = [next(r for r in res if r.signal == s.name and r.horizon == h
                            and r.period == "whole day") for h in (1, 10)]
            print(f"  {s.name:<18}{name:<14}{cov:>7.1%}{ss._f(r1.diff):>9}"
                  f"{f'[{ss._f(r1.low)}, {ss._f(r1.high)}]':>17}{ss._f(r10.diff):>10}"
                  f"{f'[{ss._f(r10.low)}, {ss._f(r10.high)}]':>17}  {r1.days_worse}/{r1.days}")


if __name__ == "__main__":
    main()
