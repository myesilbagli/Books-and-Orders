"""Which days to study (D53): development days drawn at random, hold-out days the latest.

    research/.venv/bin/python research/pick_days.py

Development: 10 trading days drawn from January-September 2026 with a fixed seed, so many
months (and market moods) are in it and nobody hand-picked them. Hold-out: the last 10
trading days to 20 Nov 2026, the newest data, not looked at until the end.
Standard library only.
"""

import argparse
import random
from datetime import date, timedelta

# NYSE and Nasdaq full-day closures in 2026.
HOLIDAYS = {
    date(2026, 1, 1),    # New Year's Day
    date(2026, 1, 19),   # Martin Luther King Jr. Day
    date(2026, 2, 16),   # Washington's Birthday
    date(2026, 4, 3),    # Good Friday
    date(2026, 5, 25),   # Memorial Day
    date(2026, 6, 19),   # Juneteenth
    date(2026, 7, 3),    # Independence Day, observed (the 4th is a Saturday)
    date(2026, 9, 7),    # Labor Day
    date(2026, 11, 26),  # Thanksgiving
    date(2026, 12, 25),  # Christmas
}
# Early closes (13:00): left out, since quoting runs to 16:00.
HALF_DAYS = {date(2026, 11, 27), date(2026, 12, 24)}

SEED = 20261123  # the date the draw was made
DEV_FROM, DEV_TO = date(2026, 1, 1), date(2026, 9, 30)
HOLDOUT_END = date(2026, 11, 20)


def trading_days(start, end):
    """Every full trading day from start to end, both included."""
    days, d = [], start
    while d <= end:
        if d.weekday() < 5 and d not in HOLIDAYS and d not in HALF_DAYS:
            days.append(d)
        d += timedelta(days=1)
    return days


def draw(pool, n, seed):
    """n different days from pool, in date order; the same seed gives the same days."""
    return sorted(random.Random(seed).sample(pool, n))


def last_days(end, n):
    """The last n trading days up to end."""
    return trading_days(end - timedelta(days=3 * n), end)[-n:]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--n", type=int, default=10)
    p.add_argument("--seed", type=int, default=SEED)
    a = p.parse_args(argv)
    print("development:", " ".join(d.isoformat() for d in draw(trading_days(DEV_FROM, DEV_TO), a.n, a.seed)))
    print("hold-out:   ", " ".join(d.isoformat() for d in last_days(HOLDOUT_END, a.n)))


if __name__ == "__main__":
    main()
