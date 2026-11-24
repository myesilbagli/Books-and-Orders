"""Tests for fill_anatomy.py. Run from research/: ../research/.venv/bin/python -m unittest test_fill_anatomy"""

import os
import tempfile
import unittest

import bootstrap as bs
import fill_anatomy as fa
import signal_study as ss

S = 1_000_000_000  # one second in ns
BAR = 60 * S       # 1 min bars

# A low made in the bar at 100 s, confirmed at 300 s; a high made at 400 s, confirmed at
# 600 s; a low made at 700 s, confirmed at 900 s.
SWINGS = [fa.Swing("L", 100 * S, 300 * S), fa.Swing("H", 400 * S, 600 * S),
          fa.Swing("L", 700 * S, 900 * S)]


class OracleLeg(unittest.TestCase):
    # With hindsight, every moment between two swings is on a known leg: low -> high is up.
    def test_up_then_down(self):
        self.assertEqual(fa.oracle_leg(SWINGS, 200 * S, BAR), 1)
        self.assertEqual(fa.oracle_leg(SWINGS, 500 * S, BAR), -1)

    # Inside the bar that made a swing we can't tell whether the fill came before or after
    # the extreme, so it belongs to neither leg.
    def test_turning_bar_is_left_out(self):
        self.assertIsNone(fa.oracle_leg(SWINGS, 130 * S, BAR))
        self.assertIsNone(fa.oracle_leg(SWINGS, 459 * S, BAR))
        self.assertEqual(fa.oracle_leg(SWINGS, 460 * S, BAR), -1)

    # Before the first swing, and after the last (its leg never finished): unknown.
    def test_outside_the_swings(self):
        self.assertIsNone(fa.oracle_leg(SWINGS, 50 * S, BAR))
        self.assertIsNone(fa.oracle_leg(SWINGS, 800 * S, BAR))


class ConfirmedLeg(unittest.TestCase):
    # In real time a swing counts only once confirmed; the leg runs from the last one known.
    def test_known_only_once_confirmed(self):
        self.assertIsNone(fa.confirmed_leg(SWINGS, 250 * S))    # nothing confirmed yet
        self.assertEqual(fa.confirmed_leg(SWINGS, 300 * S), 1)  # the low is known: up leg
        self.assertEqual(fa.confirmed_leg(SWINGS, 599 * S), 1)  # the high was made, not known
        self.assertEqual(fa.confirmed_leg(SWINGS, 650 * S), -1)
        self.assertEqual(fa.confirmed_leg(SWINGS, 950 * S), 1)  # after the last: still known


class ReadSwings(unittest.TestCase):
    # chart_dump --swings: one timeframe kept, clock times turned into ns since midnight.
    def test_reads_one_timeframe(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "swings.csv")
            with open(path, "w") as f:
                f.write("timeframe,kind,price,time_et,confirmed_et,change\n"
                        "1s,H,270.69,09:00:00,09:00:14,\n"
                        "1m,L,270.39,09:06:00,09:15:00,\n"
                        "1m,H,272.56,09:28:00,09:30:00,1\n")
            swings = fa.read_swings(path, "1m")
        self.assertEqual(swings, [fa.Swing("L", (9 * 3600 + 6 * 60) * S, (9 * 3600 + 15 * 60) * S),
                                  fa.Swing("H", (9 * 3600 + 28 * 60) * S, (9 * 3600 + 30 * 60) * S)])


def row(t_s, side, mid_after, shares_left=10, **indicators):
    # Bought or sold 100 at 1.0000 (10'000 ticks); mid 1.0000 before, mid_after 1 s and 10 s later.
    fill = bs.Fill(timestamp_ns=t_s * S, side=side, price=10_000, quantity=100,
                   mid_x2_before=20_000, mid_x2_1s=2 * mid_after, mid_x2_10s=2 * mid_after,
                   mid_x2_60s=2 * mid_after)
    ind = {"level_broke": shares_left == 0, "oracle_leg": None, "confirmed_leg": None,
           "structure_1m": None, "er_1m": None}
    ind.update(indicators)
    return ss.Row(fill=fill, indicators=ind)


class Signals(unittest.TestCase):
    # Q1 flags the fills where the level broke; Q2 flags a buy on a down leg.
    def test_level_broke_and_against_the_leg(self):
        broke = next(s for s in fa.SIGNALS if s.name == "level broke")
        oracle = next(s for s in fa.SIGNALS if s.name == "oracle leg")
        self.assertTrue(ss._is_flagged(broke, row(1, "B", 10_000, shares_left=0)))
        self.assertFalse(ss._is_flagged(broke, row(1, "B", 10_000, shares_left=5)))
        self.assertTrue(ss._is_flagged(oracle, row(1, "B", 10_000, oracle_leg=-1)))
        self.assertFalse(ss._is_flagged(oracle, row(1, "S", 10_000, oracle_leg=-1)))
        self.assertIsNone(ss._is_flagged(oracle, row(1, "B", 10_000)))

    # Q5: only a +-2 structure with ER at or above 0.6 counts as a (strong) trend.
    def test_strong_trend_needs_both(self):
        strong = next(s for s in fa.SIGNALS if s.name == "strong trend")
        self.assertTrue(ss._is_flagged(strong, row(1, "B", 10_000, structure_1m=-2, er_1m=0.6)))
        self.assertFalse(ss._is_flagged(strong, row(1, "B", 10_000, structure_1m=-2, er_1m=0.5)))
        self.assertFalse(ss._is_flagged(strong, row(1, "B", 10_000, structure_1m=-1, er_1m=0.9)))
        self.assertIsNone(ss._is_flagged(strong, row(1, "B", 10_000, structure_1m=-2)))


class CrossTable(unittest.TestCase):
    # Shares and cents a share in each cell: level broke or held x against or with the leg.
    def test_cells(self):
        day = [row(1, "B", 9_990, shares_left=0, oracle_leg=-1),   # broke, against: -0.10 ¢
               row(2, "S", 9_995, shares_left=5, oracle_leg=-1),   # held, with: +0.05
               row(3, "B", 10_000, shares_left=5, oracle_leg=None)]  # no leg: left out
        table = fa.cross_table([day], horizon=10)
        self.assertEqual(table[("broke", "against")], (100, -0.10))
        self.assertEqual(table[("held", "with")], (100, 0.05))
        self.assertEqual(table[("broke", "with")], (0, None))
        self.assertEqual(table[("held", "against")], (0, None))


if __name__ == "__main__":
    unittest.main()
