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
          "ema_fast_1m,ema_slow_1m,sma_1m,bb_pct_1m,rsi_1m,stoch_k_1m,stoch_d_1m,vwap,"
          "structure_1s,structure_15s,structure_1m,structure_5m\n")


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

    def test_only_the_three_signals_the_founder_expects_something_from(self):
        self.assertEqual([s.name for s in ss.SIGNALS], ["1m trend", "1s trend", "VWAP"])


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
                        "10004.12,,,,,,,,,,,,,,10002.25,,,-2,\n")
            rows = ss.read_rows(path)
        self.assertEqual(rows[0].fill.mid_x2_10s, 20020)
        self.assertEqual(rows[0].indicators["ema_fast_1s"], 10004.12)
        self.assertIsNone(rows[0].indicators["ema_slow_1s"])
        self.assertEqual(rows[0].indicators["vwap"], 10002.25)
        self.assertEqual(rows[0].indicators["structure_1m"], -2)
        self.assertIsNone(rows[0].indicators["structure_15s"])


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

    # 3 signals x 2 horizons x 3 periods; the primary ones are 10 s over the whole day,
    # and Bonferroni holds each of the 3 to 5 % / 3.
    def test_counts_every_test(self):
        self.assertEqual(len(self.results), 18)
        self.assertEqual(sum(r.primary for r in self.results), 3)
        self.assertAlmostEqual(ss.CORRECTED_LEVEL, 1 - 0.05 / 3)

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



class Round2Readings(unittest.TestCase):
    # Structure reads -1 only in a downtrend (-2) and +1 only in an uptrend (+2): a mixed
    # score is a range, not a weak trend.
    def test_structure_reading(self):
        read = ss.structure_reading("1m")
        self.assertEqual(read(ind(structure_1m=-2), 0), -1)
        self.assertEqual(read(ind(structure_1m=2), 0), 1)
        self.assertEqual(read(ind(structure_1m=0), 0), 0)
        self.assertEqual(read(ind(structure_1m=1), 0), 0)
        self.assertIsNone(read(ind(), 0))  # before two highs and two lows

    # B: a trend only when the 1 min EMAs agree with the structure (the EMAs as support).
    def test_structure_with_ema_needs_both(self):
        read = ss.structure_ema_reading
        self.assertEqual(read(ind(structure_1m=-2, ema_fast_1m=99.0, ema_slow_1m=100.0), 0), -1)
        self.assertEqual(read(ind(structure_1m=-2, ema_fast_1m=101.0, ema_slow_1m=100.0), 0), 0)
        self.assertEqual(read(ind(structure_1m=2, ema_fast_1m=101.0, ema_slow_1m=100.0), 0), 1)
        self.assertIsNone(read(ind(structure_1m=-2), 0))  # EMAs still warming up

    # C groups the fills by regime, whatever their side.
    def test_in_range(self):
        in_range = ss.in_range("1m")
        self.assertTrue(in_range(ind(structure_1m=0)))
        self.assertTrue(in_range(ind(structure_1m=-1)))
        self.assertFalse(in_range(ind(structure_1m=-2)))
        self.assertIsNone(in_range(ind()))

    def test_round_two_signals(self):
        self.assertEqual([(s.name, s.primary) for s in ss.ROUNDS[2]],
                         [("1m structure", True), ("1m structure + EMA", True), ("1m range", True),
                          ("15s structure", False), ("15s range", False)])


class Round2Study(unittest.TestCase):
    """Every block: a buy in a 1 min range that makes 1 cent, and a buy in a 1 min
    downtrend (against it) that loses 2. No EMAs, no 15 s structure."""

    @classmethod
    def setUpClass(cls):
        rows = []
        for b in range(78):
            t = bs.START_NS + b * BLOCK + SECOND
            rows.append(row(t, "B", 1.0, structure_1m=0))
            rows.append(row(t + SECOND, "B", -2.0, structure_1m=-2))
        cls.results = ss.study(rows, resamples=200, seed=0, signals=ss.ROUNDS[2])

    def find(self, signal):
        return next(r for r in self.results
                    if (r.signal, r.horizon, r.period) == (signal, 10, "whole day"))

    # 5 signals x 2 horizons x 3 periods; 3 primary, each held to 5 % / 3.
    def test_counts(self):
        self.assertEqual(len(self.results), 30)
        self.assertEqual(sum(r.primary for r in self.results), 3)
        self.assertAlmostEqual(ss.corrected_level(ss.ROUNDS[2]), 1 - 0.05 / 3)

    # A: the downtrend buys (-2) against the range buys (+1).
    def test_against_the_structure(self):
        self.assertAlmostEqual(self.find("1m structure").diff, -3.0)

    # C: range fills (+1) minus trend fills (-2): +3, range better.
    def test_range_against_trend(self):
        r = self.find("1m range")
        self.assertAlmostEqual(r.diff, 3.0)
        self.assertEqual(r.verdict, "flagged better")

    # B needs the EMAs, and 15 s needs its structure: none here, so nothing to compare.
    def test_missing_inputs_are_counted(self):
        self.assertEqual(self.find("1m structure + EMA").missing, 156)
        self.assertEqual(self.find("15s range").verdict, "no data")
        self.assertFalse(self.find("15s range").primary)



def block_rows(per_block):
    """78 blocks of the same fills: per_block is [(side, markout ¢, 1 min structure)]."""
    rows = []
    for b in range(78):
        t = bs.START_NS + b * BLOCK + SECOND
        for i, (side, cents, score) in enumerate(per_block):
            rows.append(row(t + i * SECOND, side, cents, structure_1m=score))
    return rows


class ManyDays(unittest.TestCase):
    """Several days (6d-2): flagged minus rest within each day, then a weighted average."""

    def find(self, results, signal="1m structure", period="whole day"):
        return next(r for r in results
                    if (r.signal, r.horizon, r.period) == (signal, 10, period))

    # Simpson's paradox: each day, the flagged fills are 0.1 ¢ better. But the busy day has
    # more of them and is worse for everyone, so pooling all fills would say -0.7 ¢.
    def test_within_each_day_not_pooled(self):
        quiet = block_rows([("B", -0.5, 0)] * 3 + [("B", -0.4, -2)])
        busy = block_rows([("B", -2.1, 0)] + [("B", -2.0, -2)] * 3)
        r = self.find(ss.study_days([quiet, busy], resamples=50, seed=0, signals=ss.ROUNDS[2]))
        self.assertAlmostEqual(r.flagged_cents, -1.6)  # pooled, as a description
        self.assertAlmostEqual(r.rest_cents, -0.9)
        self.assertAlmostEqual(r.diff, 0.1)
        self.assertAlmostEqual(r.low, 0.1)
        self.assertAlmostEqual(r.high, 0.1)
        self.assertEqual((r.days_worse, r.days), (0, 2))

    # Days weigh by the shares compared: +1 on 200 shares a block, -1 on 600: -0.5.
    def test_days_weigh_by_shares(self):
        small = block_rows([("B", 0.0, 0), ("B", 1.0, -2)])
        large = block_rows([("B", 0.0, 0)] * 3 + [("B", -1.0, -2)] * 3)
        r = self.find(ss.study_days([small, large], resamples=50, seed=0, signals=ss.ROUNDS[2]))
        self.assertAlmostEqual(r.diff, -0.5)
        self.assertEqual((r.days_worse, r.days), (1, 2))

    # A day with no flagged fills has nothing to compare: left out, not counted as 0.
    def test_a_day_without_flagged_fills_is_left_out(self):
        some = block_rows([("B", 0.0, 0), ("B", -1.0, -2)])
        none = block_rows([("B", 5.0, 0)])
        r = self.find(ss.study_days([some, none], resamples=50, seed=0, signals=ss.ROUNDS[2]))
        self.assertAlmostEqual(r.diff, -1.0)
        self.assertEqual((r.days_worse, r.days), (1, 1))

    # One day is the single-day study, to the last digit: same estimate, same resampled
    # blocks, same intervals (so every earlier result still stands).
    def test_one_day_is_the_single_day_study(self):
        rows = []
        for b in range(78):
            t = bs.START_NS + b * BLOCK + SECOND
            rows.append(row(t, "B", (b % 7) - 3.0, structure_1m=0))
            rows.append(row(t + SECOND, "S", (b % 5) - 2.5, structure_1m=2))
        one = ss.study_days([rows], resamples=200, seed=3, signals=ss.ROUNDS[2])
        old = ss.study(rows, resamples=200, seed=3, signals=ss.ROUNDS[2])
        for a, b in zip(one, old):
            self.assertEqual((a.diff, a.low, a.high, a.low_99, a.high_99),
                             (b.diff, b.low, b.high, b.low_99, b.high_99))

    # Each day's blocks are drawn only from that day: one resample is n_k draws per day.
    def test_blocks_stay_in_their_day(self):
        draws = ss.stratified_draws([3, 5], resamples=20, seed=1)
        self.assertEqual(len(draws), 20)
        for day_a, day_b in draws:
            self.assertEqual((len(day_a), len(day_b)), (3, 5))
            self.assertTrue(all(0 <= i < 3 for i in day_a))
            self.assertTrue(all(0 <= i < 5 for i in day_b))
        # with one day, exactly bootstrap.py's draws: the pairing with milestone 5 holds
        self.assertEqual([d[0] for d in ss.stratified_draws([4], 10, 7)],
                         list(bs._resampled(4, 10, 7)))


if __name__ == "__main__":
    unittest.main()
