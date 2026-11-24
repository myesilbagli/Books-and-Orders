"""Other mm_sim runs against a base run on the same days, paired by day and 5 min block (D60).

    research/.venv/bin/python research/compare_runs.py naive_0us ladder2_0us ladder3_0us

Reads data/sim/days/<SYM>/<YYYYMMDD>/<run>.csv (research/run_days.sh writes them). For each
other run: cents a share (spread + adverse selection) at 1 s and 10 s, other minus base
within each day, weighted by shares, with the signal study's stratified block bootstrap
(D56); the same blocks for both runs, so a block where the price ran hurts both and
cancels. Also the shares traded and the share of them filled as the level broke (D33).
The 10 s whole-day difference of each run is a primary test. Standard library only.
"""

import argparse
import csv
import glob
import os

import bootstrap as bs
import signal_study as ss


def read_run(path):
    """The fills, and how many shares were left at our price as each one filled."""
    with open(path, newline="") as f:
        left = [int(r["shares_left"]) for r in csv.DictReader(f)]
    return bs.read_fills(path), left


def broke_share(shares_left, quantities):
    total = sum(quantities)
    return sum(q for s, q in zip(shares_left, quantities) if s == 0) / total if total else 0.0


def compare(base_days, other_days, horizon, resamples, seed, level, draws, period=slice(None),
            period_name="whole day", primary=True):
    """signal_study's comparison with "flagged" = the other run and "rest" = the base run."""
    per_day = []
    for base, other in zip(base_days, other_days):
        b_pnl, b_sh = bs.block_sums(base, horizon, ss.BLOCK_NS)
        o_pnl, o_sh = bs.block_sums(other, horizon, ss.BLOCK_NS)
        per_day.append((o_pnl[period], o_sh[period], b_pnl[period], b_sh[period]))
    return ss._compare("other - base", horizon, period_name, primary, 0, per_day, resamples,
                       seed, level, draws)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("base", help="run name, e.g. naive_0us")
    p.add_argument("others", nargs="+", help="run names to compare with it")
    p.add_argument("--days", default="data/sim/days/AAPL", help="one folder per day")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    folders = sorted(d for d in glob.glob(os.path.join(a.days, "*")) if os.path.isdir(d))
    runs = {name: [read_run(os.path.join(d, f"{name}.csv")) for d in folders]
            for name in [a.base] + a.others}
    level = 1 - 0.05 / len(a.others)  # Bonferroni over the primary tests, one per run
    draws = {}

    def describe(name):
        fills = [f for day, _ in runs[name] for f in day]
        left = [s for _, day in runs[name] for s in day]
        shares = sum(f.quantity for f in fills)
        broke = broke_share(left, [f.quantity for f in fills])
        return f"{name}: {shares:,} shares, {broke:.1%} of them as the level broke"

    print(f"{len(folders)} days in {a.days}; cents a share, other minus base within each day, "
          f"95 % interval ({level:.1%} corrected for the primary), {a.resamples} resamples")
    print(describe(a.base))
    for name in a.others:
        print(f"\n{describe(name)}")
        print(f"  {'h':>4} {'period':<10}{'base ¢':>9}{'other ¢':>9}{'diff':>8}{'95 %':>18}"
              f"{'corrected':>18}  worse  verdict")
        base = [day for day, _ in runs[a.base]]
        other = [day for day, _ in runs[name]]
        half = len(bs.block_sums([], 1, ss.BLOCK_NS)[0]) // 2
        for h in ss.HORIZONS:
            for period_name, part in (("whole day", slice(None)), ("morning", slice(0, half)),
                                      ("afternoon", slice(half, None))):
                primary = h == ss.PRIMARY_HORIZON and period_name == "whole day"
                r = compare(base, other, h, a.resamples, a.seed, level, draws, part, period_name,
                            primary)
                ci = f"[{r.low:+.2f}, {r.high:+.2f}]"
                ci2 = f"[{r.low_99:+.2f}, {r.high_99:+.2f}]" if primary else ""
                verdict = {"flagged worse": "other worse", "flagged better": "other better"}.get(
                    r.verdict, r.verdict)
                print(f"  {h:>3}s {period_name:<10}{ss._f(r.rest_cents):>9}{ss._f(r.flagged_cents):>9}"
                      f"{ss._f(r.diff):>8}{ci:>18}{ci2:>18}{r.days_worse:>4}/{r.days:<2} {verdict}"
                      f"{' *' if primary else ''}")
    print("\n* primary test; 'worse' = days the other run did worse per share")


if __name__ == "__main__":
    main()
