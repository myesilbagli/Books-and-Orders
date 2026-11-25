"""Tests for event_study.py. Run from research/: .venv/bin/python -m unittest test_event_study"""

import os
import tempfile
import unittest

import event_study as es

HEADER = ("timeframe,time_et,open,high,low,close,volume,vwap,ema_fast,ema_slow,sma,bb_upper,"
          "bb_lower,bb_pct,rsi,stoch_k,stoch_d,atr,structure,er,ema_mid,ema_long,b1,b2\n")
EMPTY = "," * 15  # vwap .. ema_long


def one_second(hhmmss, close):
    return f"1s,{hhmmss},{close},{close},{close},{close},0{EMPTY},,\n"


def fifteen(hhmmss, open_, close, b1="0", b2="0"):
    return f"15s,{hhmmss},{open_},{open_},{close},{close},0{EMPTY},{b1},{b2}\n"


def chart(rows):
    d = tempfile.mkdtemp()
    path = os.path.join(d, "chart.csv")
    with open(path, "w") as f:
        f.write(HEADER + "".join(rows))
    return path


class Mids(unittest.TestCase):
    # The mid at t is the close of the 1 s bar that ends at t (the mid just before t).
    def test_mid_at_a_second(self):
        day = es.read_day(chart([one_second("10:00:00", "100.00"), one_second("10:00:01", "100.02")]))
        t = 10 * 3600
        self.assertAlmostEqual(day.mid_at(t + 1), 100.00)
        self.assertAlmostEqual(day.mid_at(t + 2), 100.02)
        self.assertIsNone(day.mid_at(t + 3))  # no bar ends there


class Events(unittest.TestCase):
    # A read is an event when its 15 s bar ends (when it was known), in its direction; the
    # momentum baseline is every 15 s bar's own direction (a flat bar has none).
    def test_reads_and_momentum(self):
        day = es.read_day(chart([fifteen("10:00:00", "100.00", "100.05", b1="-1"),
                                 fifteen("10:00:15", "100.05", "100.05", b2="1")]))
        t = 10 * 3600 + 15
        self.assertEqual(day.events["B1 trap"], [(t, -1)])
        self.assertEqual(day.events["B2 pullback end"], [(t + 15, 1)])
        self.assertEqual(day.events["15s momentum"], [(t, 1)])  # the flat bar has none


class SignedMoves(unittest.TestCase):
    # B1 says down at 10:00:01 (mid 100.00); by 10:00:04 the mid is 99.97, the close of the
    # bar ending then: +3 c the read's way. Horizons past the last bar are missing, not zero.
    def test_cents_the_reads_way(self):
        rows = [one_second("10:00:00", "100.00"), one_second("10:00:01", "100.00"),
                one_second("10:00:02", "99.99"), one_second("10:00:03", "99.97")]
        day = es.read_day(chart(rows))
        t = 10 * 3600 + 1
        moves = es.signed_moves(day, [(t, -1)], horizons=(3, 10))
        self.assertAlmostEqual(moves[0][3], 3.0)
        self.assertIsNone(moves[0][10])


class Cost(unittest.TestCase):
    # A taker pays half the spread in and half out, plus the take fee each way. The half
    # spread is read off the naive fills (they rest at the touch): |price - mid|.
    def test_round_trip_from_the_fills(self):
        d = tempfile.mkdtemp()
        path = os.path.join(d, "f.csv")
        with open(path, "w") as f:
            f.write("timestamp_ns,side,price,quantity,mid_x2_before\n"
                    "1,B,2760000,100,5520100\n"   # half spread 50 ticks = 0.5 c
                    "2,S,2760100,100,5520100\n"   # 0.5 c
                    "3,B,2760000,100,5520200\n")  # 1 c
        self.assertAlmostEqual(es.median_half_spread_cents([path]), 0.5)
        self.assertAlmostEqual(es.round_trip_cents(0.5, fee_cents=0.3), 1.6)


class BlockMean(unittest.TestCase):
    # Mean of every event's move, with an interval from resampling 5 min blocks within each
    # day. Equal values: the interval is the mean itself.
    def test_constant_values(self):
        days = [[(36_000, 2.0), (36_400, 2.0)], [(40_000, 2.0)]]
        mean, low, high = es.block_mean_ci(days, resamples=200, seed=1)
        self.assertEqual((mean, low, high), (2.0, 2.0, 2.0))

    def test_none_without_events(self):
        self.assertEqual(es.block_mean_ci([[], []]), (None, None, None))


if __name__ == "__main__":
    unittest.main()
