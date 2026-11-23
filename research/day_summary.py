"""One line per day from mm_sim's reports, and all days together (6d-1).

    research/.venv/bin/python research/day_summary.py data/sim/days/AAPL/*/naive_0us.txt

Each report's day is the name of its folder. Parts in cents per share, adverse selection
over 10 s (the horizon of every milestone 5 table). Descriptive only: no test is counted.
Standard library only.
"""

import argparse
import os
import re


def _number(text):
    return float(text.replace(",", ""))


def parse(report):
    """The counts and the P&L parts (dollars, adverse selection at 10 s) of one report."""
    if "check: total vs simulator P&L agree" not in report:
        raise ValueError("the report's P&L parts don't add up")  # mm_sim's own check failed
    fills = re.search(r"^fills:\s+(\d+)\s+\(\d+ buys / (\d+) sh, \d+ sells / (\d+) sh\)", report,
                      re.M)
    position = re.search(r"max \|position\| (\d+) sh", report)
    pnl = re.search(r"^P&L:\s+\$(-?[\d.,]+)", report, re.M)
    d = {"fills": int(fills[1]), "shares": int(fills[2]) + int(fills[3]),
         "max_position": int(position[1]), "pnl": _number(pnl[1])}
    for key, label in (("spread", "spread captured"), ("adverse", "adverse selection"),
                       ("inventory", "inventory")):
        # Columns: $ and ¢ at 1 s, at 10 s, at 60 s; the 10 s dollars are the third number.
        row = re.search(rf"^\s+{label}\s+(.+)$", report, re.M)[1].split()
        d[key] = int(_number(row[2]))
    return d


def pooled(days):
    """All days as one: sums, except the largest position, which is the largest of any day."""
    out = {k: sum(d[k] for d in days) for k in ("fills", "shares", "pnl", "spread", "adverse",
                                                 "inventory")}
    out["max_position"] = max(d["max_position"] for d in days)
    return out


def cents(dollars, shares):
    return 100.0 * dollars / shares


def _row(name, d):
    s = d["shares"]
    return (f"{name:<10} {d['fills']:>7,} {s:>10,} {d['max_position']:>6,} "
            f"{cents(d['spread'], s):>7.2f} {cents(d['adverse'], s):>8.2f} "
            f"{cents(d['inventory'], s):>7.2f} {cents(d['pnl'], s):>7.2f} {d['pnl']:>11,.0f}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("reports", nargs="+", help="mm_sim reports, one per day, in a folder named after it")
    a = p.parse_args(argv)
    days = []
    for path in sorted(a.reports):
        with open(path) as f:
            days.append((os.path.basename(os.path.dirname(os.path.abspath(path))), parse(f.read())))

    print("cents per share; adverse selection over 10 s; P&L in $ (inventory at the 16:00 mid)")
    print(f"{'day':<10} {'fills':>7} {'shares':>10} {'max':>6} {'spread':>7} {'adverse':>8} "
          f"{'invent':>7} {'net':>7} {'P&L $':>11}")
    for name, d in days:
        print(_row(name, d))
    print(_row("all", pooled([d for _, d in days])))
    losing = sum(d["pnl"] < 0 for _, d in days)
    print(f"days with a loss: {losing} of {len(days)}")


if __name__ == "__main__":
    main()
