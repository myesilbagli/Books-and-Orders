"""Tests for dbn_to_itch.py. Run: research/.venv/bin/python -m unittest discover -s research"""

import struct
import unittest
from datetime import datetime, timezone

import dbn_to_itch as dx

NS = 1_000_000_000
# 2019-12-30 09:30:00 ET = 14:30 UTC (winter, UTC-5)
T_0930 = int(datetime(2019, 12, 30, 14, 30, tzinfo=timezone.utc).timestamp()) * NS
P = 1_000_000_000  # MBO prices are fixed point, 1e-9 dollars


def rec(seq, action, side, price_dollars, size, order_id, t=T_0930):
    return dx.Mbo(sequence=seq, ts_event=t, action=action, side=side,
                  price=round(price_dollars * P), size=size, order_id=order_id)


def messages(blob):
    """Split the output into ITCH messages (each after a 2-byte big-endian length)."""
    out, pos = [], 0
    while pos < len(blob):
        n = struct.unpack(">H", blob[pos:pos + 2])[0]
        out.append(blob[pos + 2:pos + 2 + n])
        pos += 2 + n
    return out


def header(m):
    """(type, locate, timestamp ns since midnight ET)."""
    return chr(m[0]), struct.unpack(">H", m[1:3])[0], int.from_bytes(m[5:11], "big")


def convert(records):
    blob, stats = dx.convert(records, "AAPL", locate=7)
    return messages(blob)[1:], stats  # [0] is the stock directory


class EasternTime(unittest.TestCase):
    # ITCH time is nanoseconds since midnight New York time, summer time included.
    def test_winter_and_summer(self):
        winter = int(datetime(2019, 12, 30, 14, 30, tzinfo=timezone.utc).timestamp()) * NS + 5
        summer = int(datetime(2025, 7, 1, 13, 30, tzinfo=timezone.utc).timestamp()) * NS
        self.assertEqual(dx.eastern_ns(winter), (9 * 3600 + 30 * 60) * NS + 5)
        self.assertEqual(dx.eastern_ns(summer), (9 * 3600 + 30 * 60) * NS)


class StockDirectory(unittest.TestCase):
    # The tools find a stock by name in its R message, which must come first.
    def test_first_message_names_the_stock(self):
        blob, _ = dx.convert([], "AAPL", locate=7)
        r = messages(blob)[0]
        self.assertEqual(len(r), 39)
        self.assertEqual(header(r)[:2], ("R", 7))
        self.assertEqual(r[11:19], b"AAPL    ")


class Add(unittest.TestCase):
    # MBO side A (ask) is an ITCH sell; prices go from 1e-9 to ITCH's 4 decimals.
    def test_add_order(self):
        (m,), _ = convert([rec(1, "A", "A", 290.12, 100, 55)])
        self.assertEqual(len(m), 36)
        self.assertEqual(header(m), ("A", 7, (9 * 3600 + 30 * 60) * NS))
        ref, side, shares, stock, price = struct.unpack(">QcI8sI", m[11:36])
        self.assertEqual((ref, side, shares, stock, price), (55, b"S", 100, b"AAPL    ", 2_901_200))


class Cancel(unittest.TestCase):
    # A cancel of part of an order is an X; of all that is left, a D.
    def test_partial_then_full(self):
        msgs, _ = convert([rec(1, "A", "B", 290, 100, 55), rec(2, "C", "B", 290, 30, 55),
                           rec(3, "C", "B", 290, 70, 55)])
        x, d = msgs[1], msgs[2]
        self.assertEqual((chr(x[0]), len(x)), ("X", 23))
        self.assertEqual(struct.unpack(">QI", x[11:23]), (55, 30))
        self.assertEqual((chr(d[0]), len(d)), ("D", 19))
        self.assertEqual(struct.unpack(">Q", d[11:19])[0], 55)


class Replace(unittest.TestCase):
    # One message that cancels an order and adds a new one is an ITCH U (replace), not a
    # D and an A: two messages would show a book without the order in between.
    def test_cancel_and_add_in_one_message_is_a_replace(self):
        msgs, _ = convert([rec(1, "A", "A", 289.76, 100, 55), rec(2, "C", "A", 289.76, 100, 55),
                           rec(2, "A", "A", 289.75, 100, 56), rec(3, "C", "A", 289.75, 100, 56)])
        u = msgs[1]
        self.assertEqual((chr(u[0]), len(u)), ("U", 35))
        self.assertEqual(struct.unpack(">QQII", u[11:35]), (55, 56, 100, 2_897_500))
        self.assertEqual(chr(msgs[2][0]), "D")  # the new order is known by its new number


class Execution(unittest.TestCase):
    # Trade + fill + the shares leaving the book: one ITCH E at the order's own price.
    def test_at_the_order_price_is_an_e(self):
        msgs, _ = convert([rec(1, "A", "A", 290.12, 100, 55),
                           rec(2, "T", "B", 290.12, 22, 0), rec(2, "F", "A", 290.12, 22, 55),
                           rec(2, "C", "A", 290.12, 22, 55),
                           rec(3, "C", "A", 290.12, 78, 55)])
        e = msgs[1]
        self.assertEqual((chr(e[0]), len(e)), ("E", 31))
        self.assertEqual(struct.unpack(">QI", e[11:23]), (55, 22))
        self.assertEqual(chr(msgs[2][0]), "D")  # 78 left, then cancelled: all of it

    # At another price (a cross, for example): an ITCH C with its price, printable since
    # a trade was reported.
    def test_at_another_price_is_a_printable_c(self):
        msgs, _ = convert([rec(1, "A", "B", 290.00, 100, 55),
                           rec(2, "T", "A", 290.05, 40, 0), rec(2, "F", "B", 290.05, 40, 55),
                           rec(2, "C", "B", 290.05, 40, 55)])
        c = msgs[1]
        self.assertEqual((chr(c[0]), len(c)), ("C", 36))
        ref, shares, match, printable, price = struct.unpack(">QIQcI", c[11:36])
        self.assertEqual((ref, shares, printable, price), (55, 40, b"Y", 2_900_500))

    # A fill with no trade record was not printed: a non-printable C.
    def test_without_a_trade_record_is_not_printable(self):
        msgs, _ = convert([rec(1, "A", "B", 290.00, 100, 55),
                           rec(2, "F", "B", 290.00, 40, 55), rec(2, "C", "B", 290.00, 40, 55)])
        self.assertEqual(chr(msgs[1][0]), "C")
        self.assertEqual(msgs[1][31:32], b"N")


class HiddenTrade(unittest.TestCase):
    # A trade with no side and no resting order is a hidden (non-displayed) execution: P.
    def test_trade_alone_is_a_p(self):
        msgs, _ = convert([rec(1, "T", "N", 290.07, 300, 0)])
        p = msgs[0]
        self.assertEqual((chr(p[0]), len(p)), ("P", 44))
        ref, side, shares, stock, price, match = struct.unpack(">QcI8sIQ", p[11:44])
        self.assertEqual((side, shares, stock, price), (b"B", 300, b"AAPL    ", 2_900_700))


class Unknown(unittest.TestCase):
    # Anything else is counted, never guessed at.
    def test_unknown_patterns_are_counted(self):
        msgs, stats = convert([rec(1, "M", "B", 290, 100, 55)])
        self.assertEqual(msgs, [])
        self.assertEqual(stats["skipped M"], 1)


if __name__ == "__main__":
    unittest.main()
