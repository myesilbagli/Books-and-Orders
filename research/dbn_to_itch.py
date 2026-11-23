"""Databento MBO (XNAS.ITCH) back into Nasdaq ITCH 5.0 bytes, for one stock (D52).

    research/.venv/bin/python research/dbn_to_itch.py data/databento/AAPL_20191230_XNAS_mbo.dbn.zst \\
        AAPL data/itch/20191230.AAPL.dbn.ITCH50

Databento sells Nasdaq's order-by-order feed in its own MBO records, not as ITCH. This
turns them back into ITCH, so the whole C++ side (book, simulator, charts) reads any day
unchanged.

Every ITCH message became one or more MBO records with the same `sequence` number. Each
group is turned back into the one message it came from:

    A          -> A  add order
    C          -> D  delete (all that is left) or X (part of it)
    C, A       -> U  replace: a cancel and a new order in one message, never a D and an A,
                     which would show a book without the order in between
    T, F, C    -> E  execution at the order's price; C (printable) at another price
    F, C       -> C  non-printable execution (no trade was reported)
    T          -> P  hidden (non-displayed) execution, and the open and close crosses

Times: MBO is UTC; ITCH is nanoseconds since midnight New York time (summer time included).
Anything else is counted and left out, never guessed at; so are records from another New York
day than the first record's (ITCH time restarts at midnight), and Databento's snapshot records
(at UTC midnight it repeats the whole book; the live feed already built it).
"""

import argparse
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

NEW_YORK = ZoneInfo("America/New_York")
NS = 1_000_000_000
TO_ITCH_PRICE = 100_000  # MBO: 1e-9 dollars; ITCH: 1e-4 (4 decimals)
F_SNAPSHOT = 32  # Databento's flag for a record that repeats the book, not a feed event


@dataclass(frozen=True)
class Mbo:
    sequence: int  # the venue message it came from: shared by all its records
    ts_event: int  # ns since the epoch, UTC
    action: str    # A add, C cancel, T trade, F fill, (M modify, R clear: not in ITCH data)
    side: str      # B bid, A ask, N none
    price: int     # 1e-9 dollars
    size: int
    order_id: int
    flags: int = 0  # Databento flags; only F_SNAPSHOT is read here


def _new_york(ts_utc_ns):
    seconds, ns = divmod(ts_utc_ns, NS)
    return datetime.fromtimestamp(seconds, timezone.utc).astimezone(NEW_YORK), ns


def eastern_ns(ts_utc_ns):
    """ITCH time: nanoseconds since midnight in New York, on that day."""
    local, ns = _new_york(ts_utc_ns)
    return (local.hour * 3600 + local.minute * 60 + local.second) * NS + ns


def eastern_day(ts_utc_ns):
    """The New York date of a UTC time (not the UTC date: 23:30 ET is the next day in UTC)."""
    return _new_york(ts_utc_ns)[0].date()


def _price(mbo_price):
    itch, rest = divmod(mbo_price, TO_ITCH_PRICE)
    if rest:
        raise ValueError(f"price {mbo_price} has more than 4 decimals")
    return itch


class Writer:
    """ITCH 5.0 messages, each after a 2-byte big-endian length, as in Nasdaq's files."""

    def __init__(self, symbol, locate):
        self.stock = symbol.encode().ljust(8)
        self.locate = locate
        self.out = bytearray()
        self.match = 0
        self.counts = Counter()

    def _message(self, kind, t_ns, body):
        # Header: type, stock locate, tracking number (0), 6-byte timestamp.
        m = kind.encode() + struct.pack(">HH", self.locate, 0) + t_ns.to_bytes(6, "big") + body
        self.out += struct.pack(">H", len(m)) + m
        self.counts[kind] += 1

    def _next_match(self):
        self.match += 1  # unique match numbers; nothing reads them, but ITCH has them
        return self.match

    def directory(self):
        # R: stock, market category Q, normal, round lot 100, not round lots only, common
        # stock, no sub-type, live, short-sale threshold N, no IPO, LULD tier 1, not an ETP.
        self._message("R", 0, self.stock + b"QN" + struct.pack(">I", 100) + b"NC" + b"  "
                      + b"PN 1N" + struct.pack(">I", 0) + b"N")

    def add(self, t, ref, side, shares, price):
        self._message("A", t, struct.pack(">Qc", ref, side) + struct.pack(">I", shares)
                      + self.stock + struct.pack(">I", price))

    def delete(self, t, ref):
        self._message("D", t, struct.pack(">Q", ref))

    def cancel(self, t, ref, shares):
        self._message("X", t, struct.pack(">QI", ref, shares))

    def replace(self, t, old, new, shares, price):
        self._message("U", t, struct.pack(">QQII", old, new, shares, price))

    def executed(self, t, ref, shares):
        self._message("E", t, struct.pack(">QIQ", ref, shares, self._next_match()))

    def executed_with_price(self, t, ref, shares, printable, price):
        self._message("C", t, struct.pack(">QIQcI", ref, shares, self._next_match(), printable,
                                          price))

    def hidden_trade(self, t, shares, price):
        # P: order reference 0 and side B, as Nasdaq has sent them since 2014.
        self._message("P", t, struct.pack(">QcI", 0, b"B", shares) + self.stock
                      + struct.pack(">IQ", price, self._next_match()))


def _groups(records):
    """Consecutive records with the same sequence number: one venue message each."""
    group = []
    for r in records:
        if group and r.sequence != group[0].sequence:
            yield group
            group = []
        group.append(r)
    if group:
        yield group


def convert(records, symbol, locate=1):
    """(ITCH bytes, counts). records: Mbo in feed order."""
    w = Writer(symbol, locate)
    w.directory()
    orders = {}  # order id -> [ITCH price, shares left], to tell D from X and E from C
    stats = Counter()
    day = None  # the New York date of the first record: the file's one trading day

    def live(records):
        # Snapshot records are Databento's copy of a book the feed already built (at UTC
        # midnight, mid-file); ITCH has no such message.
        for r in records:
            if r.flags & F_SNAPSHOT:
                stats["skipped: snapshot record"] += 1
            else:
                yield r

    for g in _groups(live(records)):
        # ITCH time restarts at midnight, so another day's records would mix into this one.
        if day is None:
            day = eastern_day(g[0].ts_event)
        elif eastern_day(g[0].ts_event) != day:
            stats["skipped: another New York day"] += 1
            continue
        pattern = "".join(r.action for r in g)
        t = eastern_ns(g[0].ts_event)
        if pattern == "A":
            a = g[0]
            side = b"B" if a.side == "B" else b"S"
            w.add(t, a.order_id, side, a.size, _price(a.price))
            orders[a.order_id] = [_price(a.price), a.size]
        elif pattern == "C":
            c = g[0]
            order = orders.get(c.order_id)
            if order is None:
                stats["cancel of an unknown order"] += 1
            if order is None or c.size >= order[1]:
                w.delete(t, c.order_id)
                orders.pop(c.order_id, None)
            else:
                w.cancel(t, c.order_id, c.size)
                order[1] -= c.size
        elif pattern == "CA":
            c, a = g
            w.replace(t, c.order_id, a.order_id, a.size, _price(a.price))
            orders.pop(c.order_id, None)
            orders[a.order_id] = [_price(a.price), a.size]
        elif pattern in ("TFC", "FC"):
            f, c = g[-2], g[-1]
            if c.size != f.size or c.order_id != f.order_id:
                stats["execution whose cancel doesn't match its fill"] += 1
            order = orders.get(f.order_id)
            if order is None:
                stats["execution of an unknown order"] += 1
            price = _price(f.price)
            if pattern == "TFC" and order is not None and price == order[0]:
                w.executed(t, f.order_id, f.size)
            else:
                # A printed trade at another price, or one that wasn't printed at all.
                w.executed_with_price(t, f.order_id, f.size, b"Y" if pattern == "TFC" else b"N",
                                      price)
            if order is not None:
                order[1] -= f.size
                if order[1] <= 0:
                    del orders[f.order_id]
        elif pattern == "T":
            w.hidden_trade(t, g[0].size, _price(g[0].price))
        else:
            stats[f"skipped {pattern}"] += 1

    stats.update({f"ITCH {k}": v for k, v in w.counts.items()})
    return bytes(w.out), stats


def read_dbn(path):
    """The MBO records of a Databento file, in feed order (R, the book clear, left out: the
    file starts at an empty book, and the clear before a snapshot is not a feed event)."""
    import databento as db  # only here: the tests don't need the package

    df = db.DBNStore.from_file(path).to_df(price_type="fixed", pretty_ts=False,
                                           map_symbols=False)
    df = df[df["action"] != "R"]
    for seq, t, action, side, price, size, ref, flags in zip(
            df["sequence"].to_numpy(), df["ts_event"].to_numpy(), df["action"].to_numpy(),
            df["side"].to_numpy(), df["price"].to_numpy(), df["size"].to_numpy(),
            df["order_id"].to_numpy(), df["flags"].map(int).to_numpy()):
        yield Mbo(sequence=int(seq), ts_event=int(t), action=action, side=side, price=int(price),
                  size=int(size), order_id=int(ref), flags=int(flags))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("dbn", help="Databento XNAS.ITCH MBO file, one stock")
    p.add_argument("symbol")
    p.add_argument("out", help="ITCH 5.0 file to write")
    p.add_argument("--locate", type=int, default=1, help="stock locate to use in the messages")
    a = p.parse_args(argv)
    blob, stats = convert(read_dbn(a.dbn), a.symbol, a.locate)
    with open(a.out, "wb") as f:
        f.write(blob)
    for k, v in sorted(stats.items()):
        print(f"  {k}: {v:,}", file=sys.stderr)
    print(f"wrote {a.out}: {len(blob):,} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
