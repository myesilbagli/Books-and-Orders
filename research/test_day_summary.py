"""Tests for day_summary.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import unittest

import day_summary as ds

# A trimmed mm_sim report: the lines day_summary reads, as mm_sim prints them.
REPORT = """
AAPL (locate 13), latency 0 us, quoting 09:31 ET until 16:00:00 ET
strategy:         naive (join the touch)
fills:            8140  (4209 buys / 234349 sh, 3931 sells / 234130 sh)
inventory at end: 219 sh  (max |position| 500 sh)
quote messages:   135773  (16.7 per fill)
P&L:              $-3796.21  (inventory marked at the 16:00 mid, no fees or rebates)

P&L breakdown ($ and cents per share), adverse selection measured over
                                  1 s            10 s            60 s
  spread captured         4838   1.03     4838   1.03     4838   1.03
  adverse selection      -7901  -1.69    -8779  -1.87    -9559  -2.04
  inventory               -733  -0.16      145   0.03      925   0.20
  total                  -3796  -0.81    -3796  -0.81    -3796  -0.81
  check: total vs simulator P&L agree
"""


class Parse(unittest.TestCase):
    # The dollars of each part at 10 s, the counts, and the P&L to the cent.
    def test_reads_the_report(self):
        d = ds.parse(REPORT)
        self.assertEqual(d["fills"], 8140)
        self.assertEqual(d["shares"], 234349 + 234130)
        self.assertEqual(d["max_position"], 500)
        self.assertEqual(d["pnl"], -3796.21)
        self.assertEqual((d["spread"], d["adverse"], d["inventory"]), (4838, -8779, 145))

    # A report whose parts don't add up (mm_sim says so) must not be summarized.
    def test_refuses_a_failed_check(self):
        with self.assertRaises(ValueError):
            ds.parse(REPORT.replace("agree", "DISAGREE"))


class Pooled(unittest.TestCase):
    # All days together: cents per share is total dollars over total shares, so a busy day
    # weighs more than a quiet one (the average of the daily ¢/share would not).
    def test_pooled_cents_per_share(self):
        a = {"fills": 10, "shares": 1000, "max_position": 100, "pnl": -10.0,
             "spread": 10, "adverse": -22, "inventory": 2}
        b = {"fills": 30, "shares": 3000, "max_position": 300, "pnl": -10.0,
             "spread": 30, "adverse": -40, "inventory": 0}
        p = ds.pooled([a, b])
        self.assertEqual((p["fills"], p["shares"], p["max_position"]), (40, 4000, 300))
        self.assertEqual((p["spread"], p["adverse"], p["inventory"], p["pnl"]), (40, -62, 2, -20.0))
        self.assertAlmostEqual(ds.cents(p["pnl"], p["shares"]), -0.5)


if __name__ == "__main__":
    unittest.main()
