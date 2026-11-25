"""Tests for taker_summary.py. Run from research/: .venv/bin/python -m unittest test_taker_summary"""

import os
import tempfile
import unittest

import taker_summary as tk

HEAD = "signal_ns,direction,entry_ns,entry_price,exit_ns,exit_price,quantity\n"


def write(rows):
    d = tempfile.mkdtemp()
    path = os.path.join(d, "t.csv")
    with open(path, "w") as f:
        f.write(HEAD + "".join(rows))
    return path


class ReadTrades(unittest.TestCase):
    # A short sold at 276.30 and bought back at 276.10 made 20 c a share before fees; the
    # fee is 0.3 c each way, so 19.4 c net, $19.40 on 100 shares.
    def test_cents_net_of_fees(self):
        t = tk.read_trades(write(["5,-1,6,2763000.00,7,2761000.00,100\n"]))[0]
        self.assertAlmostEqual(t.gross_cents, 20.0)
        self.assertAlmostEqual(t.net_cents, 19.4)
        self.assertAlmostEqual(t.net_dollars, 19.40)


class Summary(unittest.TestCase):
    def test_counts_days_and_worst(self):
        win = "5,1,6,2763000.00,7,2763100.00,100\n"    # +1 c gross, +0.4 net
        loss = "5,1,6,2763000.00,7,2762000.00,100\n"   # -10 c gross, -10.6 net
        days = [tk.read_trades(write([win, win])), tk.read_trades(write([loss]))]
        s = tk.summarize(days, resamples=200, seed=0)
        self.assertEqual(s.trades, 3)
        self.assertAlmostEqual(s.win_rate, 2 / 3)
        self.assertAlmostEqual(s.mean_net_cents, (0.4 + 0.4 - 10.6) / 3)
        self.assertEqual(s.days_positive, 1)
        self.assertAlmostEqual(s.worst_day_dollars, -10.60)
        self.assertAlmostEqual(s.worst_trade_cents, -10.6)
        self.assertAlmostEqual(s.total_dollars, 0.40 + 0.40 - 10.60)
        self.assertLessEqual(s.low, s.mean_net_cents)
        self.assertGreaterEqual(s.high, s.mean_net_cents)

    def test_no_trades(self):
        s = tk.summarize([[], []])
        self.assertEqual(s.trades, 0)
        self.assertIsNone(s.mean_net_cents)


if __name__ == "__main__":
    unittest.main()
