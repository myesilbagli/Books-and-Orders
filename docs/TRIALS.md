# Trial log

Every idea tested on the data, in the order it was tried. Each entry is written **before** the run: the question, the rule and its parameters, the primary test and its pass mark, and the expectations (Claude's, which writes the code, and the founder's). The result is added **after**, below it, and the entry is not edited again; a correction gets a new dated line. Failed ideas stay in. This is what keeps the exploration honest: with many tries, some look real by chance, so every test is counted in the budget below (D47, D55). Public since 26 Nov 2026 (D67).

- The founder's expectations are quoted as he wrote them, in Turkish, with the reading used in English after each.
- "Real" = the 95 % interval excludes 0. ¢ = cents a share. Days, data and methods: [DESIGN.md](DESIGN.md). The current state of knowledge, on one page: [RESULTS.md](RESULTS.md).

## Test budget

At 95 %, about 1 test in 20 looks real by chance. Every primary test counts here, across all rounds.

| round | what | primary tests | all tests | run? |
|---|---|---|---|---|
| 1 | EMA trend (1 min, 1 s), VWAP: signal study | 3 | 18 | yes, 23 Nov 2026: 0 of 3 primary real |
| 2 | Market structure 1 min (A, B, C) + 15 s supporting | 3 | 30 | yes, 23 Nov 2026: 0 of 3 primary real |
| 7a | Queue (level broke), oracle leg, confirmed leg, structure, strong trend | 5 | 30 | yes, 24 Nov 2026: level broke and oracle real (neither a pre-trade signal); 0 of 3 real-time trend reads real |
| 7b-1 | Ladder, levels 2 and 3 vs naive | 2 | 12 | yes, 24 Nov 2026: 0 of 2 primary real; ladder flat to slightly worse per share |
| 7b-2 | Ladder 2 vs naive at 50 µs | 1 | 6 | yes, 24 Nov 2026: 0 of 1 primary real; identical per share |
| 7c | Delayed-oracle curve, 7 lags | 0 | 14 | yes, 24 Nov 2026: half the oracle gone by 30 s, none by 120 s (= median confirmation) |
| 7d-1 | Fast reads in the fills vs the curve | 0 | 14 | yes, 24 Nov 2026: all worth nothing; 66-68 % agreement with the leg yet 0 value |
| 7d-2 | Price action at levels: B1 trap, B2 pullback end (whole day + 4 sessions) | 0 | 20 | yes, 25 Nov 2026: B1 −0.19 ¢ at 1 s (64 s equiv. lag), interval touches 0; at the open −0.76 [−1.24, −0.13]; at lunch the other way. 2 of 20 intervals off 0 (about 1 by chance) |
| 7e | Event study: B1, B2 (and 15 s momentum / fade baselines) forward move vs taker cost | 0 | 60 | yes, 25 Nov 2026: B1 at the open +18 c at 2 min [+7, +29], 9/10 days, over a 2.6 c round trip; elsewhere about 0; B2 wrong-way at 2 min; baselines 0 |
| 7f | Taker backtest: B1 at the open, 100 ms, 2 min hold (+ 3 sensitivity runs) | 0 | 4 | yes, 25 Nov 2026: +1.4 c net [−10.5, +12.4], 7/10 days, worst trade −$2.40/share: not distinguishable from 0; the open's round trip costs about 7 c |
| 7d-3 | MM: pull the side B1 reads against, all day and at the open only, vs naive | 2 | 12 | yes, 25 Nov 2026: 0 of 2 primary real (10 s ¢/share −0.04, −0.02); but P&L +$567 and +$563 a day (8/10, 9/10 days), from the inventory part: not the primary, a candidate |
| H1 | Hold-out: pull B1 at the open, AAPL 9-20 Nov (MSFT replication) | 1 | 2 | yes, 26 Nov 2026: FAIL, AAPL +$351/day [−13, +769], 7/10; MSFT +$32 noise. Hold-out spent |
| **total** | | **17** | **222** | real-time trend reads (rounds 1, 2, 7a Q3-Q5): 0 of 9 primary real; 4 "real" of their 66 tests (about 3.3 expected by chance) |

## Round 1: EMA trend and VWAP (signal study, stage 1)

**Question:** are the fills we took against the signal's reading worse per share than the rest? (D47)

**Rules (what flags a buy; a sell is the mirror):**
- 1m trend: at the fill, the last closed 1 min bar has EMA9 < EMA21.
- 1s trend: the same on 1 s bars.
- VWAP: mid below VWAP.

**Primary test:** 10 s P&L per share, flagged minus rest, whole day (dev day 30 Dec 2019, AAPL, naive 0 µs).

**Hypotheses (founder, 23 Nov 2026, before any result):**
- EMAs alone: "trend var demeye asla yetmiyor emalar, yatay piyasada cok noisy calisiyor. bunlardan eminim. gercekten saglam bir trend oldugunda hani duzgun calisiyor, cok guvenilebilir bir sey cikmayabilir bu durumdan dolayi, sadece test etmek icin okeyim ama"
  - Earlier: "1m trend bize bir seyler anlatabilir"
  - Read as: 1m trend, at most a small difference (flagged worse), because the EMA rule mixes range days in.
- 1s trend: "1 saniyede hele cok noisy olacagindan eminim."
  - Read as: no difference.
- VWAP: "vwap olayi da kritik yani cok fazla case var dayi ya, vwap alti net kotu veya vwap alti net iyi demek dogru degil bence su an. yani bunlar cok degisken seyler anlatabildim mi"
  - Read as: no difference on its own. What being below VWAP means depends on the context (rejected from it or reclaiming it), so VWAP is a level, not a direction.
  - Round 3 candidate: VWAP as a key level (rejection vs reclaim).
- Beyond round 1: "ben market structure'in daha iyi calisacagina inaniyorum. emalar bir destek mekanizmasi gibi."
  - Round 2 candidate: market structure (swings, HH/HL), with the EMAs as a confirming filter.

**Dropped before testing (not counted):** Bollinger %B and Stoch RSI. "bollinger ve stochastic cok iyi calismayabilir onlari gozardi edebiliriz su an" (Bollinger and Stochastic may not work well; we can ignore them for now.)

**Result (23 Nov 2026, `data/sim/signals/round1_naive_0us.txt`):** all 3 primary tests are noise. The hypotheses held.

| signal | flagged share | flagged ¢ | rest ¢ | diff, 10 s | 95 % | 98.3 % | hypothesis |
|---|---|---|---|---|---|---|---|
| 1m trend | 49.8 % | −1.15 | −0.53 | −0.62 | [−1.62, +0.30] | [−1.88, +0.47] | small at most: held (worse direction, not significant) |
| 1s trend | 46.5 % | −1.11 | −0.61 | −0.50 | [−1.47, +0.31] | [−1.70, +0.48] | none: held |
| VWAP | 49.6 % | −1.01 | −0.68 | −0.33 | [−1.33, +0.60] | [−1.53, +0.83] | none, context-dependent: held |

- **All three lean the same way (flagged worse), but they're not three separate pieces of evidence.** A down-move puts the EMAs in "down" and the mid below VWAP at the same time, so the three groups overlap heavily.
- **The lean is all in the morning:** −1.23, −0.96 and −0.84 ¢ in the morning against about 0 in the afternoon. Not significant either. It may only be the open's volatility.
- **1 of the 18 tests came out "real":** VWAP at 1 s in the afternoon, flagged better (+0.35 ¢). About 0.9 were expected by chance (18 / 20). Not a finding.
- **Each signal flags about half the fills.** Pulling on any of them would halve the volume.
- **Power:** the intervals are about ±1 ¢ wide. A real difference of 0.6 ¢ would need about 2.5× the data (the width shrinks with the square root) to be seen: roughly 3 days. One day can't settle effects this size.

## Round 2: market structure (signal study, stage 1)

**Definitions (D48-D50):** ZigZag swings on closed bars, a swing = a pullback of c x ATR(14). Score = sign(high2 - high1) + sign(low2 - low1): +2 up, -2 down, else range. Changes only at a confirmed swing.

**Swing size c, chosen by eye on the hub with the fills hidden (founder, 23 Nov 2026, before any result):**
- 1 min: **c = 1.5**. "1m icin 1.5 iyi duruyor, ama bunlari daha sonra boyle guzel bir motorla farkli kombinasyon deneriz zaten ama su an bu iyi gozukuyor" (1.5 looks good for 1 min; we'll try other combinations later with the engine, but this looks right for now.)
- 15 s: **c = 2**. "15 snde 1.5 biraz noisy duruyor 2 ye daha fit gibiyim." (On 15 s, 1.5 looks a bit noisy; 2 fits better.)
- 5 min: parked (D49). 1 s: not tested (noise in round 1, 3,278 swings at c = 2).

**Founder's read of the chart:** "bazi yerlerde trendler cok iyi calisiyor, baya iyi yani akiyor, range yapilan yerlerde de guzel bir range donuyor. mesela 9.30 10.30 arasi 9.35 9.39 arasi saglam bi downtrend, sonra 9.40-10.10 arasi bi range, sonra yine bi downtrend." (In places the trends work very well and flow, and in ranges it shows a clean range: e.g. 09:35-09:39 a solid downtrend, then 09:40-10:10 a range, then a downtrend again.)
- Note: on the hub the real-time score in 09:40-10:10 alternates down and range. The eye sees the finished box; the algorithm only the past. Round 2 measures whether the real-time version still helps.

**Planned tests (1 min primary, 15 s secondary):**
- A. Structure: fills against it (buy at -2, sell at +2) vs the rest.
- B. Structure + EMA: the same, only when the 1 min EMA9/21 agrees.
- C. Regime: fills in a range (score not +-2) vs fills in a trend (+-2).

**Hypotheses:**
- C: "range icin market making mantken daha iyi calismali kesinlikle". Read as: range fills better than trend fills.
- A and B (founder, 23 Nov 2026, before the run): "yani trend oldugunda trend yonunun tersine market making yapmak tabii mantikli durmuyor yani edgesel problem cikiyor bence confirmliyorum, ama her seye supheci yaklasmaya calisiyorum dostum." (Market making against the trend's direction doesn't make sense, an edge problem; I confirm it, but I try to be sceptical of everything.)
  - A: fills against the 1 min structure are clearly worse than the rest, a clearer gap than round 1's EMA order.
  - B: with the EMAs agreeing, the gap is larger (fewer but cleaner trends; wider intervals).

**Result (23 Nov 2026, `data/sim/signals/round2_naive_0us.txt`):** all 3 primary tests are noise. None of the hypotheses was confirmed; C went the other way (no difference).

| test | flagged share | flagged ¢ | rest ¢ | diff, 10 s | 95 % | 98.3 % | hypothesis |
|---|---|---|---|---|---|---|---|
| A, 1m structure | 32.0 % | −1.17 | −0.69 | −0.48 | [−1.61, +0.51] | [−1.82, +0.82] | clearly worse: not confirmed (right direction, noise) |
| B, + EMA | 30.8 % | −1.22 | −0.67 | −0.54 | [−1.67, +0.47] | [−1.92, +0.78] | larger than A: not confirmed (almost the same as A) |
| C, range vs trend | 35.1 % | −0.90 | −0.81 | −0.09 | [−0.44, +0.23] | [−0.50, +0.31] | range clearly better: not confirmed |

- **The sign flips between halves of the day, as in round 1.** A in the morning: −1.31 ¢ (flagged worse); in the afternoon: +0.58 ¢ (flagged better). Neither is significant.
  - A possible reading: against-trend fills hurt in the morning's strong moves (09:30-10:15 down, then the rally), and help in the afternoon's choppy "trends". The structure label would then help only when the trend is strong.
  - **This is a new hypothesis, made from the data,** so it is exploratory only. It needs a trend-strength measure (e.g. efficiency ratio) and other days.
- **B is almost A:** the EMA filter removes only 1.2 % of fills (32.0 % → 30.8 %). When the 1 min structure is down, the EMAs are already down. B adds no independent information.
- **C is an informative null:** its interval is narrow (±0.35 ¢). On this day a 1 min range isn't better for the market maker than a trend, per fill. Range fills lose −0.90 ¢ and trend fills −0.81 ¢ at 10 s.
- **15 s:** nothing at all.
- **Supporting "real" results:** 1 of 30 (A, 1 s, morning, flagged worse, [−0.94, −0.02]), about what chance gives (1.5).
- **Power, again:** A's interval is ±1.06 ¢. One day can't tell a 0.5 ¢ effect from zero; that needs about 4-5 days.

## Round 7a: where the loss comes from: queue (level broke) and trend (oracle vs real time)

Written 24 Nov 2026, **before** running. Exploration on the 10 development days (D55), naive 0 µs fills, 1 min swings at c = 1.5.

Founder's question (24 Nov): if the trend were identified very well, would it cut adverse selection? Measure that before building the trend system further; also the queue (we are always at the back).

**Splits (flagged vs rest, within each day, block bootstrap, 1 s and 10 s):**
- **Q1 level broke:** flagged = fills where nothing was left at our price (`shares_left = 0`), the level broke as we filled.
- **Q2 oracle leg (hindsight, NOT tradable):** each fill in a ZigZag leg drawn with hindsight (swing made → next swing made; fills in a turning bar or after the last swing left out). Flagged = against the leg (buy in a down leg, sell in an up leg). Upper bound of any trend read at this swing size.
- **Q3 confirmed leg (real time):** the same swings, known only once confirmed: direction since the last confirmed swing. Oracle minus this = the cost of the confirmation lag.
- **Q4 1 min structure (= round 2 A on 10 days):** against a ±2 score.
- **Q5 strong trend:** against a ±2 score with ER 1m ≥ 0.6 (hub's opening threshold, not yet picked by the founder).
- **Cross table:** level broke × oracle leg, shares and ¢.

**My expectations (Claude), before the result:**
- Q1: broke fills clearly worse at 1 s and 10 s (2019: 56 % of shares, most of the adverse move).
- Q2: against-leg fills much worse at 10 s, partly by construction (a 1.5 ATR leg over ~10 min drifts about 1 ¢ per 10 s).
- Q3: a large part of Q2's gap is lost to the lag; maybe a third left.
- Q4, Q5: small, near noise, as on one day.
- Founder's expectation: not asked this time (he said start).

**Result (24 Nov 2026, `research/fill_anatomy.py`, 10 days, 2000 resamples):**

| split | flagged share | 1 s diff [95 %] | days worse | 10 s diff [95 %] (corrected) | days worse |
|---|---|---|---|---|---|
| level broke | 49.8 % | −0.37 [−0.42, −0.31] | 10/10 | −0.31 [−0.44, −0.18] ([−0.48, −0.13]) | 9/10 **real** |
| oracle leg | 49.8 % | −0.62 [−0.76, −0.48] | 10/10 | −4.69 [−5.28, −4.15] | 10/10 **real, not tradable** |
| confirmed leg | 50.0 % | +0.11 [−0.01, +0.25] | 3/10 | −0.23 [−0.82, +0.32] | 4/10 noise |
| 1m structure | 30.6 % | +0.14 [−0.00, +0.26] | 3/10 | −0.22 [−0.74, +0.29] | 4/10 noise |
| strong trend | 5.1 % | +0.28 [+0.03, +0.49] flagged better | 3/10 | +0.30 [−0.98, +1.46] | 6/10 noise |

Cross table, 1 s (share of shares, ¢): broke+against 26.2 % −0.75; broke+with 25.3 % −0.15; held+against 23.6 % −0.46; held+with 24.9 % +0.16.

- **Expectations vs result:** Q1 as expected. Q2 as expected, and at 1 s too (−0.62; with-leg fills break even, +0.01). **Q3 wrong:** I expected a third of the oracle left; the confirmed leg keeps **nothing** (1 s even leans the other way). Q4, Q5 noise as expected; Q5's 1 s "flagged better" is a supporting test, 1 of 25 non-primary, about chance.
- **Trend and queue are two separate losses:** in the cross table each adds roughly on its own (trend ~0.6 ¢, queue ~0.3-0.6 ¢ at 1 s). The level-broke effect is not the trend in disguise.
- **The lag is everything:** knowing the 1 min leg would remove the 1 s loss entirely; knowing it once confirmed is worth nothing. A "better trend label" won't help; only a faster (leading) read could. Next: a delayed-oracle curve (the leg known L seconds after its start) to see how fast the value decays.
- **The queue is the robust lever:** 9-10 of 10 days. Supports the ladder (7b-1).

## Round 7b-1: the ladder (queue position)

Written 24 Nov 2026, **before** running. Exploration on the 10 development days (D55), 0 µs, naive top price.

- **Rule:** orders at the touch and levels − 1 ticks behind it, each left alone while it stays in that window (D60). Runs: levels 2 and 3 vs the naive single quote (levels 1). The limit counts every resting order as filled (stricter than naive's).
- **Measured:** ¢/share (spread + adverse, 1 s and 10 s), shares, P&L with inventory, share of fills with the level broken; ladder minus naive within each day, stratified block bootstrap.
- **Primary tests (2):** levels 2 − naive and levels 3 − naive, ¢/share at 10 s, whole day.
- **My expectation (Claude):** better ¢/share at 1 s (more fills while the level holds, 7a: held −0.12 vs broke −0.48 at 1 s), maybe +0.1 to +0.2 ¢; fewer shares broke. At 10 s smaller, because a deep order also fills in sweeps. Levels 3 adds little over 2. More shares traded per day, not fewer.
- **Founder's expectation:** wants to try it; thinks big firms do this to hold queue priority.

**Result (24 Nov 2026, `research/compare_runs.py naive_0us ladder2_0us ladder3_0us`, 2000 resamples):**

| run | shares | level broke | 1 s ¢ (diff vs naive) | 10 s ¢ (diff, corrected) | P&L $ |
|---|---|---|---|---|---|
| naive | 8.63 M | 49.8 % | −0.30 | −0.34 | −25,392 |
| ladder 2 | 11.39 M (+32 %) | 47.2 % | −0.31 (−0.02 [−0.03, −0.00], 8/10 worse) | −0.33 (+0.00 [−0.03, +0.03]) noise | −36,609 |
| ladder 3 | 12.64 M (+46 %) | 46.2 % | −0.33 (−0.03 [−0.05, −0.02], 9/10 worse) | −0.37 (−0.03 [−0.08, +0.01]) noise | −43,681 |

- **Expectation wrong:** I expected +0.1 to +0.2 ¢ at 1 s. The ladder is flat to slightly worse per share, trades 32-46 % more shares, so loses more dollars. 0 of 2 primary real.
- **Queue position barely changes which fills we get:** level-broke share falls only 49.8 → 47.2 → 46.2 %. 7a's held-vs-broke split was observational (which fills turned out well), not a lever: standing earlier in a queue doesn't make the market come to you in calm moments.
- **Likely why:** a deep order is at the front only after the price has come down to it, i.e. right after a move; those fills carry the move's momentum, so "front of queue" here is not the calm fill 7a saw.
- **Not tested:** latency. At 50 µs the naive quote loses the race to each new level (D35); a ladder is already there. Cheap next try: naive vs ladder 2 at 50 µs.
- Rebate not counted: more shares mean more rebate dollars; per share unchanged.

## Round 7b-2: the ladder with 50 µs latency

Written 24 Nov 2026, **before** running. Same 10 days, naive top price.

- **Why:** at 0 µs the naive quote reaches every new level instantly; with latency it arrives late and behind (milestone 5: 50 µs cost 0.16 ¢ a share, D35). A ladder is already queued at the next level when the touch moves there, so latency should hurt it less.
- **Runs:** naive 50 µs vs ladder 2 at 50 µs. **Primary test (1):** ladder 2 − naive at 50 µs, ¢/share at 10 s, whole day. Also described: each vs its own 0 µs run (what latency costs each).
- **My expectation (Claude):** latency costs naive more than the ladder; ladder 2 − naive at 50 µs better than at 0 µs, maybe +0.03 to +0.08 ¢, still small. Not enough to make the ladder profitable.
- **Founder's expectation:** chose to try it.

**Result (24 Nov 2026, `compare_runs.py naive_50us ladder2_50us`, 2000 resamples):**

| run | shares | level broke | 1 s ¢ | 10 s ¢ | P&L $ |
|---|---|---|---|---|---|
| naive 50 µs | 8.19 M | 59.5 % | −0.48 | −0.51 | −40,447 |
| ladder 2, 50 µs | 10.48 M | 56.2 % | −0.48 (−0.00 [−0.02, +0.01]) | −0.51 (−0.00 [−0.03, +0.03]) noise, 4/10 worse | −53,337 |

What 50 µs costs each, 10 s, within days: naive −0.17 [−0.22, −0.12], ladder 2 −0.17 [−0.21, −0.14]; 10/10 days for both.

- **Expectation wrong again:** the ladder doesn't soften latency at all; 0 of 1 primary real.
- **So latency doesn't hurt by arriving late at new levels:** it hurts through the stale quote (our old order stays live for 50 µs after we decide to move, D35) being hit as the level breaks. Level-broke share jumps 50 → 60 % for both. The ladder can't help with that: its touch order is just as stale.
- 2026's latency cost (0.17 ¢) matches 2019's (0.16 ¢).
- **Conclusion for the queue lever:** in AAPL, neither resting behind the touch nor latency's interaction with it changes fill quality. What matters is reacting to the level breaking (pulling the order in time), not where we stand.

## Round 7c: the delayed-oracle curve (how fast must a trend read be?)

Written 24 Nov 2026, **before** running. Same 10 days, naive 0 µs fills, 1 min swings at c = 1.5.

- **Rule:** the hindsight leg (7a's oracle), but known only L seconds after its turning bar ends; until then the trader still believes the previous leg. Flagged = against what he believes. L = 0 is 7a's oracle. L = 0, 5, 15, 30, 60, 120, 300 s.
- **Measured:** flagged minus rest at 1 s and 10 s, within days, block bootstrap, per L; and what share of L = 0's gap is left. Context: median leg length and median confirmation lag of the real swings.
- **Descriptive curve, no primary test** (14 supporting tests): it decides how fast a trend read would need to be, not whether a rule works.
- **My expectation (Claude):** the 1 s gap halves by about 30 s and is near zero by 2 min; the 10 s gap lasts longer (drift). The confirmation lag is around 2-3 min, past the point where anything is left, which is why the confirmed leg kept nothing.
- **Founder's expectation:** chose to run it.

**Result (24 Nov 2026, `research/oracle_curve.py`, 2000 resamples):** 1 min legs last 240 s (median); a swing is confirmed 120 s after its bar ends (median).

| lag | 1 s diff [95 %] | left | days worse | 10 s diff [95 %] | left |
|---|---|---|---|---|---|
| 0 s | −0.62 [−0.76, −0.48] | 100 % | 10/10 | −4.69 [−5.28, −4.15] | 100 % |
| 5 s | −0.53 [−0.67, −0.39] | 86 % | 10/10 | −4.27 | 91 % |
| 15 s | −0.41 [−0.55, −0.27] | 67 % | 10/10 | −3.72 | 79 % |
| 30 s | −0.31 [−0.44, −0.19] | 51 % | 10/10 | −3.21 | 68 % |
| 60 s | −0.21 [−0.34, −0.08] | 34 % | 10/10 | −2.21 | 47 % |
| 120 s | +0.06 [−0.07, +0.18] | −10 % | 2/10 | −0.60 [−1.19, −0.05] | 13 % |
| 300 s | +0.44 [+0.31, +0.58] | −71 % | 0/10 | +2.34 | −50 % |

- **Expectation about right this time:** half gone by 30 s, nothing by 2 min (I said halves by ~30 s, near zero by 2 min).
- **Why the confirmed leg kept nothing (7a):** the median confirmation comes 120 s after the turning bar, exactly where the curve crosses zero. Half of a 4 min leg is over before a ZigZag can know it began.
- **This is structural, not a parameter:** a ZigZag confirms a turn only after a c × ATR retrace, and a leg is at least c × ATR long, so at any c or timeframe confirmation eats a large part of the leg. A smaller c or a faster chart shortens both. A useful trend read must be a different kind of signal: one that sees the turn as it happens (order flow, imbalance, a smaller scale leading the larger), not one that confirms it.
- **The spec for such a read:** know the 1 min leg within about 30 s of its turn and it keeps half the oracle's 1 s gap (rest −0.15 ¢ vs about −0.30 ¢ for all fills, giving up about half the shares).
- **At 300 s the sign flips:** a leg learned 5 min late is usually over (legs last 4 min), so the belief is the wrong one.

## Round 7d-1: fast reads already in the fills, placed on the delayed-oracle curve

Written 24 Nov 2026, **before** running. Same 10 days, naive 0 µs fills.

- **Reads (flagged = against the read):** 1 min structure (baseline), 15 s structure, 1 s structure, 15 s strong trend (±2 and ER 15s ≥ 0.6), 1 s EMA trend, 1 min EMA trend, confirmed 1 min leg.
- **Measured:** flagged minus rest at 1 s and 10 s within days (block bootstrap); **equivalent lag** = where that 1 s gap sits on 7c's curve (−0.62 = 0 s, −0.31 = 30 s, about 0 = 120 s); coverage (share of shares with a direction); agreement with the hindsight 1 min leg where both have one.
- **Descriptive (no primary test):** it ranks reads for 7d-2/7d-3.
- **My expectation (Claude):** all slower than 60 s. 15 s structure agrees with the 1 min leg a bit more than chance (55-60 %) but its 1 s gap is small; 1 s EMA may show a 1 s gap from microstructure, not from the leg.
- **Founder (24 Nov):** candle patterns (morning star kind) and momentum are critical, and levels: at an intraday support/resistance zone he waits for a reversal confirmation. Worried that strict rules give false signals. Has read all of Al Brooks (trends, reversals, ranges). Will pick 3-4 Brooks setups for 7d-2.

**Result (24 Nov 2026, `research/turn_reads.py`, 2000 resamples):**

| read | covers | agrees with leg | 1 s diff [95 %] | equiv. lag | 10 s diff | worse 1 s |
|---|---|---|---|---|---|---|
| 1m structure | 61 % | 49 % | +0.14 [−0.00, +0.26] | nothing | −0.22 | 3/10 |
| 15s structure | 63 % | 52 % | +0.02 [−0.11, +0.16] | nothing | +0.12 | 6/10 |
| 1s structure | 61 % | 67 % | −0.01 [−0.15, +0.14] | (105 s, noise) | −0.15 | 7/10 |
| 15s strong trend | 11 % | 51 % | +0.03 [−0.19, +0.25] | nothing | +0.18 | 6/10 |
| 1s EMA trend | 100 % | 68 % | +0.11 [−0.03, +0.24] | nothing | +0.15 | 2/10 |
| 1m EMA trend | 100 % | 56 % | +0.15 [+0.02, +0.28] | nothing | −0.13 | 1/10 |
| confirmed 1m leg | 100 % | 66 % | +0.11 [−0.01, +0.25] | nothing | −0.23 | 3/10 |

- **As expected, none is fast enough; all "nothing".** 1m EMA's 1 s "against is better" is the only interval off zero (1 of 14, about chance).
- **The finding: agreeing with the leg is not the same as being worth something.** The 1 s EMA and the confirmed leg agree with the hindsight leg on 66-68 % of shares, yet are worth nothing. From 7c: a 30 s lag removes half the gap, and the first 30 s of a 240 s leg are about 12 % of the fills. **About half the oracle's value sits in the first ~12 % of each leg, at the turn**, exactly where every lagging read is still wrong. Being right mid-leg is already priced.
- **At 1 s, fills against short-term momentum do slightly better** (+0.11 to +0.15): short-horizon mean reversion (bounce), the opposite of what a trend rule would assume.
- **Spec for 7d-2 stays:** a read must flip within ~30 s of the turn. Coverage and agreement are not the measure; the 1 s gap is.

## Round 7d-2: price action at levels (B1 trap, B2 pullback end)

Written 25 Nov 2026, **before** running. Same 10 days, naive 0 µs fills, rerun so they carry `b1`, `b2` (older columns checked byte-identical on 4 Feb).

- **Reads (D63; definitions from the founder's Al Brooks notes, synthesised into measurable rules):**
  - B1: a 15 s bar at a 1 min swing high/low or the day's high/low makes a new 2 min high (low), pokes past the level by ≤ ¼ of the 1 min ATR, and it or one of the next 2 bars closes back. Read against the poke for 45 s.
  - B2: 1 min EMAs stacked 9 > 20 > 50 (or reverse); a 15 s bar that is the deepest of the last 4 comes within 0.2 ATR of the 1 min EMA 20 or VWAP and closes strongly the trend's way, beyond that level. Read with the trend for 45 s.
- **Calibrated on counts only, before this entry** (no markouts looked at): B1 at first fired on 20-35 % of all 15 s bars, B2 3-14 a day. Now B1 52-96 a day (median 69), B2 6-23 (median 14).
- **Measured:** as 7d-1 (1 s and 10 s gap, equivalent lag on 7c's curve, coverage, agreement with the hindsight leg), plus each by the founder's sessions (open 9:30-10:30, late morning to 12, lunch to 14, power to 16). Descriptive, no primary test (20 supporting).
- **My expectation (Claude):**
  - Coverage: B1 about 10-15 % of shares (69 × 45 s ≈ 13 % of the day, a bit more since fills crowd at turns); B2 about 2-4 %.
  - B1: agrees with the leg 55-60 %; 1 s gap −0.10 to −0.25 ¢, an equivalent lag of 60-100 s; the interval may still touch 0. Hurt by counter-trend traps inside steep moves (seen on the hub, 30 Jul 10:33). Best in the late morning (Brooks and the playbook: failed breakouts after the open), weakest at the open.
  - B2: too few shares to say much; sign likely negative (against the read worse) but the interval wide.
  - If B1 lands at "nothing", the next steps are the tight-channel veto (V) and order flow, not more candle shapes.
- **Founder's expectation:** chose to run it.

**Result (25 Nov 2026, `research/turn_reads.py`, 2000 resamples):**

| read | covers | agrees with leg | 1 s diff [95 %] | equiv. lag | 10 s diff [95 %] | worse 1 s |
|---|---|---|---|---|---|---|
| B1 trap | 15 % | **18 %** | **−0.19 [−0.42, +0.03]** | **64 s** | −0.76 [−1.76, +0.35] | 7/10 |
| B2 pullback end | 2 % | 40 % | −0.14 [−0.41, +0.16] | 76 s | +0.27 [−0.73, +1.25] | 7/10 |
| (7d-1 reads, unchanged) | | | all "nothing" except 1s structure (noise) | | | |

By session (against minus rest, 1 s; 10 s):

| read | session | covers | 1 s diff [95 %] | 10 s diff [95 %] | worse 1 s |
|---|---|---|---|---|---|
| B1 | open 9:30-10:30 | 23.6 % | **−0.76 [−1.24, −0.13]** | −1.76 [−4.14, +0.95] | 7/10 |
| B1 | late morning | 13.7 % | +0.09 [−0.24, +0.39] | +0.55 | 4/10 |
| B1 | lunch | 10.3 % | **+0.28 [+0.07, +0.46]** | +0.72 [−0.10, +1.55] | 1/10 |
| B1 | power | 13.1 % | −0.06 [−0.28, +0.14] | −0.79 | 7/10 |
| B2 | open | 1.3 % | −0.52 [−1.00, +0.01] | +0.03 | 6/8 |
| B2 | late morning | 2.8 % | +0.44 [−0.06, +0.97] | +0.94 | 2/9 |
| B2 | lunch | 3.1 % | −0.12 | +0.72 | 3/10 |
| B2 | power | 1.8 % | −0.32 | +1.03 | 7/9 |

- **B1 is the first real-time read on the right side of zero, at the 7d-2 spec's edge:** −0.19 ¢ at 1 s, an equivalent lag of 64 s (every 7d-1 read was "nothing"), worse on 7 of 10 days, but the whole-day interval still touches 0. Coverage 15 % as expected; the size as expected (−0.10 to −0.25).
- **Expectation wrong on agreement: 18 %, not 55-60 %.** B1 mostly points against the hindsight 1 min leg, yet fills against it still do worse at 1 s. So its value is not "seeing the 1 min turn early": it catches the 15 s-scale turns inside a 1 min leg (the pullbacks), short-lived moves the 1 min ruler doesn't measure. The equivalent lag is then a comparison of size, not of mechanism.
- **Expectation wrong on the session: the open, not the late morning.** At the open B1 covers a quarter of the shares and the fills against it lose 0.76 ¢ more at 1 s (1.76 ¢ at 10 s, wide). At lunch it turns the other way (+0.28, fills against B1 do better): in the quiet range a "trap" is just the range's edge, and the price comes back.
- **Many tests, read with care:** 20 supporting tests, about 1 expected off zero by chance; 2 are (B1 open, B1 lunch). The open is a subgroup picked after seeing the data: a candidate for a frozen rule confirmed on the hold-out, not a finding yet.
- **B2: too few shares (2 %)** to say anything; every interval but one touches 0.
- **Next candidates:** (1) 7d-3: a strategy that pulls or skews the side B1 says is toxic, first whole day and at the open only, against naive with `compare_runs` (P&L and shares, since pulling also gives up spread); (2) the tight-channel veto for B1; (3) the order-flow benchmark.

## Round 7e: event study, does the price go the read's way far enough for a taker?

Written 25 Nov 2026, **before** running. Same 10 days; the reads exactly as in 7d-2.

- **Why (founder, 25 Nov):** "can price action or harder math improve the current system; if it comes out negative whatever we do, turning it toward the discretionary direction makes more sense to me." 7d-2 measured B1 relative to other fills (MM view). A directional trader needs the absolute move after the read to beat the spread and fees.
- **Rule:** each B1 / B2 read is an event at the end of its 15 s bar (when known). The mid's move in its direction 10, 30, 60, 120, 300 s later, in cents; mean over events, 95 % interval from 5 min blocks resampled within days; share of events beating a taker's round trip. Baselines: every 15 s bar's direction (momentum) and its opposite (fade). Whole day and B1/B2 by session.
- **Cost:** a taker pays half the spread in and out (median half spread from the naive fills at the touch) plus about 0.3 c take fee each way: about 1.6 c if the spread is 1 c.
- **Descriptive, no primary test** (60 supporting cells; a handful will look real by chance).
- **My expectation (Claude):**
  - B1, whole day: +0.2 to +0.5 c its way at 10-60 s, well under the round trip; about 30-35 % of events beat it. Not a taker strategy on its own.
  - B1 at the open: larger, +0.5 to +1.5 c at 30-60 s, approaching but probably still under the cost.
  - B2: noise (too few events).
  - Momentum slightly negative at 10 s (7d-1's short-term bounce), fade slightly positive; both far under the cost.
  - At 300 s everything is noise (the intervals span several cents).
- **What it decides:** if B1's move clears the round trip somewhere (the open), a mid-speed taker version is worth building; if not, B1's only use is passive (7d-3: pulling the toxic quote, where we don't pay the spread).
- **Founder's expectation:** chose to run it; leaning toward the discretionary direction if every MM test is negative.

**Result (25 Nov 2026, `research/event_study.py`, 2000 resamples):** median half spread at the naive fills 1.00 c (the spread is usually 2 c, sometimes 1 c; fills come more when it's wide, so maybe a bit high): **taker round trip 2.6 c** (1.6 c at a 1 c spread).

Whole day, mean cents the read's way [95 %], share of events beating 2.6 c:

| read | events | 10 s | 30 s | 60 s | 120 s | 300 s |
|---|---|---|---|---|---|---|
| B1 trap | 718 | −0.13 [−0.90, +0.68] | +0.55 | +1.00 [−1.43, +3.36] | **+3.59 [+0.40, +7.22]** 47 % | +3.57 [−2.14, +9.36] |
| B2 pullback end | 149 | +1.18 [−0.12, +2.47] | −0.00 | −1.78 | **−4.70 [−9.08, −0.55]** | −6.27 |
| 15 s momentum (baseline) | 15,990 | +0.04 | −0.06 | +0.15 | +0.17 [−0.31, +0.68] | −0.12 |
| 15 s fade (baseline) | 15,990 | −0.04 | +0.06 | −0.15 | −0.17 | +0.12 |

By session (B1; B2 at the open only, the rest is noise):

| read | session | events | 10 s | 30 s | 60 s | 120 s | 300 s |
|---|---|---|---|---|---|---|---|
| **B1** | **open** | 168 | +1.95 | +3.93 | +7.21 [−0.19, +15.05] | **+18.01 [+7.01, +29.37]** 62 % | +22.03 [+4.97, +39.30] |
| B1 | late morning | 175 | −1.09 | −1.69 | −1.68 | −1.17 | −1.87 |
| B1 | lunch | 173 | −0.06 | −0.25 | −1.11 | −0.34 | +1.73 |
| B1 | power | 193 | −1.08 [−2.36, −0.01] | +0.31 | −0.12 | −1.35 | −4.72 |
| B2 | open | 20 | +7.12 [+4.89, +9.74] | +6.25 | +1.52 | −4.70 | −15.97 |

B1 at the open, 120 s, per day: +12, +31, +7, +8, +23, **−45** (7 Jul), +19, +45, +37, +37 c: **9 of 10 days positive**.

- **The baselines are flat** (momentum and fade ±0.2 c at every horizon): the method has no built-in bias; random 15 s directions earn nothing.
- **Expectation wrong on the horizon:** B1's value is not at 10-60 s (I expected +0.2 to +0.5 c there; it is about 0 at 10 s) but at **2-5 minutes**. In 7d-2 the 1 s / 10 s MM gap was the small, early part of a move that takes minutes.
- **Expectation wrong on the size at the open:** I expected +0.5 to +1.5 c; it is +18 c at 2 min, about 7× the round trip, with 62 % of events beating the cost and 9 of 10 days positive. The open is when AAPL moves most, so cents scale up; still, the other sessions are about 0, not smaller versions of it.
- **Outside the open, B1 is worth nothing** to a taker (late morning, lunch, power all about 0 or slightly negative).
- **B2 as coded goes the wrong way at 2 min** (−4.7 c whole day, interval off 0). The pullback to the 1 min EMA 20, once the 9 / 20 / 50 stack is on, is too late: the trend is mature and the "pullback" keeps going. Not Brooks' H2 yet; the EMA-stack gate is the suspect.
- **Read with care:** the open is a subgroup found in 7d-2 on the same 10 days, so this is the same phenomenon seen again, not an independent confirmation. Events overlap (about 17 per open, often seconds apart); the 9/10-day count is the more honest strength measure. 60 cells: a few look real by chance.
- **What it says about the founder's question:** the price-action edge, if real, lives at **1-5 minutes**, not at the seconds an MM holds. That points to a mid-speed taker version ("B1 at the open, hold about 2 min"), confirmed on the hold-out before believing it.
- **Next:** a realistic taker backtest of exactly that rule on the dev days (cross the spread after the bar closes, with latency; one position at a time; exit at the bid after 2 min), then freeze it and confirm on new days.

## Round 7f: taker backtest, B1 at the open

Written 25 Nov 2026, **before** running. Same 10 days (the ones 7d-2 and 7e found the open on: not independent).

- **Rule (the frozen candidate, D65):** from 09:30 to 10:30, when a 15 s bar completes B1, a market order for 100 shares in B1's direction reaches the book 100 ms after the bar's end and takes what the real book offers (best price first, then the next ticks). 2 minutes after the entry the exit is sent; it arrives 100 ms later and takes the other side. One position at a time: reads while entering or holding are ignored. Fee 0.3 c a share each way.
- **Primary:** `b1_open` (100 ms, 2 min). **Sensitivity, not for picking:** 1 ms latency; 1 min and 5 min holds.
- **Measured (`taker_summary.py`):** trades, win rate after fees, mean net cents a share (interval: trades resampled within days), days positive, worst day and worst trade, total dollars on 100 shares.
- **My expectation (Claude):** fewer trades than events (about 8-10 a day instead of 17, one at a time). The open's spread is wider than the day's (2-4 c) and the sweep adds a bit, so less than the event study's +18 c: net **+8 to +15 c a share**, win rate about 55 %, **7-9 of 10 days positive**, 7 Jul the worst day (around −$50 to −$100 on 100 shares). Total over 10 days about +$100-150 on 100 shares. Latency 1 ms vs 100 ms: almost no difference (the edge is minutes long). 1 min hold: smaller (+3 to +6 c); 5 min: about the same, noisier.
- **What it decides:** if the net holds up here, freeze the rule exactly and confirm on new days (hold-out, a paid Databento pull: price and the founder's yes first). If the fills eat it, B1 stays an MM input only.
- **Founder's expectation:** chose to run it; thinking of a setup library across common stocks (opening fakeouts and the like).

**Result (25 Nov 2026, `research/run_taker.sh` + `research/taker_summary.py`, 2000 resamples):**

| run | trades | win (net) | net c / share [95 %] | days + | worst day ($, 100 sh) | worst trade (c / sh) | total $ |
|---|---|---|---|---|---|---|---|
| **b1_open** (100 ms, 2 min) | 96 | 57 % | **+1.43 [−10.52, +12.43]** | 7/10 | −460.93 (7 Jul) | −240.3 | +137.12 |
| b1_open_1ms | 96 | 56 % | +1.39 [−10.50, +12.53] | 7/10 | −472.10 | −238.4 | +133.89 |
| b1_open_hold60 | 117 | 45 % | −1.14 [−8.61, +5.78] | 5/10 | −398.08 | −184.1 | −133.38 |
| b1_open_hold300 | 68 | 51 % | +6.76 [−11.30, +25.44] | 6/10 | −237.30 | −193.6 | +460.01 |

Per day (b1_open): +121, −202, +31, +27, −86, **−461** (7 Jul), +115, +300, +53, +238 $.

Where the event study's +18 c went:
- **The traded signals moved less:** +8.9 c mean (median +16) mid to mid in 2 min. One position at a time trades the first read of each cluster; the event study counted every read, and the later ones in a cluster (the price poking again and again) moved more.
- **The open is expensive:** entry 3.5 c worse than the mid at the signal (latency, a wide opening spread, the sweep), about the same on exit, plus 0.6 c fees: **about 7 c a round trip**, not the 1.6-2.6 c the event study assumed (that spread came from the whole day's fills).
- **The open is wild:** single trades of ±$1-2.40 a share in 2 minutes (worst: 3 Mar 09:36, −$2.40; 7 Jul three of the worst six). With 96 trades the interval is ±11 c around +1.4.
- **Latency doesn't matter** (1 ms ≈ 100 ms), as expected: the edge, if any, is minutes long.
- **Expectations wrong:** I expected +8 to +15 c net and 7-9 days positive; it is +1.4 c (median +7.4), 7 days, and not distinguishable from 0. Right about latency and roughly about the trade count (9.6 a day).
- **Conclusion for the founder's question, so far:** neither use of price action shows an edge we can tell from zero on these 10 days. In the MM, B1 separates fills by a fraction of a cent at 1-10 s (7d-2); as a taker at the open the move is there in mid terms but the open's cost and volatility swallow it. Not "negative", but not yet anything.
- **Ideas this raises (new hypotheses, not tuning on these days):** skip the first minutes (the worst trades cluster at 09:30-09:37 when the spread is widest), enter passively at the touch instead of crossing, a stop on the tail. Each would need its own entry and fresh days.

## Round 7d-3: the market maker pulls the side B1 reads against

Written 25 Nov 2026, **before** running. Same 10 days, 0 µs, naive top price (the second half of the founder's question: can price action improve the current system?).

- **Rule (`PullOnRead`, D66):** the naive quotes, but while B1 reads down there is no bid, while it reads up no ask. Two runs: all day (`pullb1_0us`) and only 09:30-10:30 (`pullb1open_0us`). When the read ends the quote rejoins at the back of the queue.
- **Measured:** `compare_runs.py naive_0us pullb1_0us pullb1open_0us`: ¢/share (spread + adverse) at 1 s and 10 s, pulled minus naive within days, block bootstrap; shares; P&L.
- **Primary tests (2):** each run minus naive, ¢/share at 10 s, whole day (Bonferroni over the 2).
- **My expectation (Claude):** small. B1 is in force on about 15 % of the shares and only the side against it is pulled, so about 7 % of shares go; 7d-2 put those 0.2 ¢ (1 s) to 0.8 ¢ (10 s) worse than the rest, so the whole day moves by about +0.02 to +0.05 ¢ a share, inside the noise; lunch (where B1 was the wrong way round) pulls good fills too. Losing queue place when the quote comes back costs a little. Open only: a smaller share of the day, about +0.01 to +0.03 ¢ whole-day. Shares −3 to −7 % (all day). **0 of 2 primary real.**
- **What it decides:** if it's not real here either, neither use of B1 shows an edge on these days, and the founder's rule applies (turn toward the discretionary direction). If it is, B1 earns its keep in the MM.
- **Founder's expectation:** chose to run it.

**Result (25 Nov 2026, `research/compare_runs.py naive_0us pullb1_0us pullb1open_0us`, 2000 resamples):**

| run | shares | 1 s diff vs naive | 10 s diff (corrected) | worse 10 s | P&L 10 days | net ¢/sh (10 s) | spread / adverse / inventory ¢ |
|---|---|---|---|---|---|---|---|
| naive | 8.63 M | | | | −$25,392 | −0.29 | 1.16 / −1.49 / +0.04 |
| pull B1 all day | 7.55 M (−12.5 %) | −0.00 [−0.02, +0.01] | **−0.04 [−0.08, +0.01]** noise | 7/10 | **−$19,723** | −0.26 | 1.14 / −1.51 / **+0.11** |
| pull B1 open only | 8.23 M (−4.6 %) | +0.01 [−0.00, +0.01] | **−0.02 [−0.05, +0.01]** noise | 5/10 | **−$19,765** | −0.24 | 1.13 / −1.48 / **+0.11** |

Per day, P&L minus naive (day bootstrap, 5000 resamples; **not the pre-registered measure**):
- all day: +695, +1875, +10, +662, +787, +271, +316, +1135, −68, −15 → mean **+$567 a day [+244, +932]**, 8/10 days better.
- open only: +1098, +1478, −44, +11, +492, +254, +315, +821, +756, +445 → mean **+$563 a day [+296, +847]**, 9/10 days better.

- **The primary tests: 0 of 2 real, as expected.** Spread + adverse selection at 10 s doesn't move (slightly worse, noise): the fills we skip are not worse at 10 s by enough to show.
- **But the P&L improves by about 22 %, and the gain sits in the inventory part** (+0.04 → +0.11 ¢ a share): pulling the bid while B1 reads down keeps us from getting long into a fall that takes minutes (the event study's 2-5 min horizon). The 10 s markout can't see a move that long; the P&L's inventory part does.
- **The open alone gives the whole gain** with a third of the lost volume (−4.6 % vs −12.5 % shares) and 9 of 10 days better. The rest of the day adds nothing net (lunch, where B1 was the wrong way, gives some back).
- **Read with care:** P&L per day was not the primary measure; I chose it after seeing the totals. The inventory part is noisy (it depends on the position at each move), and 10 days give a rough day bootstrap. It is the best candidate so far, not a finding: freeze it ("pull the side B1 reads against, 09:30-10:30") with **P&L per day vs naive as the primary**, and confirm on new days.
- **The system still loses money** (−$19.8K over 10 days, before maker rebates; D36: at about 0.2 ¢ a share of rebate the naive run would be about −$8K and this one about −$3K). B1 makes the market maker less bad, not good.
- **Expectation partly wrong:** right that the 10 s measure shows nothing and the volume falls; wrong to think the effect would be negligible overall: I looked only at the 10 s markout and missed the inventory channel.
- **Answer to the founder's question (25 Nov), on these 10 days:** price action *can* improve the current system, through the market maker's position at the open, by about $560 a day on 100-share quotes; it doesn't make it profitable, and it isn't confirmed yet. As a taker the same read is not distinguishable from 0 after costs.

## Hold-out H1: pull the side B1 reads against at the open, on new days (frozen before buying)

Written 26 Nov 2026, **before** the hold-out data was bought (founder's yes for $5.86, 26 Nov). Tested once; no change after the result, whatever it is.

- **Rule, frozen:** `mm_sim` at code `ac6740e`, naive (join the touch), 0 µs, `--pull-read b1 --read-window 09:30-10:30` (run `pullb1open_0us`) vs plain naive (`naive_0us`). B1 exactly as calibrated in 7d-2 (D63): SwingHigh/SwingLow/DayHigh/DayLow levels, poke a new high/low of 8 bars, poke ≤ 0.25 × 1m ATR, close back within the poke bar or the next 2, held 45 s. No parameter touched.
- **Days:** the 10 hold-out days of D53, 9-20 Nov 2026, 08:00 UTC to 02:00 UTC next day, each passing `convert_days.sh`'s replay check.
- **Primary test (1, so 95 % with no correction):** AAPL, P&L per day of pullb1open_0us minus naive_0us (inventory at the 16:00 mid, no fees or rebates), mean over the 10 days, day bootstrap 95 % interval (5000 resamples, seed 0). **Pass: the lower end is above 0.** Script: `research/holdout_compare.py` (sha256 14c80029…cd026); on the development days it gives +563 [+296, +846], 9/10.
- **Replication (secondary, does not change the AAPL verdict):** MSFT, same 10 days, same rule and settings unchanged (100-share quotes, 500 max position), same measure. Replicates if its lower end is above 0 too.
- **Reported alongside, not tested:** days better, the spread / adverse / inventory split, shares lost.
- **My expectation (Claude):** smaller than on the development days. +$563 was the best of the runs I looked at and P&L per day was picked after seeing the totals (winner's curse), and the hold-out days are quieter (cheaper, less data) so fewer dollars move. I expect AAPL around +$100 to +$350 a day, 6-8 of 10 days better, and **about a 40-50 % chance of passing**. MSFT: smaller still, about 1 in 3 to pass; B1 was never looked at there.
- **What it decides:** pass on AAPL → B1 at the open is a confirmed (small) improvement to the market maker, and the system remains loss-making before rebates. Fail → nothing price action did here survives new days, and per the founder's rule (25 Nov) the work turns toward the discretionary direction / setup library, on fresh development days.

**Result (26 Nov 2026, run once; `holdout_compare.py`, sha256 unchanged; code `ac6740e`; all 20 days pass the replay check; bought for $5.857, 1.7 GB):**

| stock | naive P&L 10 days | pull B1 open P&L | mean diff $/day [95 %] | days better | verdict |
|---|---|---|---|---|---|
| **AAPL (primary)** | −$13,914 | −$10,401 | **+351 [−13, +769]** | 7/10 | **FAIL** (lower end −13, misses 0 by $13) |
| MSFT (replication) | −$13,681 | −$13,359 | +32 [−1,403, +1,496] | 6/10 | FAIL (pure noise: daily swings ±$5K) |

- AAPL per day: +145, +1959, −20, +115, −369, +493, −461, +768, +420, +462. One big day (10 Nov, +$1,959); without it the mean is about +$176.
- AAPL split (¢/share): naive 1.73 / −1.90 / −0.12, pull 1.71 / −1.87 / −0.06: again the gain sits in inventory (−0.12 → −0.06), the same channel as on the development days. Shares −2.2 %.
- MSFT: spread 3.94 ¢ and adverse −4.99 ¢ a share, three times AAPL's; inventory swings dominate each day, so 10 days can't tell +$32 from anything.
- **The pre-registered test fails.** The rule is not confirmed. The point estimate (+$351) and 7/10 sit in the range I expected (+100 to +350, 6-8 days), so the direction held and shrank about as feared (winner's curse: +563 → +351), but 10 days are too few to separate an effect this size from 0.
- **These hold-out days are now spent.** They must not be used to re-tune or re-test B1. A second confirmation needs **new, never-seen days** and a fresh entry in the budget (pre-registered before buying, e.g. 20 AAPL days to halve the interval's width relative to the effect).
- **Expectation check:** AAPL right in size and days, pass chance 40-50 % → failed by $13; MSFT right (noise).
- **Against the founder's rule (25 Nov):** price action did not *prove* it improves the current system on new days. It came close; whether to spend one more fresh-day test on it or turn to the discretionary direction is the founder's call.

## Ideas to try later (not run yet)

- ADX as a second trend-strength measure next to ER (founder, 24 Nov 2026: "denemekten zarar gelmez"). Same place as ER: C++ indicator per timeframe, a column per fill, then the signal study. Compare ER vs ADX on the same days.
- A setup library across common stocks (founder, 25 Nov 2026): a bot with case-specific edges by timeframe and daily price action (opening fakeouts and the like). Each setup = detector + event study + taker backtest + hold-out, with its own entry in the test budget; replication across symbols as the honest check.
