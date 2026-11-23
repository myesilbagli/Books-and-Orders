"""Tests for hub_export.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import json
import os
import tempfile
import unittest

import hub_export as hx

CHART_HEADER = ("timeframe,time_et,open,high,low,close,volume,vwap,ema_fast,ema_slow,sma,"
                "bb_upper,bb_lower,bb_pct,rsi,stoch_k,stoch_d\n")
FILLS_HEADER = ("timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
                "touch_share,shares_left,"
                "ema_fast_1s,ema_slow_1s,sma_1s,bb_pct_1s,rsi_1s,stoch_k_1s,stoch_d_1s,"
                "ema_fast_1m,ema_slow_1m,sma_1m,bb_pct_1m,rsi_1m,stoch_k_1m,stoch_d_1m,vwap\n")
SECOND = 1_000_000_000
MIDNIGHT = 1577664000  # 2019-12-30 00:00 as UTC seconds


def write(directory, name, text):
    path = os.path.join(directory, name)
    with open(path, "w") as f:
        f.write(text)
    return path


class ChartTime(unittest.TestCase):
    # The chart library shows UTC; Eastern wall-clock time stored as if it were UTC makes
    # the axis read 09:30 at 09:30 ET, with no time-zone code in the page.
    def test_wall_clock_as_utc(self):
        self.assertEqual(hx.day_start("2019-12-30"), MIDNIGHT)
        self.assertEqual(hx.chart_time(MIDNIGHT, "09:30:00"), MIDNIGHT + 9 * 3600 + 30 * 60)


class ReadChart(unittest.TestCase):
    def test_splits_timeframes_and_keeps_empty_fields_empty(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "chart.csv", CHART_HEADER +
                         "1s,09:00:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,\n"
                         "1m,09:00:00,287.0000,287.1000,286.9000,287.0500,900,"
                         "287.0200,287.0300,287.0400,287.0100,287.2000,286.8000,0.625,55.50,12.00,20.00\n")
            chart = hx.read_chart(path, MIDNIGHT)
        self.assertEqual(len(chart["1s"]), 1)
        self.assertEqual(len(chart["1m"]), 1)
        s = chart["1s"][0]
        self.assertEqual(s["time"], MIDNIGHT + 9 * 3600)
        self.assertEqual((s["open"], s["high"], s["low"], s["close"]), (287.0, 287.01, 286.99, 287.005))
        self.assertEqual(s["volume"], 300)
        self.assertIsNone(s["ema_fast"])  # warming up: null, never 0
        self.assertIsNone(s["vwap"])
        m = chart["1m"][0]
        self.assertEqual(m["bb_pct"], 0.625)
        self.assertEqual((m["stoch_k"], m["stoch_d"]), (12.0, 20.0))


class ReadFills(unittest.TestCase):
    # Bought 100 at 1.0000 (10'000 ticks). Mid 1.0005 before; 0.9990 1 s later, 1.0010 at
    # 10 s, 1.0000 at 60 s. Markouts: -10, +10, 0 ticks a share = -0.1, +0.1, 0 cents.
    def test_markouts_in_cents_a_share_and_chart_time(self):
        ts = (9 * 3600 + 45 * 60) * SECOND + SECOND // 4  # 09:45:00.25
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "fills.csv", FILLS_HEADER +
                         f"{ts},B,10000,100,20010,19980,20020,20000,0.8,340,"
                         "10004.12,,,,,,,,,,,,,,10002.25\n")
            fills = hx.read_fills(path, MIDNIGHT)
        self.assertEqual(len(fills), 1)
        f = fills[0]
        self.assertAlmostEqual(f["time"], MIDNIGHT + 9 * 3600 + 45 * 60 + 0.25)
        self.assertEqual(f["side"], "B")
        self.assertEqual(f["price"], 1.0)  # dollars
        self.assertEqual(f["quantity"], 100)
        self.assertAlmostEqual(f["markout_1s"], -0.1)
        self.assertAlmostEqual(f["markout_10s"], 0.1)
        self.assertAlmostEqual(f["markout_60s"], 0.0)
        self.assertEqual((f["touch_share"], f["shares_left"]), (0.8, 340))

    # A sell is the mirror: sold at 1.0010, mid 1.0000 at 1 s -> +10 ticks = +0.1 cents.
    def test_a_sell_earns_when_the_mid_falls(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "fills.csv", FILLS_HEADER +
                         f"{SECOND},S,10010,100,20010,20000,20020,20020,0.5,0" + "," * 15 + "\n")
            f = hx.read_fills(path, MIDNIGHT)[0]
        self.assertAlmostEqual(f["markout_1s"], 0.1)
        self.assertAlmostEqual(f["markout_10s"], 0.0)  # mid back at our price


class Export(unittest.TestCase):
    # One folder per day, and an index that lists every day and its runs; a second export
    # of another run adds to the index instead of replacing it.
    def test_writes_the_day_and_merges_the_index(self):
        with tempfile.TemporaryDirectory() as d:
            chart = write(d, "chart.csv", CHART_HEADER +
                          "1s,09:00:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,\n")
            fills = write(d, "fills.csv", FILLS_HEADER)
            out = os.path.join(d, "hub")
            hx.export("2019-12-30", "AAPL", chart, {"naive_0us": fills}, out)
            hx.export("2019-12-30", "AAPL", chart, {"as10_0us": fills}, out)
            with open(os.path.join(out, "index.json")) as f:
                index = json.load(f)
            day = os.path.join(out, "2019-12-30_AAPL")
            self.assertTrue(os.path.exists(os.path.join(day, "chart_1s.json")))
            self.assertTrue(os.path.exists(os.path.join(day, "chart_1m.json")))
            self.assertTrue(os.path.exists(os.path.join(day, "fills_naive_0us.json")))
        self.assertEqual(index, {"days": [{"id": "2019-12-30_AAPL", "date": "2019-12-30",
                                           "symbol": "AAPL", "runs": ["as10_0us", "naive_0us"]}]})


if __name__ == "__main__":
    unittest.main()
