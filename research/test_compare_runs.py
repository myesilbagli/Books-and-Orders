"""Tests for compare_runs.py. Run from research/: .venv/bin/python -m unittest test_compare_runs"""

import unittest

import bootstrap as bs
import compare_runs as cr

S = 1_000_000_000


def fill(t_s, side, mid_after, qty=100):
    # At 1.0000 (10'000 ticks), mid 1.0000 before; mid_after at 1 s, 10 s and 60 s.
    return bs.Fill(timestamp_ns=bs.START_NS + t_s * S, side=side, price=10_000, quantity=qty,
                   mid_x2_before=20_000, mid_x2_1s=2 * mid_after, mid_x2_10s=2 * mid_after,
                   mid_x2_60s=2 * mid_after)


class Compare(unittest.TestCase):
    # The other run is 0.05 ¢ a share better than the base on both days: +0.05, worse on none.
    def test_other_minus_base_within_days(self):
        base = [[fill(10, "B", 9_990)], [fill(20, "S", 10_010)]]   # -0.10 ¢ each day
        other = [[fill(10, "B", 9_995)], [fill(20, "S", 10_005)]]  # -0.05 ¢ each day
        r = cr.compare(base, other, horizon=10, resamples=50, seed=0, level=0.975, draws={})
        self.assertAlmostEqual(r.diff, 0.05)
        self.assertEqual((r.days, r.days_worse), (2, 0))
        self.assertAlmostEqual(r.flagged_cents, -0.05)  # the other run, pooled
        self.assertAlmostEqual(r.rest_cents, -0.10)     # the base, pooled


class BrokeShare(unittest.TestCase):
    # Share of the shares filled with nothing left at our price.
    def test_share_of_shares(self):
        self.assertAlmostEqual(cr.broke_share([0, 7, 0], [100, 100, 200]), 0.75)


if __name__ == "__main__":
    unittest.main()
