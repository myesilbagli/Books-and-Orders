"""Block-bootstrap confidence intervals for the simulator's results (D37).

Reads the fills CSVs `mm_sim` writes and answers: is this strategy's result, or the
difference between two strategies, more than one day's noise?

    research/.venv/bin/python research/bootstrap.py data/sim/ci/naive_0us.csv data/sim/ci/as10_0us.csv

The first file is the baseline; every other file is compared with it, block by block.
Standard library only.

What is measured: the P&L per share at a horizon h (1, 10 or 60 s), i.e. spread plus
adverse selection (D35). Inventory, the move from h to the close, belongs to the whole
day's path and can't be cut into blocks, so it isn't bootstrapped.
"""

import argparse
import csv
import math
import random
from dataclasses import dataclass
from pathlib import Path

START_NS = (9 * 3600 + 31 * 60) * 1_000_000_000  # quoting starts 09:31 ET
END_NS = 16 * 3600 * 1_000_000_000               # and stops at 16:00 ET
MINUTE_NS = 60 * 1_000_000_000


@dataclass(frozen=True)
class Fill:
    timestamp_ns: int
    side: str           # "B" or "S"
    price: int          # ticks of $0.0001
    quantity: int
    mid_x2_before: int  # twice the mid just before the fill
    mid_x2_1s: int      # twice the mid 1 / 10 / 60 s later (capped at the end)
    mid_x2_10s: int
    mid_x2_60s: int


def read_fills(path):
    with open(path, newline="") as f:
        return [Fill(timestamp_ns=int(r["timestamp_ns"]), side=r["side"], price=int(r["price"]),
                     quantity=int(r["quantity"]), mid_x2_before=int(r["mid_x2_before"]),
                     mid_x2_1s=int(r["mid_x2_1s"]), mid_x2_10s=int(r["mid_x2_10s"]),
                     mid_x2_60s=int(r["mid_x2_60s"]))
                for r in csv.DictReader(f)]


def fill_pnl_ticks(fill, horizon_s):
    """Spread + adverse selection of one fill at the horizon, in ticks x shares.

    s * q * (mid_h - price): the same as the first two parts of the D35 breakdown, since
    (mid_before - price) + (mid_h - mid_before) = mid_h - price.
    """
    mid_x2 = {1: fill.mid_x2_1s, 10: fill.mid_x2_10s, 60: fill.mid_x2_60s}[horizon_s]
    sign = 1 if fill.side == "B" else -1
    return sign * fill.quantity * (mid_x2 / 2 - fill.price)


def block_sums(fills, horizon_s, block_ns, start_ns=START_NS, end_ns=END_NS):
    """P&L and shares per time block. Blocks with no fills stay in, as zeros: a quiet
    stretch is part of the day, and dropping it would change what a "day" is."""
    n = math.ceil((end_ns - start_ns) / block_ns)
    pnl = [0.0] * n
    shares = [0] * n
    for f in fills:
        b = min(max((f.timestamp_ns - start_ns) // block_ns, 0), n - 1)
        pnl[b] += fill_pnl_ticks(f, horizon_s)
        shares[b] += f.quantity
    return pnl, shares


def _cents_per_share(pnl, shares):
    return pnl / shares / 100 if shares else 0.0  # 1 cent = 100 ticks


def _percentile(sorted_values, q):
    """Linear interpolation between the two nearest ranks."""
    pos = q * (len(sorted_values) - 1)
    lo = math.floor(pos)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (pos - lo)


def _resampled(n, resamples, seed):
    """The same block draws for every call with the same seed: that is what lets two
    runs be compared on identical resampled days (paired)."""
    rng = random.Random(seed)
    for _ in range(resamples):
        yield [rng.randrange(n) for _ in range(n)]  # n blocks, drawn with replacement


def bootstrap_ci(pnl_blocks, share_blocks, resamples=2000, seed=0, level=0.95):
    """(estimate, low, high) of the P&L per share in cents.

    Ratio of sums (total P&L / total shares), not the average of each block's ratio: a
    block with 10 shares shouldn't count as much as one with 10,000.
    """
    estimate = _cents_per_share(sum(pnl_blocks), sum(share_blocks))
    stats = sorted(
        _cents_per_share(sum(pnl_blocks[i] for i in idx), sum(share_blocks[i] for i in idx))
        for idx in _resampled(len(pnl_blocks), resamples, seed))
    tail = (1 - level) / 2
    return estimate, _percentile(stats, tail), _percentile(stats, 1 - tail)


def paired_diff_ci(base_pnl, base_shares, other_pnl, other_shares, resamples=2000, seed=0,
                   level=0.95):
    """(estimate, low, high) of other minus base, in cents per share.

    Both runs are resampled with the same blocks each time. A block where the market
    moved hard hurts both, so that shared noise cancels in the difference.
    """
    def diff(idx):
        return (_cents_per_share(sum(other_pnl[i] for i in idx), sum(other_shares[i] for i in idx))
                - _cents_per_share(sum(base_pnl[i] for i in idx), sum(base_shares[i] for i in idx)))

    everything = range(len(base_pnl))
    estimate = diff(everything)
    stats = sorted(diff(idx) for idx in _resampled(len(base_pnl), resamples, seed))
    tail = (1 - level) / 2
    return estimate, _percentile(stats, tail), _percentile(stats, 1 - tail)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("csv", nargs="+", help="fills CSVs from mm_sim; the first is the baseline")
    parser.add_argument("--horizon", type=int, choices=(1, 10, 60), default=10, help="seconds")
    parser.add_argument("--block-min", type=float, default=5.0, help="block length in minutes")
    parser.add_argument("--resamples", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args(argv)

    block_ns = int(args.block_min * MINUTE_NS)
    runs = []
    for path in args.csv:
        pnl, shares = block_sums(read_fills(path), args.horizon, block_ns)
        runs.append((Path(path).stem, pnl, shares))

    print(f"P&L per share at {args.horizon} s (spread + adverse selection), cents, 95% interval;"
          f" {len(runs[0][1])} blocks of {args.block_min:g} min, {args.resamples} resamples")
    print(f"  {'run':<14}{'shares':>9}{'estimate':>10}{'interval':>20}"
          f"{'vs ' + runs[0][0]:>16}{'interval':>20}  verdict")
    _, base_pnl, base_shares = runs[0]
    for name, pnl, shares in runs:
        est, low, high = bootstrap_ci(pnl, shares, args.resamples, args.seed)
        line = f"  {name:<14}{sum(shares):>9,}{est:>10.3f}{f'[{low:+.3f}, {high:+.3f}]':>20}"
        if name != runs[0][0]:
            d, dlow, dhigh = paired_diff_ci(base_pnl, base_shares, pnl, shares, args.resamples,
                                            args.seed)
            verdict = "noise" if dlow <= 0 <= dhigh else ("better" if dlow > 0 else "worse")
            line += f"{d:>+16.3f}{f'[{dlow:+.3f}, {dhigh:+.3f}]':>20}  {verdict}"
        print(line)


if __name__ == "__main__":
    main()
