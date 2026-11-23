"""Tests for signal_study.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import os
import tempfile
import unittest

import bootstrap as bs
import signal_study as ss

SECOND = 1_000_000_000
BLOCK = 5 * 60 * SECOND
HEADER = ("timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
          "touch_share,shares_left,"
          "ema_fast_1s,ema_slow_1s,sma_1s,bb_pct_1s,rsi_1s,stoch_k_1s,stoch_d_1s,"
          "ema_fast_1m,ema_slow_1m,sma_1m,bb_pct_1m,rsi_1m,stoch_k_1m,stoch_d_1m,vwap\n")


def ind(**values):
    """Indicators at a fill; anything not given is missing (warming up)."""
    return {k: values.get(k) for k in ss.INDICATOR_COLUMNS}


def row(t, side, markout_cents, **indicators):
    """A 100-share fill at 1.0000 whose 1 s and 10 s markouts are `markout_cents`."""
    sign = 1 if side == "B" else -1
    mid_x2 = 2 * (10_000 + sign * round(markout_cents * 100))  # 1 cent = 100 ticks
    fill = bs.Fill(timestamp_ns=t, side=side, price=10_000, quantity=100, mid_x2_before=20_010,
                   mid_x2_1s=mid_x2, mid_x2_10s=mid_x2, mid_x2_60s=mid_x2)
    return ss.Row(fill=fill, indicators=ind(**indicators))


class Readings(unittest.TestCase):
    # -1: the indicator says the price is low or falling; +1: high or rising; 0: neither.
    def test_trend_is_the_sign_of_fast_minus_slow(self):
        trend = ss.SIGNALS[0].reading
        self.assertEqual(trend(ind(ema_fast_1m=99.0, ema_slow_1m=100.0), 100.0), -1)
        self.assertEqual(trend(ind(ema_fast_1m=101.0, ema_slow_1m=100.0), 100.0), 1)
        self.assertEqual(trend(ind(ema_fast_1m=100.0, ema_slow_1m=100.0), 100.0), 0)
        self.assertIsNone(trend(ind(ema_fast_1m=100.0), 100.0))  # slow EMA still warming up

    def test_one_second_trend_reads_the_1s_columns(self):
        trend = ss.SIGNALS[1].reading
        self.assertEqual(trend(ind(ema_fast_1s=99.0, ema_slow_1s=100.0), 100.0), -1)
        self.assertIsNone(trend(ind(ema_fast_1m=99.0, ema_slow_1m=100.0), 100.0))

    def test_vwap_compares_the_mid_with_it(self):
        vwap = ss.SIGNALS[2].reading
        self.assertEqual(vwap(ind(vwap=100.0), 99.5), -1)
        self.assertEqual(vwap(ind(vwap=100.0), 100.5), 1)
        self.assertIsNone(vwap(ind(), 100.0))  # before 09:30 there is no VWAP

    def test_bollinger_is_outside_the_bands_only(self):
        bands = ss.SIGNALS[3].reading
        self.assertEqual(bands(ind(bb_pct_1m=-0.1), 0), -1)
        self.assertEqual(bands(ind(bb_pct_1m=0.5), 0), 0)
        self.assertEqual(bands(ind(bb_pct_1m=1.2), 0), 1)

    def test_stoch_rsi_uses_20_and_80(self):
        stoch = ss.SIGNALS[4].reading
        self.assertEqual(stoch(ind(stoch_k_1m=15.0), 0), -1)
        self.assertEqual(stoch(ind(stoch_k_1m=20.0), 0), 0)  # at the line is not past it
        self.assertEqual(stoch(ind(stoch_k_1m=85.0), 0), 1)


class Flagged(unittest.TestCase):
    # Flagged: we traded against the reading, bought when it said low, sold when it said
    # high. These are the fills a rule on this signal would have stepped away from.
    def test_against_the_reading(self):
        self.assertTrue(ss.flagged("B", -1))
        self.assertTrue(ss.flagged("S", 1))
        self.assertFalse(ss.flagged("B", 1))
        self.assertFalse(ss.flagged("S", -1))
        self.assertFalse(ss.flagged("B", 0))


class ReadRows(unittest.TestCase):
    def test_reads_fills_and_indicators_empty_as_none(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "fills.csv")
            with open(path, "w") as f:
                f.write(HEADER + f"{SECOND},B,10000,100,20010,20000,20020,19980,0.8,340,"
                        "10004.12,,,,,,,,,,,,,,10002.25\n")
            rows = ss.read_rows(path)
        self.assertEqual(rows[0].fill.mid_x2_10s, 20020)
        self.assertEqual(rows[0].indicators["ema_fast_1s"], 10004.12)
        self.assertIsNone(rows[0].indicators["ema_slow_1s"])
        self.assertEqual(rows[0].indicators["vwap"], 10002.25)


def day_of_fills():
    """Every 5-minute block of the quoting day has two buys: one in a 1 min downtrend that
    loses 2 cents a share, one in an uptrend that makes 1 cent. Nothing else is known."""
    rows = []
    for b in range(78):
        t = bs.START_NS + b * BLOCK + SECOND
        rows.append(row(t, "B", -2.0, ema_fast_1m=99.0, ema_slow_1m=100.0))
        rows.append(row(t + SECOND, "B", 1.0, ema_fast_1m=101.0, ema_slow_1m=100.0))
    return rows


class Study(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.results = ss.study(day_of_fills(), resamples=200, seed=0)

    def find(self, signal, horizon, period):
        return next(r for r in self.results
                    if (r.signal, r.horizon, r.period) == (signal, horizon, period))

    # 5 signals x 2 horizons x 3 periods; the primary ones are 10 s over the whole day.
    def test_counts_every_test(self):
        self.assertEqual(len(self.results), 30)
        self.assertEqual(sum(r.primary for r in self.results), 5)

    # The same in every block, so every resampled day gives the same answer: -2 - (+1).
    def test_flagged_minus_the_rest(self):
        r = self.find("1m trend", 10, "whole day")
        self.assertAlmostEqual(r.flagged_cents, -2.0)
        self.assertAlmostEqual(r.rest_cents, 1.0)
        self.assertAlmostEqual(r.diff, -3.0)
        self.assertAlmostEqual(r.low, -3.0)
        self.assertAlmostEqual(r.high, -3.0)
        self.assertAlmostEqual(r.flagged_share, 0.5)
        self.assertEqual(r.verdict, "flagged worse")
        self.assertTrue(r.primary)
        self.assertIsNotNone(r.low_99)

    def test_each_half_of_the_day_on_its_own(self):
        for period in ("morning", "afternoon"):
            r = self.find("1m trend", 10, period)
            self.assertAlmostEqual(r.diff, -3.0)
            self.assertFalse(r.primary)
            self.assertIsNone(r.low_99)  # the corrected interval is for primary tests only

    # No VWAP anywhere: every fill is left out, and there is nothing to compare.
    def test_missing_indicators_are_counted_not_guessed(self):
        r = self.find("VWAP", 10, "whole day")
        self.assertEqual(r.missing, 156)
        self.assertIsNone(r.diff)
        self.assertEqual(r.verdict, "no data")

    def test_noise_when_the_interval_holds_zero(self):
        self.assertEqual(ss.verdict(-0.5, -1.0, 0.2), "noise")
        self.assertEqual(ss.verdict(0.5, 0.1, 0.9), "flagged better")


if __name__ == "__main__":
    unittest.main()
