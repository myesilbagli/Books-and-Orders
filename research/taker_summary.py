"""What the taker made, after fees (round 7f, D65).

    research/.venv/bin/python research/taker_summary.py <run> [<run> ...]

Reads data/sim/days/<SYM>/<day>/taker_<run>.csv (research/run_taker.sh writes them) and
prints, per run: trades, the share that made money after fees, the mean net cents a share
with a 95 % interval (trades resampled within each day), days that made money, the worst
day and trade, and the total in dollars. Fees: Nasdaq's take fee, about 0.3 c a share each
way. Standard library only.
"""

import argparse
import csv
import glob
import os
import random
from dataclasses import dataclass

FEE_CENTS = 0.3  # each way


@dataclass(frozen=True)
class Trade:
    direction: int
    signal_ns: int
    gross_cents: float  # a share, before fees
    quantity: int

    @property
    def net_cents(self):
        return self.gross_cents - 2 * FEE_CENTS

    @property
    def net_dollars(self):
        return self.net_cents * self.quantity / 100


def read_trades(path):
    with open(path, newline="") as f:
        return [Trade(direction=int(r["direction"]), signal_ns=int(r["signal_ns"]),
                      # prices in ticks ($0.0001): 100 ticks are a cent
                      gross_cents=int(r["direction"]) * (float(r["exit_price"]) -
                                                         float(r["entry_price"])) / 100,
                      quantity=int(r["quantity"]))
                for r in csv.DictReader(f)]


@dataclass(frozen=True)
class Summary:
    trades: int
    win_rate: object
    mean_net_cents: object  # a share, over trades
    low: object
    high: object
    days: int
    days_positive: int
    worst_day_dollars: object
    worst_trade_cents: object
    total_dollars: float


def summarize(days, resamples=2000, seed=0, level=0.95):
    """days: [[Trade, ...], ...]. The interval resamples trades within each day, so every
    day keeps its number of trades (trades never overlap: one position at a time)."""
    flat = [t for day in days for t in day]
    if not flat:
        return Summary(0, None, None, None, None, len(days), 0, None, None, 0.0)
    nets = [t.net_cents for t in flat]
    mean = sum(nets) / len(nets)
    rng = random.Random(seed)
    means = []
    for _ in range(resamples):
        total = count = 0
        for day in days:
            for _ in day:
                total += day[rng.randrange(len(day))].net_cents
                count += 1
        means.append(total / count)
    means.sort()
    day_dollars = [sum(t.net_dollars for t in day) for day in days if day]
    return Summary(
        trades=len(flat), win_rate=sum(n > 0 for n in nets) / len(nets),
        mean_net_cents=mean, low=means[int((1 - level) / 2 * resamples)],
        high=means[min(resamples - 1, int((1 + level) / 2 * resamples))],
        days=len(days), days_positive=sum(d > 0 for d in day_dollars),
        worst_day_dollars=min(day_dollars), worst_trade_cents=min(nets),
        total_dollars=sum(t.net_dollars for t in flat))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("runs", nargs="+")
    p.add_argument("--days", default="data/sim/days/AAPL")
    p.add_argument("--resamples", type=int, default=2000)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)
    print(f"net of {FEE_CENTS} c a share each way; interval: trades resampled within days\n")
    print(f"  {'run':<16}{'trades':>7}{'win':>6}{'net c/sh':>10}{'95 %':>18}{'days +':>8}"
          f"{'worst day $':>12}{'worst trade c':>14}{'total $':>9}")
    for run in a.runs:
        paths = sorted(glob.glob(os.path.join(a.days, "2026*", f"taker_{run}.csv")))
        s = summarize([read_trades(path) for path in paths], a.resamples, a.seed)
        if not s.trades:
            print(f"  {run:<16}{0:>7}")
            continue
        print(f"  {run:<16}{s.trades:>7}{s.win_rate:>6.0%}{s.mean_net_cents:>+10.2f}"
              f"{f'[{s.low:+.2f}, {s.high:+.2f}]':>18}{f'{s.days_positive}/{s.days}':>8}"
              f"{s.worst_day_dollars:>+12.2f}{s.worst_trade_cents:>+14.2f}{s.total_dollars:>+9.2f}")


if __name__ == "__main__":
    main()
