"""Tests for hub_export.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import json
import os
import tempfile
import unittest

import hub_export as hx

CHART_HEADER = ("timeframe,time_et,open,high,low,close,volume,vwap,ema_fast,ema_slow,sma,"
                "bb_upper,bb_lower,bb_pct,rsi,stoch_k,stoch_d,atr,structure\n")
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
                         "1s,09:00:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,,,\n"
                         "1m,09:00:00,287.0000,287.1000,286.9000,287.0500,900,"
                         "287.0200,287.0300,287.0400,287.0100,287.2000,286.8000,0.625,55.50,12.00,20.00,"
                         "0.1500,-2\n"
                         "15s,09:00:00,287.0000,287.0100,286.9900,287.0050,0,,,,,,,,,,,,\n")
            chart = hx.read_chart(path, MIDNIGHT)
        self.assertEqual(sorted(chart), ["15s", "1m", "1s"])  # every timeframe in the file
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
        self.assertEqual(m["atr"], 0.15)
        self.assertEqual(m["structure"], -2)  # an integer: -2, -1, 0, 1, 2
        self.assertIsNone(s["structure"])

    # The efficiency ratio (D57) is the last column; a chart written before it reads as null.
    def test_reads_the_efficiency_ratio(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "chart.csv", CHART_HEADER.rstrip("\n") + ",er\n" +
                         "1m,09:00:00,287.0000,287.1000,286.9000,287.0500,900,"
                         ",,,,,,,,,,,,0.457\n"
                         "1m,09:01:00,287.0000,287.1000,286.9000,287.0500,900,,,,,,,,,,,,\n")
            chart = hx.read_chart(path, MIDNIGHT)
            old = write(d, "old.csv", CHART_HEADER +
                        "1m,09:00:00,287.0000,287.1000,286.9000,287.0500,900,,,,,,,,,,,,\n")
            before = hx.read_chart(old, MIDNIGHT)
        self.assertEqual([b["er"] for b in chart["1m"]], [0.457, None])  # empty: warming up
        self.assertIsNone(before["1m"][0]["er"])


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


SWINGS_HEADER = "timeframe,kind,price,time_et,confirmed_et,change\n"


class ReadSwings(unittest.TestCase):
    # Each swing gets the label a chart reader would write next to it.
    def test_labels_and_times(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "swings.csv", SWINGS_HEADER +
                         "1m,L,287.0000,09:40:00,09:42:00,\n"
                         "1m,H,287.5000,09:43:00,09:45:00,\n"
                         "1m,L,287.2000,09:46:00,09:47:00,1\n"
                         "1m,H,287.4000,09:48:00,09:50:00,-1\n"
                         "15s,L,287.1000,09:40:15,09:40:45,0\n")
            swings = hx.read_swings(path, MIDNIGHT)
        labels = [s["label"] for s in swings["1m"]]
        self.assertEqual(labels, ["L", "H", "HL", "LH"])
        self.assertEqual(swings["15s"][0]["label"], "EL")  # equal to the last low
        first = swings["1m"][0]
        self.assertEqual(first["time"], MIDNIGHT + 9 * 3600 + 40 * 60)       # made
        self.assertEqual(first["confirmed"], MIDNIGHT + 9 * 3600 + 42 * 60)  # known
        self.assertEqual((first["price"], first["kind"]), (287.0, "L"))


class ScoreChanges(unittest.TestCase):
    # Only the bars where the score changes: the hub fills in the bars between.
    def test_keeps_only_changes(self):
        bars = [{"time": t, "structure": s} for t, s in
                [(1, None), (2, None), (3, 2), (4, 2), (5, 0), (6, 0), (7, -2)]]
        self.assertEqual(hx.score_changes(bars), [{"time": 3, "score": 2}, {"time": 5, "score": 0},
                                                  {"time": 7, "score": -2}])


class Export(unittest.TestCase):
    # One folder per day, and an index that lists every day and its runs; a second export
    # of another run adds to the index instead of replacing it.
    def test_writes_the_day_and_merges_the_index(self):
        with tempfile.TemporaryDirectory() as d:
            chart = write(d, "chart.csv", CHART_HEADER +
                          "1s,09:00:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,,,\n"
                          "5m,09:00:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,,,\n")
            fills = write(d, "fills.csv", FILLS_HEADER)
            out = os.path.join(d, "hub")
            hx.export("2019-12-30", "AAPL", chart, {"naive_0us": fills}, out)
            hx.export("2019-12-30", "AAPL", chart, {"as10_0us": fills}, out)
            with open(os.path.join(out, "index.json")) as f:
                index = json.load(f)
            day = os.path.join(out, "2019-12-30_AAPL")
            self.assertTrue(os.path.exists(os.path.join(day, "chart_1s.json")))
            self.assertTrue(os.path.exists(os.path.join(day, "chart_5m.json")))
            self.assertTrue(os.path.exists(os.path.join(day, "fills_naive_0us.json")))
        self.assertEqual(index, {"days": [{"id": "2019-12-30_AAPL", "date": "2019-12-30",
                                           "symbol": "AAPL", "runs": ["as10_0us", "naive_0us"],
                                           "structure_c": []}]})

    # One structure file per timeframe and c: the swings and where the score changed.
    def test_writes_the_structure_for_each_c(self):
        with tempfile.TemporaryDirectory() as d:
            chart = write(d, "chart.csv", CHART_HEADER +
                          "1m,09:30:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,,0.1000,\n"
                          "1m,09:31:00,287.0000,287.0100,286.9900,287.0050,300,,,,,,,,,,,0.1000,2\n")
            swings = write(d, "swings.csv", SWINGS_HEADER + "1m,L,287.0000,09:30:00,09:31:00,\n")
            out = os.path.join(d, "hub")
            hx.export("2019-12-30", "AAPL", chart, {}, out, structures={"1.5": (chart, swings)})
            with open(os.path.join(out, "2019-12-30_AAPL", "structure_1m_c1.5.json")) as f:
                structure = json.load(f)
            with open(os.path.join(out, "index.json")) as f:
                index = json.load(f)
        self.assertEqual(structure["scores"], [{"time": MIDNIGHT + 9 * 3600 + 31 * 60, "score": 2}])
        self.assertEqual(len(structure["swings"]), 1)
        self.assertEqual(index["days"][0]["structure_c"], ["1.5"])


if __name__ == "__main__":
    unittest.main()
