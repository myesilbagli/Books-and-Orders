"""Does a chart reading pick out the fills that lose money? (milestone 6, stage 1, D47)

    research/.venv/bin/python research/signal_study.py data/sim/m5/naive_0us.csv

For each signal (1 min and 1 s EMA trend, VWAP), a fill is "flagged" when we traded against the signal's reading: we
bought while it said the price was low or falling, or sold while it said high or rising.
Those are the fills a rule on that signal would have stepped away from. The study
compares the flagged fills' P&L per share with the rest's, at 1 s and 10 s, over the
whole day and each half, with a block-bootstrap interval for the difference.

Whether flagged fills are worse (the trend view) or better (the mean-reversion view) is
the hypothesis, written down before running this. This script only measures.

Uses what the simulator recorded at each fill (D45) and bootstrap.py's P&L and
intervals. Standard library only.
"""

import argparse
import csv
from dataclasses import dataclass

import bootstrap as bs

INDICATOR_COLUMNS = [f"{name}_{tf}" for tf in ("1s", "1m")
                     for name in ("ema_fast", "ema_slow", "sma", "bb_pct", "rsi", "stoch_k",
                                  "stoch_d")] + ["vwap"]
HORIZONS = (1, 10)
PRIMARY_HORIZON = 10  # the horizon milestone 5 judged everything at
BLOCK_NS = 5 * bs.MINUTE_NS


@dataclass(frozen=True)
class Row:
    fill: bs.Fill
    indicators: dict  # column -> value, None while warming up


def _sign(x):
    return (x > 0) - (x < 0)  # booleans count as 1 / 0: -1, 0 or +1


# Each reading: -1 says the price is low or falling, +1 high or rising, 0 neither, None
# when the indicator isn't there yet. Prices in ticks; mid is the mid just before the fill.

def _trend(tf):
    def reading(ind, mid):
        fast, slow = ind[f"ema_fast_{tf}"], ind[f"ema_slow_{tf}"]
        if fast is None or slow is None:
            return None
        return _sign(fast - slow)
    return reading


def _vwap(ind, mid):
    return None if ind["vwap"] is None else _sign(mid - ind["vwap"])


@dataclass(frozen=True)
class Signal:
    name: str
    rule: str  # what flags a buy; a sell is the mirror
    reading: object  # function (indicators, mid) -> -1 / 0 / +1 / None


SIGNALS = [
    Signal("1m trend", "1 min EMA9 < EMA21", _trend("1m")),
    Signal("1s trend", "1 s EMA9 < EMA21", _trend("1s")),
    Signal("VWAP", "mid < VWAP", _vwap),
]  # Bollinger and Stoch RSI left out for now: the founder doesn't expect them to work (D47)
# The primary tests' corrected interval: Bonferroni keeps the chance of any false
# "real" among them at 5 %, so each one is held to 5 % / their number.
CORRECTED_LEVEL = 1 - 0.05 / len(SIGNALS)


def flagged(side, reading):
    return (side == "B" and reading == -1) or (side == "S" and reading == 1)


def read_rows(path):
    with open(path, newline="") as f:
        return [Row(fill=bs.Fill(timestamp_ns=int(r["timestamp_ns"]), side=r["side"],
                                 price=int(r["price"]), quantity=int(r["quantity"]),
                                 mid_x2_before=int(r["mid_x2_before"]),
                                 mid_x2_1s=int(r["mid_x2_1s"]), mid_x2_10s=int(r["mid_x2_10s"]),
                                 mid_x2_60s=int(r["mid_x2_60s"])),
                    indicators={c: float(r[c]) if r[c] != "" else None for c in INDICATOR_COLUMNS})
                for r in csv.DictReader(f)]


@dataclass(frozen=True)
class Result:
    signal: str
    horizon: int
    period: str
    primary: bool
    missing: int           # fills left out: the indicator wasn't there yet
    flagged_share: float   # flagged shares / all shares compared
    flagged_cents: object  # cents a share; None without fills
    rest_cents: object
    diff: object           # flagged minus rest; None if either group is empty
    low: object            # 95 % interval of diff
    high: object
    low_99: object = None  # Bonferroni-corrected interval (CORRECTED_LEVEL), primary only
    high_99: object = None
    verdict: str = ""


def verdict(diff, low, high):
    if diff is None:
        return "no data"
    if high < 0:
        return "flagged worse"
    if low > 0:
        return "flagged better"
    return "noise"


def _cents(pnl, shares):
    return sum(pnl) / sum(shares) / 100 if sum(shares) else None  # 1 cent = 100 ticks


def _compare(signal, horizon, period, primary, missing, fl, rest, resamples, seed):
    """fl and rest: (pnl blocks, share blocks) for the period."""
    total = sum(fl[1]) + sum(rest[1])
    fields = dict(signal=signal, horizon=horizon, period=period, primary=primary,
                  missing=missing, flagged_share=sum(fl[1]) / total if total else 0.0,
                  flagged_cents=_cents(*fl), rest_cents=_cents(*rest),
                  diff=None, low=None, high=None)
    if sum(fl[1]) and sum(rest[1]):
        # Same blocks for both groups on every resample (paired, D37): a block where the
        # price ran hurts both, and that shared noise cancels in the difference. A resample
        # that happens to hold no flagged shares counts as 0 (bootstrap.py's rule); with
        # few flagged fills, read the interval with care.
        fields["diff"], fields["low"], fields["high"] = bs.paired_diff_ci(
            rest[0], rest[1], fl[0], fl[1], resamples, seed)
        if primary:
            _, fields["low_99"], fields["high_99"] = bs.paired_diff_ci(
                rest[0], rest[1], fl[0], fl[1], resamples, seed, level=CORRECTED_LEVEL)
    return Result(**fields, verdict=verdict(fields["diff"], fields["low"], fields["high"]))


def study(rows, resamples=2000, seed=0, block_ns=BLOCK_NS):
    results = []
    for s in SIGNALS:
        fl, rest, missing = [], [], 0
        for r in rows:
            reading = s.reading(r.indicators, r.fill.mid_x2_before / 2)
            if reading is None:
                missing += 1
            elif flagged(r.fill.side, reading):
                fl.append(r.fill)
            else:
                rest.append(r.fill)
        for h in HORIZONS:
            fl_blocks = bs.block_sums(fl, h, block_ns)
            rest_blocks = bs.block_sums(rest, h, block_ns)
            half = len(fl_blocks[0]) // 2  # 78 blocks: 09:31-12:46 and 12:46-16:00
            periods = [("whole day", slice(None)), ("morning", slice(0, half)),
                       ("afternoon", slice(half, None))]
            for period, part in periods:
                results.append(_compare(
                    s.name, h, period, h == PRIMARY_HORIZON and period == "whole day", missing,
                    (fl_blocks[0][part], fl_blocks[1][part]),
                    (rest_blocks[0][part], rest_blocks[1][part]), resamples, seed))
    return results


def _f(x, fmt="+.2f"):
    return "-" if x is None else format(x, fmt)


def report(path, results, resamples):
    primary = sum(r.primary for r in results)
    print(f"Signal study: {path}")
    print("Flagged = we bought while the signal read low/falling, or sold while it read high/rising.")
    print(f"P&L per share (spread + adverse selection), cents; diff = flagged - rest, 95 % interval,"
          f" 5 min blocks, {resamples} resamples.")
    print(f"{len(results)} tests, {primary} primary (10 s, whole day; also a {CORRECTED_LEVEL:.1%}"
          f" interval, Bonferroni for {primary}). At 95 %, about 1 test in 20 looks real by chance.\n")
    for s in SIGNALS:
        rs = [r for r in results if r.signal == s.name]
        print(f"{s.name}: a buy is flagged when {s.rule}; {rs[0].missing} fills without the indicator")
        print(f"  {'h':>4} {'period':<10}{'flagged':>9}{'flagged ¢':>11}{'rest ¢':>9}{'diff':>8}"
              f"{'95 %':>18}{'corrected':>18}  verdict")
        for r in rs:
            ci = "-" if r.low is None else f"[{r.low:+.2f}, {r.high:+.2f}]"
            ci99 = "" if r.low_99 is None else f"[{r.low_99:+.2f}, {r.high_99:+.2f}]"
            mark = " *" if r.primary else ""
            print(f"  {r.horizon:>3}s {r.period:<10}{r.flagged_share:>9.1%}{_f(r.flagged_cents):>11}"
                  f"{_f(r.rest_cents):>9}{_f(r.diff):>8}{ci:>18}{ci99:>18}  {r.verdict}{mark}")
        print()
    print("* primary test")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("csv", help="fills CSV from mm_sim, with the chart columns (D45)")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    report(a.csv, study(read_rows(a.csv), a.resamples, a.seed), a.resamples)


if __name__ == "__main__":
    main()
