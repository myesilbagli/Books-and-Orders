"""Hold-out verdict (frozen 26 Nov 2026, before the data was bought): P&L per day of
pullb1open_0us minus naive_0us, mean over days, day bootstrap 95 % interval.

    research/.venv/bin/python <this> AAPL 202611

Passes when the interval's lower end is above 0.
"""
import random
import sys

sys.path.insert(0, "research")
import day_summary as ds  # noqa: E402
import glob  # noqa: E402
import os  # noqa: E402

RESAMPLES, SEED = 5000, 0
symbol, prefix = sys.argv[1], sys.argv[2]
diffs = []
for d in sorted(glob.glob(f"data/sim/days/{symbol}/{prefix}*")):
    base = ds.parse(open(os.path.join(d, "naive_0us.txt")).read())["pnl"]
    pull = ds.parse(open(os.path.join(d, "pullb1open_0us.txt")).read())["pnl"]
    diffs.append(pull - base)
    print(f"{os.path.basename(d)}  naive {base:>10,.0f}  pull {pull:>10,.0f}  diff {pull - base:>+8,.0f}")
n = len(diffs)
mean = sum(diffs) / n
rng = random.Random(SEED)
means = sorted(sum(rng.choice(diffs) for _ in range(n)) / n for _ in range(RESAMPLES))
low, high = means[int(0.025 * RESAMPLES)], means[int(0.975 * RESAMPLES) - 1]
better = sum(x > 0 for x in diffs)
print(f"{symbol}: {n} days, mean {mean:+,.0f} $/day [{low:+,.0f}, {high:+,.0f}], {better}/{n} days better")
print("PASS" if low > 0 else "FAIL", "(lower end of the 95 % interval above 0)")
