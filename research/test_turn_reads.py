"""Tests for turn_reads.py. Run from research/: .venv/bin/python -m unittest test_turn_reads"""

import os
import tempfile
import unittest

import bootstrap as bs
import signal_study as ss
import turn_reads as tr

CURVE = [(0, -0.60), (30, -0.30), (60, -0.20), (120, 0.05), (300, 0.40)]


class EquivalentLag(unittest.TestCase):
    # Where a read's gap sits on the delayed-oracle curve, by straight lines between points.
    def test_between_points(self):
        self.assertAlmostEqual(tr.equivalent_lag(-0.45, CURVE), 15.0)
        self.assertAlmostEqual(tr.equivalent_lag(-0.25, CURVE), 45.0)

    # As good as the oracle or better: 0 s. Worth nothing (the curve has reached 0): None.
    def test_ends(self):
        self.assertEqual(tr.equivalent_lag(-0.70, CURVE), 0.0)
        self.assertIsNone(tr.equivalent_lag(0.02, CURVE))
        self.assertIsNone(tr.equivalent_lag(0.30, CURVE))  # past the crossing: not read back

    # Between the last negative point and the crossing: up to where the curve hits 0.
    def test_up_to_the_crossing(self):
        self.assertAlmostEqual(tr.equivalent_lag(-0.10, CURVE), 60 + 60 * 0.10 / 0.25)


def row(side, qty, **ind):
    fill = bs.Fill(timestamp_ns=0, side=side, price=10_000, quantity=qty, mid_x2_before=20_000,
                   mid_x2_1s=20_000, mid_x2_10s=20_000, mid_x2_60s=20_000)
    return ss.Row(fill=fill, indicators=ind)


class Coverage(unittest.TestCase):
    # Share of shares the read gives a direction to, and how often that direction is the
    # hindsight leg's where both have one.
    def test_coverage_and_agreement(self):
        reading = tr.column_reading("r")
        day = [row("B", 100, r=1, oracle_leg=1),     # agrees
               row("S", 100, r=-1, oracle_leg=1),    # disagrees
               row("B", 200, r=0, oracle_leg=1),     # no direction
               row("B", 100, r=1, oracle_leg=None)]  # no leg to compare
        cov, agree = tr.coverage_and_agreement([day], reading)
        self.assertAlmostEqual(cov, 0.6)    # 300 of 500 shares have a direction
        self.assertAlmostEqual(agree, 0.5)  # 100 of the 200 compared agree


HOUR_NS = 3600 * 1_000_000_000


def timed(t_ns):
    fill = bs.Fill(timestamp_ns=t_ns, side="B", price=10_000, quantity=100,
                   mid_x2_before=20_000, mid_x2_1s=20_000, mid_x2_10s=20_000, mid_x2_60s=20_000)
    return ss.Row(fill=fill, indicators={})


class Sessions(unittest.TestCase):
    # The founder's session map: open 9:30-10:30, late morning to 12, lunch to 14, power to 16.
    def test_the_founders_sessions(self):
        self.assertEqual([name for name, _, _ in tr.SESSIONS],
                         ["open", "late morning", "lunch", "power"])
        self.assertEqual(tr.SESSIONS[0][1:], (int(9.5 * HOUR_NS), int(10.5 * HOUR_NS)))
        self.assertEqual(tr.SESSIONS[-1][1:], (14 * HOUR_NS, 16 * HOUR_NS))

    # Each day keeps only its fills from start (included) to end (not included).
    def test_in_session_cuts_every_day(self):
        days = [[timed(10 * HOUR_NS), timed(12 * HOUR_NS), timed(int(12.5 * HOUR_NS))],
                [timed(15 * HOUR_NS)]]
        cut = tr.in_session(days, 12 * HOUR_NS, 14 * HOUR_NS)
        self.assertEqual([[r.fill.timestamp_ns for r in d] for d in cut],
                         [[12 * HOUR_NS, int(12.5 * HOUR_NS)], []])


class TurnReadColumns(unittest.TestCase):
    # The fills CSV's b1 and b2 (D63) are read like the other columns; a CSV from before them
    # reads them as missing.
    def test_b1_b2_are_read(self):
        head = ("timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,"
                "mid_x2_60s,b1,b2\n")
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "f.csv")
            with open(path, "w") as f:
                f.write(head + "1,B,10000,100,20000,20000,20000,20000,-1,0\n")
            ind = ss.read_rows(path)[0].indicators
        self.assertEqual((ind["b1"], ind["b2"]), (-1, 0))
        self.assertIn("B1 trap", [name for name, _ in tr.READS])
        self.assertIn("B2 pullback end", [name for name, _ in tr.READS])


if __name__ == "__main__":
    unittest.main()
