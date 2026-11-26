# Results

What the project has found so far, kept current: this page is updated after every round. Each round's rule, expectations and full numbers are in the [trial log](TRIALS.md) (written before each run, results added after); why each piece is built the way it is, in [DESIGN.md](DESIGN.md).

**Last updated: 26 Nov 2026**, after the first hold-out test (H1).

## Where things stand

The question: can a discretionary trader's read of the chart (trend or range, structure, levels, price action) make a market maker lose less to adverse selection? Tested on real Nasdaq data (AAPL; MSFT as a check), with queue-aware fills.

1. **Joining the touch loses, every day, to adverse selection.** We earn about 1.2 ¢ a share of spread and give back about 1.5 ¢ within 10 s: −0.29 ¢ a share over 10 random days of 2026 (−0.84 ¢ on a 2019 day). Maker rebates (about 0.2 ¢) would not close the gap.
2. **Latency is the one simulator setting that always matters:** 50 µs costs about 0.17 ¢ a share, on 10 of 10 days. It hurts through the stale quote being hit as a level breaks, not through queue position: a ladder of orders behind the touch doesn't help ([7b](TRIALS.md#round-7b-1-the-ladder-queue-position)).
3. **Trend reads come too late to help the market maker.** Knowing the 1 min trend leg with hindsight would remove the first-second loss entirely; but half of that value is gone if the leg is known 30 s late, and all of it by 120 s. A swing-based read confirms a turn about 120 s after it ([7c](TRIALS.md#round-7c-the-delayed-oracle-curve-how-fast-must-a-trend-read-be)). So EMA trend, VWAP, market structure and strong-trend reads all came out as noise: 0 of 9 primary tests ([rounds 1, 2, 7a, 7d-1](TRIALS.md#test-budget)).
4. **One price-action read stood out on the development days: a failed breakout at a level ("B1 trap") at the open.**
   - Fills against it were 0.76 ¢ a share worse in the next second; the price moved +18 ¢ its way within 2 minutes, on 9 of 10 days ([7d-2](TRIALS.md#round-7d-2-price-action-at-levels-b1-trap-b2-pullback-end), [7e](TRIALS.md#round-7e-event-study-does-the-price-go-the-reads-way-far-enough-for-a-taker)).
   - **As a taker it doesn't survive costs:** crossing the spread at the open costs about 7 ¢ a round trip; net +1.4 ¢ a share, interval [−10.5, +12.4] ([7f](TRIALS.md#round-7f-taker-backtest-b1-at-the-open)).
   - **In the market maker** (pull the side B1 reads against, 09:30-10:30) it cut the loss by $563 a day on the development days, through inventory: not standing long into a fall that takes minutes ([7d-3](TRIALS.md#round-7d-3-the-market-maker-pulls-the-side-b1-reads-against)).
5. **That rule was frozen and tested once on 10 new days (9-20 Nov 2026): not confirmed.** AAPL +$351 a day, 95 % interval [−13, +769], 7 of 10 days better: the same direction and channel, smaller (as expected after picking the best of several tries), and the interval misses zero by $13. MSFT, unchanged: +$32, noise ([H1](TRIALS.md#hold-out-h1-pull-the-side-b1-reads-against-at-the-open-on-new-days-frozen-before-buying)).

**Score so far:** 17 primary tests, 222 in all (about 1 in 20 looks real by chance); 1 hold-out spent; **nothing confirmed on new data yet.** The method held: every expectation was written down first, and the one candidate that looked good was shrunk by fresh days instead of being believed.

**Open:** a second, larger fresh-day test of B1, or a library of price-action setups (e.g. opening fakeouts) tested as a mid-speed taker across stocks ([ideas](TRIALS.md#ideas-to-try-later-not-run-yet)).

## Rounds at a glance

| round | question | verdict |
|---|---|---|
| [1](TRIALS.md#round-1-ema-trend-and-vwap-signal-study-stage-1), [2](TRIALS.md#round-2-market-structure-signal-study-stage-1) | Do EMA trend, VWAP, market structure pick out the losing fills? (one day) | noise, 0 of 6 |
| [7a](TRIALS.md#round-7a-where-the-loss-comes-from-queue-level-broke-and-trend-oracle-vs-real-time) | Where does the loss come from: queue or trend? | both real (level broke; hindsight leg), neither tradable as is |
| [7b](TRIALS.md#round-7b-1-the-ladder-queue-position) | Does a ladder (better queue place) help, at 0 or 50 µs? | no |
| [7c](TRIALS.md#round-7c-the-delayed-oracle-curve-how-fast-must-a-trend-read-be) | How fast must a trend read be? | within ~30 s of the turn |
| [7d-1](TRIALS.md#round-7d-1-fast-reads-already-in-the-fills-placed-on-the-delayed-oracle-curve) | Are any existing reads that fast? | no |
| [7d-2](TRIALS.md#round-7d-2-price-action-at-levels-b1-trap-b2-pullback-end) | Price action at levels (B1 trap, B2 pullback end) | B1 at the open stands out; B2 no |
| [7e](TRIALS.md#round-7e-event-study-does-the-price-go-the-reads-way-far-enough-for-a-taker) | Does the price go B1's way far enough for a taker? | at the open, +18 ¢ in 2 min |
| [7f](TRIALS.md#round-7f-taker-backtest-b1-at-the-open) | B1 at the open as a real taker | not distinguishable from 0 after costs |
| [7d-3](TRIALS.md#round-7d-3-the-market-maker-pulls-the-side-b1-reads-against) | Market maker pulls the side B1 reads against | +$563 a day (development days) |
| [H1](TRIALS.md#hold-out-h1-pull-the-side-b1-reads-against-at-the-open-on-new-days-frozen-before-buying) | The same, frozen, on 10 new days | **not confirmed** (AAPL +$351 [−13, +769]) |

## Milestone 5: market making on one replayed day

The first results, on one day of 2019. Why each piece is built the way it is: [DESIGN.md](DESIGN.md) (decisions D24–D37).

### Setup

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

### P&L split: where the money goes

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

### Is a difference real? Block bootstrap

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

### What we learned

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

### Known limitations

- **One day, one stock, in-sample.** Nothing here has been checked on a day it wasn't built on.
- **One venue.** Nasdaq's own book is often wider than the consolidated best bid and offer.
- **No stop loss or aggressive exit.** We only ever quote passively.
- **The 0 µs pull results may be flattered.** A sweep that shows up as several same-timestamp executions lets the 0 µs simulator react between them, which a real system can't do. Not yet checked.

### Next (as planned then)

**Milestone 6: the trend/range signal.**
- Turn a discretionary read of trend versus range, and of levels, into rules that widen, skew or pull quotes.
- Measure the result with the same split and the same paired bootstrap.
- The target is the first-second adverse selection.
- The rules are written down before looking at the results.

After that, the optimization phase: more days (Databento), held-out test days, and tuning.

### Reproduce

```bash
cmake --preset release && cmake --build --preset release
research/run_m5.sh data/itch/12302019.NASDAQ_ITCH50   # 7 runs + bootstrap tables into data/sim/m5/
# faster: the same on the AAPL-only slice (identical results, D43)
./build/release/cpp/itch_extract data/itch/12302019.NASDAQ_ITCH50 AAPL data/itch/12302019.AAPL.ITCH50
research/run_m5.sh data/itch/12302019.AAPL.ITCH50
```

## Milestone 6, first step: the same baseline on 10 days of 2026

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
