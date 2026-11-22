"""Tests for bootstrap.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import os
import tempfile
import unittest

import bootstrap as bs

SECOND = 1_000_000_000
MINUTE = 60 * SECOND
START = bs.START_NS


def fill(t, side="B", price=10_000, qty=100, before=20_010, m1=20_010, m10=20_010, m60=20_010):
    return bs.Fill(timestamp_ns=t, side=side, price=price, quantity=qty, mid_x2_before=before,
                   mid_x2_1s=m1, mid_x2_10s=m10, mid_x2_60s=m60)


class FillPnl(unittest.TestCase):
    # Bought 100 at 1.0000; 10 s later the mid is 1.0010: +10 ticks a share.
    def test_buy_gains_when_the_mid_rises(self):
        self.assertEqual(bs.fill_pnl_ticks(fill(0, m10=20_020), 10), 1_000)

    def test_sell_loses_when_the_mid_rises(self):
        self.assertEqual(bs.fill_pnl_ticks(fill(0, side="S", m10=20_020), 10), -1_000)

    # The horizon picks the column: 1 s later the mid was still 1.0005.
    def test_horizon_picks_the_mid(self):
        f = fill(0, m1=20_010, m10=20_020, m60=19_990)
        self.assertEqual(bs.fill_pnl_ticks(f, 1), 500)
        self.assertEqual(bs.fill_pnl_ticks(f, 60), -500)


class Blocks(unittest.TestCase):
    def test_fills_land_in_their_five_minute_block(self):
        fills = [fill(START), fill(START + 5 * MINUTE - 1), fill(START + 5 * MINUTE)]
        pnl, shares = bs.block_sums(fills, 10, 5 * MINUTE)
        self.assertEqual(shares[0], 200)
        self.assertEqual(shares[1], 100)

    # 09:31-16:00 is 389 minutes: 78 blocks of 5 minutes, the last one shorter.
    def test_the_day_has_78_blocks(self):
        pnl, shares = bs.block_sums([], 10, 5 * MINUTE)
        self.assertEqual(len(pnl), 78)
        self.assertEqual(len(shares), 78)

    def test_a_fill_just_before_the_end_is_in_the_last_block(self):
        pnl, shares = bs.block_sums([fill(bs.END_NS - 1)], 10, 5 * MINUTE)
        self.assertEqual(shares[-1], 100)


class Intervals(unittest.TestCase):
    # Every block earns exactly 5 ticks a share: no noise, so no width.
    def test_identical_blocks_give_a_zero_width_interval(self):
        est, low, high = bs.bootstrap_ci([500.0] * 10, [100] * 10, resamples=200)
        self.assertAlmostEqual(est, 0.05)  # cents per share
        self.assertAlmostEqual(low, 0.05)
        self.assertAlmostEqual(high, 0.05)

    # The estimate weights by shares, it doesn't average the blocks' ratios.
    def test_estimate_is_weighted_by_shares(self):
        est, _, _ = bs.bootstrap_ci([100.0, -900.0], [100, 300], resamples=10)
        self.assertAlmostEqual(est, -800 / 400 / 100)

    def test_interval_contains_the_estimate(self):
        pnl = [300.0, -700.0, 100.0, -200.0, 900.0, -1_200.0, 50.0, 0.0]
        shares = [100, 200, 100, 300, 100, 200, 100, 100]
        est, low, high = bs.bootstrap_ci(pnl, shares, resamples=500)
        self.assertLess(low, est)
        self.assertGreater(high, est)

    def test_same_seed_same_interval(self):
        pnl, shares = [300.0, -700.0, 100.0], [100, 200, 100]
        self.assertEqual(bs.bootstrap_ci(pnl, shares, seed=3), bs.bootstrap_ci(pnl, shares, seed=3))

    # Paired: a run compared with itself differs by exactly zero, every resample.
    def test_a_run_against_itself_differs_by_zero(self):
        pnl, shares = [300.0, -700.0, 100.0], [100, 200, 100]
        est, low, high = bs.paired_diff_ci(pnl, shares, pnl, shares, resamples=200)
        self.assertEqual((est, low, high), (0.0, 0.0, 0.0))

    # Shared noise cancels: the other run is the baseline plus 1 tick a share in every
    # block, so the difference is exactly +0.01 cents however noisy the blocks are.
    def test_paired_difference_removes_shared_noise(self):
        base = [3_000.0, -7_000.0, 1_000.0, -2_000.0]
        shares = [100, 200, 100, 300]
        other = [p + s for p, s in zip(base, shares)]
        est, low, high = bs.paired_diff_ci(base, shares, other, shares, resamples=300)
        self.assertAlmostEqual(est, 0.01)
        self.assertAlmostEqual(low, 0.01)
        self.assertAlmostEqual(high, 0.01)


class ReadFills(unittest.TestCase):
    def test_reads_what_mm_sim_writes(self):
        text = ("timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
                "touch_share,shares_left\n"
                "1000,B,10000,100,20010,20000,20020,19980,0.8,340\n")
        with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
            f.write(text)
            path = f.name
        try:
            fills = bs.read_fills(path)
        finally:
            os.unlink(path)
        self.assertEqual(fills, [fill(1000, m1=20_000, m10=20_020, m60=19_980)])


if __name__ == "__main__":
    unittest.main()
