# Results: milestone 5, market making on one replayed day

What the simulator found so far, on one page. Why each piece is built the way it is lives in [DESIGN.md](DESIGN.md) (decisions D24–D37); this page is what came out.

## Setup

- **Data:** Nasdaq TotalView-ITCH 5.0, 30 Dec 2019, the full day. One stock, **AAPL**.
- **Quoting:** 09:31 to 16:00 ET, 100 shares a side, inventory limit ±500 shares. Inventory is marked at the 16:00 mid (D28).
- **Honest fills:**
  - Our orders join the real queue at its back, and are filled only when the real orders ahead of us have traded (D24).
  - We never quote better than the touch, only at it or behind it.
  - Every action waits the stated latency (D25).
- **No fees, no rebates** unless stated.
- **Strategies:**
  - **naive:** join the best bid and the best ask.
  - **Avellaneda–Stoikov (AS):** lean the quotes against inventory, using a rolling volatility estimate (D29–D31). The setting is cents of lean per 100 shares held.
  - **pull θ:** naive, but drop a side whose share of the touch is below θ (D34).
- **Caveat:** one development day, in-sample. These numbers say what happened on this day and what is noise on this day, not what holds on other days.

## P&L split: where the money goes

Every fill's P&L splits exactly into three parts (D35):
- **spread:** what we earn against the mid just before the fill;
- **adverse selection:** how far the mid moves against us within 10 s;
- **inventory:** holding the position after that, to 16:00.

| run | shares | msgs / fill | spread | adverse, 1 s | adverse, 10 s | inventory | total |
|---|---|---|---|---|---|---|---|
| naive, 0 µs | 468 K | 16.7 | +$4,838 (+1.03 ¢) | −1.69 ¢ | −$8,779 (−1.87 ¢) | +$145 | **−$3,796** |
| AS 0.5 ¢ | 407 K | 22.7 | +$4,385 (+1.08) | −1.72 | −$7,780 (−1.91) | +$482 | −$2,913 |
| AS 1.0 ¢ | 342 K | 28.3 | +$3,703 (+1.08) | −1.74 | −$6,530 (−1.91) | +$512 | −$2,315 |
| pull 0.2 | 340 K | 36.9 | +$3,598 (+1.06) | −1.73 | −$6,550 (−1.93) | −$46 | −$2,998 |
| pull 0.3 | 242 K | 63.5 | +$2,600 (+1.08) | −1.81 | −$4,474 (−1.85) | +$255 | −$1,619 |
| naive, 50 µs | 470 K | 16.7 | +$4,569 (+0.97) | −1.72 | −$9,276 (−1.97) | +$1,341 | −$3,366 |
| pull 0.2, 50 µs | 361 K | 30.7 | +$3,631 (+1.00) | −1.81 | −$7,505 (−2.08) | +$737 | −$3,138 |

## Is a difference real? Block bootstrap

This table looks at the part a strategy controls: P&L per share at 10 s, which is spread plus adverse selection.
- **Intervals:** 95 % intervals from 2,000 resamples of the day's 78 five-minute blocks.
- **Paired comparison:** each run is compared with naive at 0 µs on the same blocks every time (D37).

| run | cents / share | 95 % interval | vs naive 0 µs | 95 % interval | verdict |
|---|---|---|---|---|---|
| naive, 0 µs | −0.84 | [−1.02, −0.67] | | | |
| AS 0.5 ¢ | −0.83 | [−1.03, −0.64] | +0.01 | [−0.11, +0.13] | noise |
| AS 1.0 ¢ | −0.83 | [−1.03, −0.64] | +0.02 | [−0.14, +0.15] | noise |
| pull 0.2 | −0.87 | [−1.10, −0.64] | −0.03 | [−0.14, +0.09] | noise |
| pull 0.3 | −0.78 | [−1.03, −0.52] | +0.07 | [−0.12, +0.25] | noise |
| naive, 50 µs | −1.00 | [−1.23, −0.78] | −0.16 | [−0.32, −0.03] | **worse** |
| pull 0.2, 50 µs | −1.07 | [−1.32, −0.83] | −0.23 | [−0.40, −0.06] | **worse** |

The verdicts don't change at a 1 s horizon, or with 1-minute or 15-minute blocks.

## What we learned

1. **The loss is adverse selection, and it comes fast.**
   - We earn about 1 cent a share of spread and give back about 1.9 cents within 10 s. About 90 % of that comes in the first second.
   - Inventory is close to zero. This is why inventory management (AS) has little to fix.
2. **The loss is real, not bad luck:** the naive interval is well below zero.
3. **No strategy so far beats naive per share.**
   - AS and the pull rule lose fewer dollars only because they trade fewer shares.
   - Their adverse selection per share is the same (−1.7 to −1.8 cents at 1 s), and the paired intervals all contain zero.
   - The pull rule also costs up to 4x the quote messages.
4. **Latency is the one difference that shows up, and total P&L hid it.**
   - At 50 µs total P&L looked better (−$3,366 vs −$3,796), but only through the inventory part, which is the day's price path.
   - On the controllable part, 50 µs is 0.16 cents a share worse, and the interval excludes zero.
5. **The fills tell us why.**
   - Joining at the back of the queue means 56 % of our shares are filled as the whole level breaks, the moment the price is about to move through it (D33).
   - Queue imbalance predicts the next second (a thin bid: 45 % down vs 28 % up) but nothing at 10 s (D32).
   - Acting on that signal by pulling moves the bad fills around instead of removing them (D34).
6. **Rebates help but can't pay for it.**
   - A 0.2 ¢ maker rebate takes the naive loss to −$2,859, and 0.3 ¢ takes it to −$2,391.
   - Breaking even would need 0.81 ¢ a share, about three times the best tier (D36).

## Known limitations

- **One day, one stock, in-sample.** Nothing here has been checked on a day it wasn't built on.
- **One venue.** Nasdaq's own book is often wider than the consolidated best bid and offer.
- **No stop loss or aggressive exit.** We only ever quote passively.
- **The 0 µs pull results may be flattered.** A sweep that shows up as several same-timestamp executions lets the 0 µs simulator react between them, which a real system can't do. Not yet checked.

## Next

**Milestone 6: the trend/range signal.**
- Turn a discretionary read of trend versus range, and of levels, into rules that widen, skew or pull quotes.
- Measure the result with the same split and the same paired bootstrap.
- The target is the first-second adverse selection.
- The rules are written down before looking at the results.

After that, the optimization phase: more days (Databento), held-out test days, and tuning.

## Reproduce

```bash
cmake --preset release && cmake --build --preset release
research/run_m5.sh data/itch/12302019.NASDAQ_ITCH50   # 7 runs + bootstrap tables into data/sim/m5/
# faster: the same on the AAPL-only slice (identical results, D43)
./build/release/cpp/itch_extract data/itch/12302019.NASDAQ_ITCH50 AAPL data/itch/12302019.AAPL.ITCH50
research/run_m5.sh data/itch/12302019.AAPL.ITCH50
```

# Milestone 6, first step: the same baseline on 10 days of 2026

The 10 development days (D53), naive, 0 µs, quoting 09:31-16:00. Cents per share, adverse selection over 10 s, P&L in dollars with inventory marked at the 16:00 mid. Descriptive only: no test is counted (D55).

| Day | Fills | Shares | Spread | Adverse (10 s) | Inventory | Net ¢/sh | P&L $ |
|---|---|---|---|---|---|---|---|
| 4 Feb | 39,218 | 1,988,402 | 1.16 | −1.45 | −0.02 | −0.31 | −6,150 |
| 3 Mar | 27,248 | 837,890 | 1.09 | −1.42 | −0.07 | −0.39 | −3,277 |
| 9 Mar | 20,539 | 698,241 | 1.09 | −1.45 | +0.37 | 0.00 | +28 |
| 21 May | 24,767 | 790,122 | 0.88 | −1.24 | +0.04 | −0.32 | −2,507 |
| 27 May | 24,026 | 708,436 | 1.00 | −1.28 | −0.01 | −0.28 | −1,993 |
| 7 Jul | 24,248 | 786,612 | 1.26 | −1.72 | −0.06 | −0.52 | −4,093 |
| 23 Jul | 20,335 | 563,896 | 1.40 | −1.75 | −0.05 | −0.40 | −2,248 |
| 30 Jul | 31,610 | 863,601 | 1.53 | −1.67 | +0.10 | −0.04 | −369 |
| 5 Aug | 31,753 | 869,686 | 1.05 | −1.42 | +0.10 | −0.27 | −2,349 |
| 2 Sep | 21,373 | 524,548 | 1.09 | −1.68 | +0.12 | −0.46 | −2,433 |
| **All** | **265,117** | **8,631,434** | **1.16** | **−1.49** | **+0.04** | **−0.29** | **−25,392** |

- **Adverse selection beats the spread on every one of the 10 days.** 9 lose; 9 Mar breaks even on inventory alone.
- **Smaller than in 2019:** −0.29 ¢ against −0.84 ¢ a share, with about 3 times the fills a day.

```bash
research/convert_days.sh AAPL 2026   # Databento MBO -> ITCH, each day replay-checked
research/run_days.sh AAPL 2026       # this table, into data/sim/days/AAPL/
```
