# Books and Orders

A limit order book, a Nasdaq ITCH 5.0 parser and a queue-aware market-making simulator in C++20, with the research around them in Python and a chart hub in TypeScript.

**The question:** can a discretionary trader's read of the market (trend or range, structure, levels) make a market maker lose less to adverse selection? It is answered on real Nasdaq data, with fills that only happen when the real queue ahead of us has traded.

## Status

| # | Milestone | State |
|---|---|---|
| 1 | Order book: price levels, FIFO queues, price-time matching | done |
| 2 | Nasdaq TotalView-ITCH 5.0 binary parser | done |
| 3 | Replay a full trading day (268.7 M messages) through the books, checked | done |
| 4 | Market-making simulator: our quotes join the real queue | done |
| 5 | Strategies (naive, Avellaneda–Stoikov, pull-when-thin), P&L split, bootstrap | done |
| 6 | The discretionary signal: indicators, market structure, levels, price-action reads, tested on 2026 days | in progress: first hold-out test run (not confirmed) |

## What it has found so far

Updated after every round. The summary is in [RESULTS.md](docs/RESULTS.md); every test, with its expectations written before the run, is in the [trial log](docs/TRIALS.md).

- **Joining the touch loses to adverse selection on every day tested:** about 1.2 ¢ a share of spread earned, 1.5 ¢ given back within 10 s (10 random AAPL days of 2026).
- **Trend reads come too late.** The value of knowing the trend sits at its turn: half is gone 30 s later, all of it by 2 minutes, and swing-based reads confirm a turn about 2 minutes after it. EMA, VWAP and market-structure reads: 0 of 9 tests real.
- **One price-action read stood out:** a failed breakout at a level, at the open. Too costly to trade as a taker (the open's round trip is about 7 ¢); as a market maker, pulling the quote it reads against cut the loss by $563 a day on the development days.
- **Frozen and tested once on 10 new days: not confirmed.** +$351 a day, 95 % interval [−13, +769]: same direction, smaller, and not distinguishable from zero.
- **Score:** 17 primary tests, 222 in all, 1 hold-out spent, nothing confirmed on new data yet.

## How the simulation stays honest

- **Queue position:** our order joins the back of the real queue at its price. It fills only once the real orders ahead of it have traded.
- **Never better than the touch:** a price nobody in the real data saw can't be simulated.
- **Latency:** every action we take waits the stated delay.
- **No look-ahead:** indicators see closed bars only. A swing counts only from the bar that confirmed it. A fill's snapshot excludes the trade that filled it.
- **Statistics:** P&L is split into spread / adverse selection / inventory. Differences are judged with a paired block bootstrap.
- **Tests are counted:** every hypothesis is written before its result in the public [trial log](docs/TRIALS.md), and every test is counted, so the false positives multiple testing produces aren't mistaken for findings.
- **Confirmed once, on unseen days:** a candidate is frozen in writing, with its single test and pass mark, before its hold-out days are even bought; it is run once, and those days are then spent.
- **Independent checks:** the bars and indicators match Databento's bars for the same day: volume identical in 100 % of minutes, indicator code to the last printed digit.

## How it fits together

```
Nasdaq ITCH 5.0 file
   │  parser (zero-copy, memory-mapped)
   ▼
Replay: one order book per stock
   │
   ▼
Simulator ── our quotes in the real queues, latency, fills, inventory
   │   └── MarketState: 1 s / 15 s / 1 min / 5 min bars, EMA, VWAP, ATR,
   │       market structure, shown read-only to the strategy
   ▼
fills CSV (each fill with the charts as they were at that moment)
   │
   ├── research/ (Python): bootstrap intervals, signal studies
   └── hub/ (TypeScript): candles, indicators, structure, trend/range and fills on one chart
```

## Repository

| Path | What |
|---|---|
| `cpp/` | The C++20 library, tools (`mm_sim`, `chart_dump`, `itch_extract`, …) and GoogleTest tests |
| `research/` | Python analysis: bootstrap, signal study, exports for the hub |
| `hub/web/` | The chart hub: Vite + React + TypeScript + lightweight-charts |
| `docs/DESIGN.md` | Every component and every design decision, numbered (D1, D2, …), with its reason |
| `docs/RESULTS.md` | Where things stand, kept current, then each milestone's results |
| `docs/TRIALS.md` | The trial log: every test, written before the run, the result added after |

## Build and run

Needs CMake 3.24+, a C++20 compiler, Python 3 and Node 20.19+ (for the hub).

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug   # tests (ASan + UBSan)
cmake --preset release && cmake --build --preset release
```

The market data isn't in the repo: it is Nasdaq's, and large. Two sources, read the same way:
- **Nasdaq's sample ITCH 5.0 days** (`emi.nasdaq.com/ITCH`): the development day `12302019.NASDAQ_ITCH50`.
- **Any day, one stock at a time, from Databento** (XNAS.ITCH, MBO). `research/dbn_to_itch.py` turns its records back into ITCH 5.0. On the development day, the converted file gives byte-identical simulator fills to Nasdaq's own file.
- **The study days (milestone 6):** 10 development days of AAPL drawn at random from January-September 2026, and 10 hold-out days (9-20 Nov 2026, AAPL and MSFT) bought only once a rule was final, and used once (26 Nov 2026). Each converted day must replay through the books with 0 errors and 0 orders left at the close.

```bash
# or: a Databento MBO file for one stock, turned into ITCH 5.0
research/.venv/bin/python research/dbn_to_itch.py data/databento/AAPL_20191230_XNAS_mbo.dbn.zst AAPL data/itch/20191230.AAPL.dbn.ITCH50
research/convert_days.sh AAPL 2026   # every downloaded 2026 day, each checked by a full replay
research/run_days.sh AAPL 2026       # the baseline on each of those days, one line per day

# one stock's slice of the day: every tool then runs in about a second
./build/release/cpp/itch_extract data/itch/12302019.NASDAQ_ITCH50 AAPL data/itch/12302019.AAPL.ITCH50

# milestone 5: 7 simulator runs + bootstrap tables
research/run_m5.sh data/itch/12302019.AAPL.ITCH50

# the hub: export the days, then open http://127.0.0.1:5174
research/export_hub.sh data/itch/12302019.AAPL.ITCH50 2019-12-30 AAPL
research/export_days.sh AAPL 2026    # the 10 development days, with trend/range shading
(cd hub/web && npm install && npm run dev)
```

More commands are in [DESIGN.md § Build](docs/DESIGN.md#build).

## Stack

C++20, CMake, GoogleTest · Python (standard library for the analysis) · TypeScript, React, Vite, lightweight-charts, vitest

## About

_Coming soon._
