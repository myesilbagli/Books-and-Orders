"""Tests for pick_days.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import unittest
from datetime import date

import pick_days as pd


class TradingDays(unittest.TestCase):
    def test_weekends_and_holidays_are_out(self):
        days = pd.trading_days(date(2026, 1, 1), date(2026, 1, 20))
        self.assertNotIn(date(2026, 1, 1), days)   # New Year's Day
        self.assertNotIn(date(2026, 1, 3), days)   # a Saturday
        self.assertNotIn(date(2026, 1, 19), days)  # Martin Luther King Jr. Day
        self.assertEqual(days[0], date(2026, 1, 2))
        self.assertEqual(len(days), 12)

    def test_holidays_of_2026(self):
        days = set(pd.trading_days(date(2026, 1, 1), date(2026, 12, 31)))
        for holiday in (date(2026, 4, 3), date(2026, 5, 25), date(2026, 6, 19), date(2026, 7, 3),
                        date(2026, 9, 7), date(2026, 11, 26), date(2026, 12, 25)):
            self.assertNotIn(holiday, days)
        self.assertNotIn(date(2026, 11, 27), days)  # half day: left out, quoting runs to 16:00


class Draw(unittest.TestCase):
    # The same seed draws the same days, in date order, without repeats.
    def test_reproducible(self):
        pool = pd.trading_days(date(2026, 1, 1), date(2026, 9, 30))
        a = pd.draw(pool, 10, seed=7)
        self.assertEqual(a, pd.draw(pool, 10, seed=7))
        self.assertEqual(a, sorted(a))
        self.assertEqual(len(set(a)), 10)
        self.assertNotEqual(a, pd.draw(pool, 10, seed=8))

    # The hold-out: the last n trading days up to a date.
    def test_last_days(self):
        self.assertEqual(pd.last_days(date(2026, 11, 20), 3),
                         [date(2026, 11, 18), date(2026, 11, 19), date(2026, 11, 20)])


if __name__ == "__main__":
    unittest.main()
