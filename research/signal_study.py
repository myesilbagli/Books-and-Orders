"""Does a chart reading pick out the fills that lose money? (milestone 6, stage 1, D47)

    research/.venv/bin/python research/signal_study.py data/sim/m5/naive_0us.csv

For each signal (round 1: 1 min and 1 s EMA trend, VWAP; round 2: market structure), a fill is "flagged" when we traded against the signal's reading: we
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
                                  "stoch_d")] + ["vwap"] + [
                         f"structure_{tf}" for tf in ("1s", "15s", "1m", "5m")]  # D49
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


# Round 2: market structure (D48-D51).

def structure_reading(tf):
    """-1 in a downtrend (-2), +1 in an uptrend (+2), 0 in a range (anything else)."""
    def reading(ind, mid):
        score = ind[f"structure_{tf}"]
        if score is None:
            return None
        return -1 if score == -2 else (1 if score == 2 else 0)
    return reading


def structure_ema_reading(ind, mid):
    """B: a trend only when the 1 min EMAs agree with the 1 min structure."""
    structure = structure_reading("1m")(ind, mid)
    ema = _trend("1m")(ind, mid)
    if structure is None or ema is None:
        return None
    return structure if structure != 0 and structure == ema else 0


def in_range(tf):
    """C: the fill happened in a range (True) or in a trend (False), whatever its side."""
    def group(ind):
        score = ind[f"structure_{tf}"]
        return None if score is None else score not in (-2, 2)
    return group


@dataclass(frozen=True)
class Signal:
    name: str
    rule: str             # which fills are flagged
    reading: object = None  # (indicators, mid) -> -1 / 0 / +1 / None: flags fills against it
    group: object = None    # or (indicators) -> True / False / None: flags a group directly
    primary: bool = True    # primary: its 10 s whole-day test counts; else supporting only


SIGNALS = [  # round 1 (D47)
    Signal("1m trend", "buys at 1 min EMA9 < EMA21, sells at EMA9 > EMA21", _trend("1m")),
    Signal("1s trend", "buys at 1 s EMA9 < EMA21, sells at EMA9 > EMA21", _trend("1s")),
    Signal("VWAP", "buys below VWAP, sells above", _vwap),
]  # Bollinger and Stoch RSI left out for now: the founder doesn't expect them to work (D47)

ROUND2 = [  # market structure, c = 1.5 on 1 min and 2 on 15 s, picked by eye (D51)
    Signal("1m structure", "buys in a 1 min downtrend (-2), sells in an uptrend (+2)",
           structure_reading("1m")),
    Signal("1m structure + EMA", "the same, only when the 1 min EMAs agree", structure_ema_reading),
    Signal("1m range", "fills in a 1 min range (score not +-2); the rest are in a trend",
           group=in_range("1m")),
    Signal("15s structure", "buys in a 15 s downtrend, sells in an uptrend",
           structure_reading("15s"), primary=False),
    Signal("15s range", "fills in a 15 s range", group=in_range("15s"), primary=False),
]

ROUNDS = {1: SIGNALS, 2: ROUND2}


def corrected_level(signals):
    """The primary tests' corrected interval: Bonferroni keeps the chance of any false
    "real" among them at 5 %, so each one is held to 5 % / their number."""
    return 1 - 0.05 / sum(s.primary for s in signals)


CORRECTED_LEVEL = corrected_level(SIGNALS)


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


def _compare(signal, horizon, period, primary, missing, fl, rest, resamples, seed, level):
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
                rest[0], rest[1], fl[0], fl[1], resamples, seed, level=level)
    return Result(**fields, verdict=verdict(fields["diff"], fields["low"], fields["high"]))


def _is_flagged(signal, row):
    """True / False, or None when the signal's input isn't there yet."""
    if signal.group is not None:
        return signal.group(row.indicators)
    reading = signal.reading(row.indicators, row.fill.mid_x2_before / 2)
    return None if reading is None else flagged(row.fill.side, reading)


def study(rows, resamples=2000, seed=0, block_ns=BLOCK_NS, signals=SIGNALS):
    results = []
    level = corrected_level(signals)
    for s in signals:
        fl, rest, missing = [], [], 0
        for r in rows:
            f = _is_flagged(s, r)
            if f is None:
                missing += 1
            elif f:
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
                    s.name, h, period,
                    s.primary and h == PRIMARY_HORIZON and period == "whole day", missing,
                    (fl_blocks[0][part], fl_blocks[1][part]),
                    (rest_blocks[0][part], rest_blocks[1][part]), resamples, seed, level))
    return results


def _f(x, fmt="+.2f"):
    return "-" if x is None else format(x, fmt)


def report(path, results, resamples, signals=SIGNALS, round_=1):
    primary = sum(r.primary for r in results)
    print(f"Signal study, round {round_}: {path}")
    print(f"P&L per share (spread + adverse selection), cents; diff = flagged - rest, 95 % interval,"
          f" 5 min blocks, {resamples} resamples.")
    print(f"{len(results)} tests, {primary} primary (10 s, whole day; also a"
          f" {corrected_level(signals):.1%} interval, Bonferroni for {primary})."
          f" At 95 %, about 1 test in 20 looks real by chance.\n")
    for s in signals:
        rs = [r for r in results if r.signal == s.name]
        print(f"{s.name}{'' if s.primary else ' (supporting)'}: flagged = {s.rule};"
              f" {rs[0].missing} fills without the input")
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
    p.add_argument("--round", type=int, choices=sorted(ROUNDS), default=1,
                   help="1: EMA trend and VWAP (D47); 2: market structure (D51)")
    a = p.parse_args(argv)
    signals = ROUNDS[a.round]
    report(a.csv, study(read_rows(a.csv), a.resamples, a.seed, signals=signals), a.resamples,
           signals, a.round)


if __name__ == "__main__":
    main()
