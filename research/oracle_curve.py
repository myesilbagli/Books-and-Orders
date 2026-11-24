"""How fast would a trend read have to be? The delayed-oracle curve (round 7c, D61).

    research/.venv/bin/python research/oracle_curve.py data/sim/days/AAPL/*/naive_0us.csv

The hindsight ZigZag leg of fill_anatomy.py (1 min, c = 1.5), learned only L seconds after
its turning bar; until then the trader believes the leg before. For each L: fills against
what he believes vs the rest, within each day, block bootstrap (D56), at 1 s and 10 s, and
the share of the no-lag gap left. Context: how long legs last and how late the real
swings are confirmed. A descriptive curve: no primary test. Standard library only.
"""

import argparse
import statistics

import fill_anatomy as fa
import signal_study as ss

LAGS_S = (0, 5, 15, 30, 60, 120, 300)


def with_lags(rows, swings, lags_s):
    """Each fill's believed leg for every lag, added to its readings as leg_<L>s."""
    out = []
    for r in rows:
        t = r.fill.timestamp_ns
        extra = {f"leg_{L}s": fa.delayed_leg(swings, t, L * fa.SECOND_NS) for L in lags_s}
        out.append(ss.Row(fill=r.fill, indicators=dict(r.indicators, **extra)))
    return out


def signals(lags_s):
    return [ss.Signal(f"lag {L} s", f"against the leg learned {L} s late",
                      fa._leg_reading(f"leg_{L}s"), primary=False) for L in lags_s]


def context(all_swings):
    """Median leg length and median confirmation delay (after the turning bar), in seconds."""
    legs, delays = [], []
    for swings in all_swings:
        legs += [(b.made_ns - a.made_ns) / fa.SECOND_NS for a, b in zip(swings, swings[1:])]
        delays += [(s.confirmed_ns - s.made_ns - fa.BAR_NS) / fa.SECOND_NS for s in swings]
    return statistics.median(legs), statistics.median(delays)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("csv", nargs="+", help="fills CSVs, one per day, in data/sim/days/<SYM>/<day>/")
    p.add_argument("--charts", default="data/sim/chart/AAPL")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    all_swings = [fa._swings_for(path, a.charts) for path in sorted(a.csv)]
    days = [with_lags(fa.read_day(path, sw), sw, LAGS_S)
            for path, sw in zip(sorted(a.csv), all_swings)]
    results = ss.study_days(days, a.resamples, a.seed, signals=signals(LAGS_S))
    leg, delay = context(all_swings)
    print(f"{len(days)} days; 1 min legs last {leg:.0f} s (median); a swing is confirmed "
          f"{delay:.0f} s after its bar ends (median)")
    print("against minus rest, cents a share, within days, 95 % interval; left = share of the"
          " no-lag gap")
    for h in ss.HORIZONS:
        whole = [r for r in results if r.horizon == h and r.period == "whole day"]
        base = whole[0].diff
        print(f"\n  {h} s  {'lag':>6}{'against ¢':>11}{'rest ¢':>9}{'diff':>8}{'95 %':>18}"
              f"{'left':>7}  worse")
        for L, r in zip(LAGS_S, whole):
            ci = f"[{r.low:+.2f}, {r.high:+.2f}]"
            print(f"      {L:>5} s{ss._f(r.flagged_cents):>11}{ss._f(r.rest_cents):>9}"
                  f"{ss._f(r.diff):>8}{ci:>18}{r.diff / base:>7.0%}  {r.days_worse}/{r.days}")


if __name__ == "__main__":
    main()
