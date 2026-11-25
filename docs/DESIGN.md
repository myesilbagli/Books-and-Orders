# Design

A limit order book and matching engine in C++20 (`cpp/`), and on top of it a market-making simulator and strategy research (`research/`).

## Goal

Build a market-making model that uses discretionary market reading (trend days vs range days) as a signal, and test it honestly: replay real Nasdaq ITCH days order by order, put the strategy's quotes into the real queue, and count a fill only when the queue ahead of it is used up. Success is measured with markouts and a P&L split into spread captured, adverse selection and inventory.

What has come out so far, on one page: [RESULTS.md](RESULTS.md).

Why this order: the simulator is only as honest as the book underneath it. Most retail backtests assume a fill the moment price touches a level; a queue-aware replay is what removes that bias.

## Components

| Component | File | Status |
|---|---|---|
| Core types (`Price`, `Quantity`, `OrderId`, `Sequence`, `Side`, `Order`) | `cpp/include/orderbook/order.hpp` | done |
| Order book: add, cancel, best bid/ask, queue per price | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| Matching: `submit` with price-time priority, `Trade` records | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| ITCH 5.0: big-endian byte readers | `cpp/include/orderbook/itch/bytes.hpp` | done |
| ITCH 5.0: message decoder (`A F E C X D U P Q R H`) | `cpp/include/orderbook/itch/messages.hpp`, `cpp/src/itch/messages.cpp` | done |
| ITCH 5.0: message splitter (`ItchReader`, zero-copy) | `cpp/include/orderbook/itch/reader.hpp` | done |
| ITCH 5.0: memory-mapped file (`MappedFile`, RAII) | `cpp/include/orderbook/itch/mapped_file.hpp`, `cpp/src/itch/mapped_file.cpp` | done |
| ITCH 5.0: `scan` (count types, check decoders) + `itch_stats` tool | `cpp/include/orderbook/itch/scan.hpp`, `cpp/src/itch/scan.cpp`, `cpp/tools/itch_stats.cpp` | done |
| Book: `reduce` (ITCH E/C/X) and `replace` (ITCH U) | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| Replay: one book per stock (by locate) + problem counters | `cpp/include/orderbook/replay.hpp`, `cpp/src/replay.cpp` | done |
| Real-day replay run (`itch_replay`): checks + speed | `cpp/tools/itch_replay.cpp` | done, full day (D23) |
| Simulator: virtual order queue position + fill rules | `cpp/include/orderbook/sim/queue.hpp`, `cpp/src/sim/queue.cpp` | done (4a) |
| Simulator loop: naive strategy, latency, requoting, fills, P&L | `cpp/include/orderbook/sim/simulator.hpp`, `cpp/src/sim/simulator.cpp` | done (4b) |
| Markouts + `mm_sim` tool; first simulation on real data | `cpp/include/orderbook/sim/markout.hpp`, `cpp/src/sim/markout.cpp`, `cpp/tools/mm_sim.cpp` | done (4c), full day (D27) |
| Volatility estimator (sigma^2 for Avellaneda-Stoikov) | `cpp/include/orderbook/sim/volatility.hpp`, `cpp/src/sim/volatility.cpp` | done (5a) |
| Strategy interface + naive strategy, split from the simulator | `cpp/include/orderbook/sim/strategy.hpp` | done (5b) |
| Avellaneda-Stoikov strategy + `mm_sim --as` | `cpp/include/orderbook/sim/avellaneda_stoikov.hpp`, `cpp/src/sim/avellaneda_stoikov.cpp` | done (5c), full day (D31) |
| Queue-imbalance study + `imbalance_study` tool | `cpp/include/orderbook/sim/imbalance.hpp`, `cpp/src/sim/imbalance.cpp`, `cpp/tools/imbalance_study.cpp` | done (5d-a), D32 |
| Book context on every fill (touch share, shares left) + grouped markouts in `mm_sim` | `cpp/include/orderbook/sim/markout.hpp`, `cpp/src/sim/simulator.cpp`, `cpp/tools/mm_sim.cpp` | done (5d-b1), D33 |
| Pull-when-thin wrapper, `mm_sim --pull`, quote message count | `cpp/include/orderbook/sim/pull_when_thin.hpp`, `cpp/include/orderbook/sim/simulator.hpp`, `cpp/tools/mm_sim.cpp` | done (5d-b2), D34 |
| P&L breakdown (spread / adverse selection / inventory) | `cpp/include/orderbook/sim/pnl.hpp`, `cpp/src/sim/pnl.cpp`, `cpp/tools/mm_sim.cpp` | done (5a), D35 |
| Maker rebate (`mm_sim --rebate`) | `cpp/tools/mm_sim.cpp` | done (5b), D36 |
| Fills CSV + block-bootstrap intervals (`research/bootstrap.py`) | `cpp/include/orderbook/sim/fills_csv.hpp`, `cpp/src/sim/fills_csv.cpp`, `research/bootstrap.py`, `research/test_bootstrap.py` | done (5c), D37 |
| Milestone 5 results page + reproduce script | `docs/RESULTS.md`, `research/run_m5.sh` | done (5d) |
| Time bars (OHLCV from the mid) for the indicators | `cpp/include/orderbook/sim/bars.hpp`, `cpp/src/sim/bars.cpp` | done (6b-1), D38 |
| Indicators: SMA, EMA, Bollinger, RSI, Stochastic RSI, VWAP (streaming) | `cpp/include/orderbook/sim/indicators.hpp`, `cpp/src/sim/indicators.cpp` | done (6b-2), D39 |
| Trades from ITCH (E, printable C, P, Q) + `ChartFeed` (bars + indicators per timeframe) | `cpp/include/orderbook/sim/trades.hpp`, `cpp/src/sim/trades.cpp`, `cpp/include/orderbook/sim/chart_feed.hpp`, `cpp/src/sim/chart_feed.cpp` | done (6b-3a), D40 |
| Chart CSV + `chart_dump` tool (1 s and 1 min bars with indicators, for checking against a chart) | `cpp/include/orderbook/sim/chart_csv.hpp`, `cpp/src/sim/chart_csv.cpp`, `cpp/tools/chart_dump.cpp` | done (6b-3b), D41 |
| Cross-check of bars and indicators against Databento | `research/check_chart.py` | done, D42 |
| One-stock ITCH extract (`itch_extract`): same format, runs in seconds | `cpp/include/orderbook/itch/extract.hpp`, `cpp/src/itch/extract.cpp`, `cpp/tools/itch_extract.cpp` | done (6c-0), D43 |
| `MarketState` (1 s + 1 min charts, VWAP) shown to the strategy; `chart_dump` uses the same class | `cpp/include/orderbook/sim/market_state.hpp`, `cpp/src/sim/market_state.cpp`, `cpp/include/orderbook/sim/strategy.hpp`, `cpp/src/sim/simulator.cpp` | done (6c-1), D44 |
| Chart snapshot on every fill (1 s + 1 min indicators, VWAP) + 15 columns in the fills CSV | `cpp/include/orderbook/sim/markout.hpp`, `cpp/src/sim/simulator.cpp`, `cpp/include/orderbook/sim/fills_csv.hpp`, `cpp/src/sim/fills_csv.cpp` | done (6c-2a), D45 |
| Local research hub, phase 1: candles, EMA 9/21, VWAP, Bollinger, Stoch RSI, our fills colored by markout; exporter to JSON | `hub/web/`, `research/hub_export.py`, `research/test_hub_export.py` | done (6c-2b), D46 |
| Hub: market structure (ZigZag, HH/HL labels, shading) on 1 s / 15 s / 1 min / 5 min, swing size c picked by eye; `export_hub.sh` | `hub/web/src/lib/structure.ts`, `hub/web/src/components/MarketChart.tsx`, `research/export_hub.sh`, `research/hub_export.py` | done (round 2, 2b), D50 |
| Signal study: do chart readings (EMA trend 1m / 1s, VWAP) pick out the losing fills? (stage 1) | `research/signal_study.py`, `research/test_signal_study.py` | round 1 run (private log), D47 |
| Signal study round 2: market structure A / B / C, c per timeframe | `research/signal_study.py --round 2`, `cpp/include/orderbook/sim/market_state.hpp` | built, not yet run, D51 |
| ATR + market structure: ZigZag swings scaled by ATR, HH/HL score (round 2) | `cpp/include/orderbook/sim/indicators.hpp`, `cpp/include/orderbook/sim/structure.hpp`, `cpp/src/sim/structure.cpp` | done (6c-2a-1), D48 |
| Structure in every chart (1 s, 15 s, 1 min, 5 min), on every fill, in the CSVs; `chart_dump --c --swings` | `cpp/include/orderbook/sim/chart_feed.hpp`, `cpp/src/sim/market_state.cpp`, `cpp/src/sim/fills_csv.cpp`, `cpp/src/sim/chart_csv.cpp`, `cpp/tools/chart_dump.cpp`, `research/hub_export.py` | done (6c-2a-2), D49 |
| Databento MBO → ITCH 5.0 converter, validated on the dev day | `research/dbn_to_itch.py`, `research/test_dbn_to_itch.py` | done, D52 |
| The 10 development days (2026), converted and replay-checked | `research/convert_days.sh`, `research/pick_days.py` | done, D53, D54 |
| Baseline on the 10 development days, one line per day (6d-1) | `research/run_days.sh`, `research/day_summary.py` | done, D55 |
| Signal study over several days: within-day difference, stratified bootstrap, days worse (6d-2) | `research/signal_study.py` (`study_days`) | done, D56 |
| Efficiency ratio (10 bars) on every chart, every fill, both CSVs (6e-1) | `cpp/include/orderbook/sim/indicators.hpp`, `cpp/src/sim/chart_feed.cpp`, `cpp/src/sim/chart_csv.cpp`, `cpp/src/sim/fills_csv.cpp` | done, D57 |
| Hub: the 10 development days, ER pane, trend/range shading from structure + ER with two threshold sliders (6e-2) | `hub/web/src/lib/regime.ts`, `hub/web/src/components/MarketChart.tsx`, `hub/web/src/App.tsx`, `research/export_days.sh`, `research/export_hub.sh`, `research/hub_export.py` | done, D58 |
| Where the loss comes from: queue (level broke) and trend (hindsight leg, confirmed leg, structure, strong trend), round 7a | `research/fill_anatomy.py`, `research/test_fill_anatomy.py` | done, D59 |
| Ladder: orders at the touch and levels − 1 ticks behind, each kept in its queue; `mm_sim --levels`; runs compared paired by day and block (7b-1) | `cpp/include/orderbook/sim/simulator.hpp`, `cpp/src/sim/simulator.cpp`, `cpp/tools/mm_sim.cpp`, `research/run_days.sh`, `research/compare_runs.py` | done, D60 |
| Delayed-oracle curve: the hindsight leg learned L seconds late, for L = 0 to 300 s (7c) | `research/oracle_curve.py`, `research/fill_anatomy.py` (`delayed_leg`) | done, D61 |
| Real-time reads placed on the delayed-oracle curve: equivalent lag, coverage, agreement with the hindsight leg (7d-1) | `research/turn_reads.py`, `research/test_turn_reads.py` | done, D62 |
| Price levels for the turn reads: last swings, day open/high/low, VWAP, EMA 20, round 50 ¢; EMA 20 and 50 on every chart (7d-2, step 1) | `cpp/include/orderbook/sim/levels.hpp`, `cpp/src/sim/levels.cpp`, `cpp/include/orderbook/sim/chart_feed.hpp` | done, D63 |
| B1, failed breakout at a level: poke ≤ ¼ ATR, close back within 2 bars, read held 45 s (7d-2, step 2) | `cpp/include/orderbook/sim/turn_reads.hpp`, `cpp/src/sim/turn_reads.cpp` | done, D63 |
| Trend/range signal for the market maker | — | milestone 6 |

## Data flow

```
order events (tests now; ITCH from milestone 2)
        │
        ▼
OrderBook::add / cancel          (replay: rest as told, no matching)
OrderBook::reduce / replace      (replay: ITCH E/C/X shrink in place, U re-queues)
OrderBook::submit → [Trade...]   (our own orders: match first, rest the remainder)
        │
        ├── bids_  std::map<Price, std::list<Order>, std::greater>   best = begin() = highest
        ├── asks_  std::map<Price, std::list<Order>>                 best = begin() = lowest
        └── index_ std::unordered_map<OrderId, {side, price, list iterator}>
```

The book holds only **displayed** orders. Nasdaq ITCH does not publish hidden, midpoint or the reserve part of iceberg orders; they show up only when they trade. Displayed orders keep priority over non-displayed ones at the same price, so queue position at a price is still correct, but hidden liquidity at a better price can take fills we would otherwise expect (the simulator in milestone 4 will be slightly optimistic for that reason).

## Known limitations

- **One venue.** ITCH is Nasdaq's own book. The same stocks also trade on NYSE Arca, Cboe, IEX, NYSE and off-exchange (dark pools, broker internalization), so Nasdaq's prints are only part of total volume. On the first test day QQQ showed about 655 K shares on Nasdaq by 09:44 ET, a fraction of its consolidated volume. Prices line up across venues (arbitrage keeps them together), so price charts are right; volume is not the whole market.
- **Why that's acceptable for the simulator:** a resting quote sits in one venue's queue, and its queue position depends only on that venue's book. Simulating on Nasdaq's book is the right model for "when do I get filled".
- **What it misses:** a move that starts on another venue can reach Nasdaq's book after it has already traded elsewhere (adverse selection we can't see coming), and real market makers quote on many venues at once (routing, inter-venue latency, Reg NMS order protection). Multi-venue simulation is out of scope.
- **Hidden liquidity:** see Data flow above. Non-displayed orders only appear when they trade (`P` messages), so fills are slightly optimistic.
- **Plan for the signal (milestone 6):** trend vs range labels and order-flow features come from **consolidated** data (all venues' trades and best prices), which is much smaller and cheaper than order-by-order data. The quotes themselves are still simulated on Nasdaq's book.
- **No stop loss, by design for now (22 Nov 2026).** Risk control is only the inventory limit and passive exit (our ask waits to be hit). The simulator can't take liquidity, so there's no "cross the spread to get out". On the dev day it wouldn't pay on average: about 90 % of the adverse move is over within 1 s (D35) and the drift after 10 s is ~0.16 cents a share, while crossing costs a spread plus the taker fee (~1.5 cents). For a market maker a stop buys a smaller tail at the cost of the spread; that matters on trend days. **Milestone 6:** add an aggressive exit (by price move or position age) together with the trend/range signal, and measure it on trend days.
- **One development day, in-sample results (decided 22 Nov 2026).** Everything so far runs on 30 Dec 2019, a quiet holiday-week day, and some choices were made by looking at it (H = 10 s, the reference variance). Results on it are in-sample: they show the system works and what each piece does, not that a strategy makes money. Milestone 5 builds the system; making it profitable comes after, and is checked on days held out until then (Nasdaq's public folder has 14 more days, 2018-2020; Databento for more), with confidence intervals on every comparison.

## Decisions

### D1. Prices are signed 64-bit integer ticks

- **Choice:** `using Price = std::int64_t`, measured in units of **$0.0001**, the same unit as ITCH 5.0 prices (4 implied decimals), so `1502778` = $150.2778 and ITCH prices are used as-is, with no conversion. (First set to $0.01; changed in milestone 2 because stocks under $1 trade in sub-penny increments, which cents can't represent.)
- **Why:** floating point can't represent most decimal prices exactly (`0.1 + 0.2 != 0.3`), so equality and ordering of prices become unreliable, and a book is all about equality and ordering. Integer compares are exact and one CPU instruction.
- **Alternatives:** `double` (inexact); a decimal class (exact but slower, extra code); 32-bit ticks (fits equities but risks overflow for other instruments or finer ticks).
- **Why signed:** price differences (spreads, P&L) can be negative; some instruments (spreads, some futures) trade at negative prices.

### D2. Quantities are unsigned 32-bit

- **Choice:** `using Quantity = std::uint32_t`.
- **Why:** a negative quantity is meaningless; 32 bits (about 4.29 billion) covers any single order; ITCH 5.0 uses 32-bit share counts, so no conversion later.
- **Cost:** unsigned subtraction wraps (`300 - 301 == 4'294'967'295`). Mitigation: quantity only decreases through `Order::fill`, which refuses an overfill and leaves the order unchanged. Tested in `OverfillIsRejectedAndOrderUnchanged`.

### D3. `Side` is an `enum class` with a 1-byte underlying type

- **Why:** no implicit conversion to `int`, so a side can't be compared with or passed as a number by mistake; `Order(…, true, …)` style calls are impossible. `std::uint8_t` keeps it to one byte.
- **Alternatives:** `bool is_buy` (unreadable at call sites); a plain `enum` (converts silently to `int`).

### D4. Time priority uses a sequence counter, not a timestamp

- **Choice:** `Sequence seq` (`uint64_t`), assigned when the order reaches the book; `arrived_before(a, b)` is `a.seq < b.seq`.
- **Why:** two orders can share a timestamp, and wall clocks can jump backwards; a counter is always strictly increasing, so priority is never ambiguous.
- **Strictness:** `<`, not `<=`, so an order never comes before itself (a strict ordering, which sorted containers require).

### D5. `Order` is a plain aggregate; `fill` returns `[[nodiscard]] bool`

- **Aggregate (no constructor):** callers use C++20 designated initializers (`Order{.id = 1, .side = Side::Buy, ...}`), so fields can't be passed in the wrong order.
- **Overfill handling:** return `false` instead of throwing or asserting. An overfill means a bug in the matcher, but returning `false` is testable, costs nothing on the normal path, and keeps exceptions off the hot path. `[[nodiscard]]` plus `-Werror` makes ignoring the result a compile error.
- **Alternatives:** `assert` (gone in Release builds, needs death tests); exceptions (slow when thrown, banned in many low-latency codebases).

### D6. Small hot-path functions stay in headers

- **Why:** `fill`, `is_filled`, `arrived_before` are one-liners on the hot path; the compiler can only inline what it can see. The book's larger functions live in `book.cpp`, built as a `STATIC` library.

### D7. Price levels in `std::map`, one per side, best price at `begin()`

- **Choice:** `bids_` sorted high to low (`std::greater`), `asks_` low to high (default). Best bid and best ask are each `begin()`, O(1).
- **Why:** always sorted, inserting a new price is O(log levels), and the code is short and obviously correct. Correct first; speed is measured later.
- **Alternatives:** sorted `std::vector` (cache-friendly, but inserting in the middle shifts elements); flat array indexed by price (O(1), what production engines use, but needs a bounded price range); both are optional optimizations later.
- **Cost:** the two maps are different types, so `cancel` has one branch per side.

### D8. Each level is a `std::list<Order>` (FIFO)

- **Why:** cancels mostly hit the middle of a queue. A list removes any element in O(1), and iterators to the other elements stay valid when something is added or removed, which the index depends on.
- **Alternatives:** `std::deque` / `std::vector` (removal from the middle shifts everything behind it and invalidates iterators); an intrusive list (no allocation per order; the optimization milestone).
- **Cost:** one heap allocation per order and poor cache locality.

### D9. `std::unordered_map<OrderId, Locator>` for cancel by id

- **Locator:** side, price, and a `std::list` iterator to the order. Cancel = hash lookup O(1) + level lookup O(log levels) + list erase O(1).
- **Alternatives:** searching the book (O(orders)); `std::map` by id (O(log n), no benefit since ids need no order).

### D10. The book validates and stamps arrival order

- `add` rejects a zero quantity or a duplicate id, and checks before changing anything, so a rejected add leaves the book untouched.
- The book overwrites `seq` with its own counter, so time priority never depends on the caller.
- An empty level is removed immediately; otherwise `best_bid()` / `best_ask()` would report a price with no orders.
- `volume_at` sums into `uint64_t`: many 32-bit orders can add up past 4.29 billion.

### D11. Two entry points: `add` (no matching) and `submit` (matching)

- **Why:** when replaying ITCH, Nasdaq has already matched; an `Add Order` message is an order that rests, and executions arrive as their own messages. Matching those adds again would invent trades. Our own simulated orders go through `submit`, which matches first.
- **Consequence:** `add` does not prevent a crossed book (best bid ≥ best ask). During replay a crossed book means a bug in our parsing or bookkeeping, so the replay milestone will check for it.

### D12. Price-time priority; trades at the resting price

- **Rule:** walk the opposite side from its best price; within a price, fill the oldest order first; stop at the incoming order's limit. The remainder rests at its limit price.
- **Trade price = the resting order's price.** The resting order was there first and set that price; the aggressor gets any improvement (willing to pay 100.05, pays 100.03).
- **Trade size = min(incoming remaining, resting remaining)**, so `fill()` can never fail inside matching; `assert` documents that in Debug, and costs nothing in Release.
- **Not handled yet:** self-trade prevention, market orders (a market order is a limit order at an extreme price), order types such as IOC. Added if the simulator needs them.

### D13. One `fill_against(Level&, …)` for both sides

- **Why:** `bids_` and `asks_` are different map types, but their levels are the same `std::list<Order>`. Matching inside a level is the part with the logic, so it is written once; only the outer "walk the best levels" loop is duplicated per side (a few lines each). The alternative, a function template over the map type, removes that duplication at the cost of a new language feature.
- **Validation before matching:** `submit` rejects a zero quantity or a duplicate id before trading anything, so a rejected order never leaves partial trades behind.

### D14. ITCH integers are assembled byte by byte

- **Why:** ITCH is big-endian (most significant byte first); ARM and x86 are little-endian, so the raw bytes can't be used as a number. `read_be` shifts in one byte at a time.
- **Alternatives:** casting a struct over the buffer (`reinterpret_cast`) is undefined behaviour in C++ (strict aliasing, misaligned reads) and still has the wrong byte order; `memcpy` + `std::byteswap` needs C++23.
- **Cost: none.** With `-O2` each fixed-width reader compiles to one load plus one `rev` (byte-reverse) instruction on arm64, verified in the generated assembly.
- **Bytes are `uint8_t`, never `char`:** whether `char` is signed depends on the compiler, and a signed `0xFF` would turn into -1 and corrupt the value. Tested in `HighBitBytesStayUnsigned`.
- **No bounds check per read:** the caller checks the message length once; the readers stay branch-free.

### D15. One decode function per ITCH message type, returning `std::optional`

- **Choice:** `decode_add_order`, `decode_order_delete`, … each take the whole message (pointer + length) and return a plain struct, or `std::nullopt` if the message doesn't fit. The caller (the file reader) looks at the type byte and picks the function with a `switch`.
- **Why:** the simplest design with no new machinery; each decoder is tested on its own with hand-built bytes.
- **Alternatives:** one `decode()` returning a `std::variant` of all message types (one entry point, but a heavier language feature and every caller has to unpack it); a handler that the parser calls back (`on_add`, `on_delete`, …), common in HFT because nothing is returned or copied, but it needs templates or virtual functions. Moving to a handler later is easy, since it would just call these decoders.
- **Validation:** before reading any field, the length must equal that type's length *and* the type byte must match (`A` and `C` are both 36 bytes, so the length alone can't tell them apart). A side byte other than `B`/`S`, or a printable flag other than `Y`/`N`, is rejected too. A bad message is never half-decoded.
- **`A` and `F` share one struct:** `F` only adds the firm's MPID (e.g. `GSCO`); for `A` it is four spaces. The book treats them the same.
- **Text is `std::array<char, N>`, not `std::string`:** fixed size, no heap allocation, and one day's file holds tens of millions of messages.
- **`P` (non-cross trade) is decoded but doesn't change the book:** it reports a trade with a hidden order, which was never on the book. It matters for volume and, later, for spotting hidden liquidity.

### D16. The reader walks bytes in memory; the file gets there by `mmap`

- **Choice:** `ItchReader` takes a pointer and a size, and `next()` hands out each message as a pointer into those bytes (zero-copy). It knows nothing about files. The file will be memory-mapped (`mmap`): the OS shows the whole file as one array and loads 16 KB pages only when they're touched, so a ~10 GB day never has to fit in RAM at once.
- **Why:** no copy per message, no allocation, and the code is a short loop over an array. Tests hand it a small byte vector; the real run hands it the mapped file. Same code either way.
- **Alternatives:** loading the whole file into a buffer (10 GB of RAM for nothing); streaming with `std::ifstream` into a reused buffer (portable, but one copy per message and more bookkeeping at chunk boundaries); reading through zlib straight from the `.gz` (saves ~10 GB of disk, adds a dependency and a copy).
- **Speed, honestly:** for a front-to-back read, `mmap` and large `read()` calls both run close to disk speed; mmap's gain is the missing copy and the simpler code. Reading isn't the bottleneck anyway: splitting and decoding costs a few ns per message, while the book's `std::map`/`std::list` updates cost far more. A second run is much faster because the file stays in the OS page cache, so benchmarks must report cold and warm runs separately.
- **Errors are explicit:** `End` (clean end between messages), `Truncated` (the file stops inside a length prefix or a message, typically a partial download), `Invalid` (a length of 0). After an error the position doesn't move: once we've lost track of where messages start, everything after is garbage, so the reader must not continue.
- **Overflow-safe bounds check:** `len > left - 2` instead of `pos + 2 + len > size`.
- **Market data stays out of git:** files live in `data/` (git-ignored). Nasdaq's sample days are free to download from `emi.nasdaq.com`, but large and not ours to republish.
- **Live trading is different:** a real feed arrives over UDP multicast (MoldUDP64), often through kernel-bypass network cards, not from a file. `mmap` is the replay and research tool.

### D17. `MappedFile` owns the mapping (RAII) and can't be copied

- **RAII:** the constructor opens and maps the file; the destructor unmaps it. Cleanup runs on every way out of a scope (normal end, early `return`), so it can't be forgotten.
- **Copies are deleted (`= delete`):** two objects owning one mapping would both unmap it (a double free). Deleting the copy constructor and copy assignment turns that bug into a compile error.
- **Failure is reported like `std::ifstream`:** `is_open()` plus an `error()` message (from `strerror`). Alternatives: throwing from the constructor (idiomatic and fine off the hot path, but the codebase has no exceptions so far); a factory returning `std::optional<MappedFile>` (a failure can't be ignored, but it needs move semantics, deferred until needed).
- **The descriptor is closed right after `mmap`:** the mapping keeps the file alive by itself, so the object only holds a pointer and a size.
- **Edge cases:** an empty file is open with size 0 (the OS refuses to map 0 bytes); a folder is rejected (`S_ISREG`).
- **Read-only and private (`PROT_READ`, `MAP_PRIVATE`), plus `madvise(MADV_SEQUENTIAL)`:** the OS reads ahead and may drop pages behind us. It's a hint; ignoring it changes speed, not results.
- **POSIX only:** macOS and Linux. No Windows support planned.

### D18. `scan` is a library function; `itch_stats` is a thin tool around it

- **Why split:** the counting and decode checks are tested with hand-built bytes like everything else; the tool only maps the file, calls `scan`, times it and prints. Exit code 0 only for a clean `End` and 0 decode failures, so a script can rely on it.
- **Counts are an array indexed by the type byte** (`count_by_type['A']`): 256 slots cover every byte value, so counting is a single add with no lookup, and unknown types are still counted.
- **Timing uses `steady_clock`**, which never jumps backwards (the same reason as D4).
- **First real-data check (30 Dec 2019, first 0.73 GB of the file, the part downloaded so far):** 22.7 M messages, **0 decode failures**, `Truncated` at the end as expected for a partial file. 8,906 stock directory (`R`) messages, one per listed stock. About 117 M messages/s, or 3.75 GB/s, **warm** (the file was just written, so it was in the page cache); a cold run will be slower.

### D19. `reduce` keeps queue position; `replace` loses it

- **`reduce(id, qty)`** (ITCH `E`, `C` executed; `X` partial cancel): changes the order's `remaining` in place through its index ticket, so it stays where it is in the queue. At 0 it is removed through `cancel`, which already cleans up empty levels. Unknown id, 0 shares, or more than what's left: rejected, book unchanged (`Order::fill` already refuses an overfill, D5).
- **`replace(old, new, qty, price)`** (ITCH `U`): cancel the old order, add the new one on the same side (U doesn't repeat it) at the back of its level, even at an unchanged price. That's Nasdaq's rule: a replace gives up time priority. Every check runs before anything changes, so a rejected replace leaves the old order in place.
- **Why it matters for market making:** queue position is what decides who gets filled first at a price. Shrinking an order keeps its spot; changing its price (or size via U) sends it to the back. The simulator's fills depend on getting this exactly right.
- **Alternative:** implement `reduce` as cancel + add of the smaller order. Simpler, but it would wrongly move the order to the back of the queue.

### D20. One book per stock in a vector indexed by stock locate, sized once

- **Choice:** `std::vector<OrderBook>` with 65,536 entries (every value a 2-byte locate can take), created in the constructor and never resized. Every book-changing ITCH message carries its stock's locate, so routing is `books_[locate]`: an array index, no hashing, no search. Symbols come from the `R` (stock directory) messages at the start of the day, kept in a parallel vector.
- **Why sized once:** when a vector grows it moves its elements to new memory, and if the element type can't be moved safely it copies them. A copied `OrderBook` would be corrupt: its index holds list iterators that point into the *old* book's lists. Never resizing makes that impossible. (Making `OrderBook` non-copyable would close the gap for all code; that needs move semantics, deferred.)
- **Cost:** 65,536 empty books exist even though about 9,000 stocks trade; an empty book is small, a few MB in total.
- **Alternative:** `std::unordered_map<std::uint16_t, OrderBook>`: holds only the stocks that trade, but costs a hash on every message.

### D21. Replay counts problems instead of stopping

- Counters, all expected to be 0 on a correct replay: `decode_failures`, `unknown_order` (an E/C/X/D/U for an order the book doesn't have: a missed or misread message), `rejected` (the book refused: duplicate id, more shares executed than the order has), `crossed` (after an add or replace, best bid ≥ best ask; the first one's stock and time are kept for investigation).
- **Why count rather than stop:** one bad message shouldn't hide what happens in the rest of the day, and the size of each counter says how big a problem is.
- **Locked counts as crossed:** bid = ask on one venue's displayed book is impossible in continuous trading, because those two orders would trade. Expected exceptions are periods without matching (halts, before an IPO opens), which the real run will show.
- **Only adds and replaces are checked for crosses:** removing shares can't make a book cross.

### D22. Crosses are checked only while a stock is trading

- **Finding:** the first replay of real data (30 Dec 2019, 04:00–10:15 ET, 55.4 M messages) showed 524 crossed or locked books. The first was PNRL at 08:00, and PNRL had been **halted** (`H` message, state `H`) since 03:09. Nasdaq accepts orders for a halted stock but matches nothing until it reopens, so its book can legitimately cross. 8 stocks started the day halted.
- **Choice:** the replay keeps each stock's trading state from `H` messages (`T` trading, `H` halted, `P` paused, `Q` quotation only). A cross while not trading goes to `crossed_not_trading` (expected); a cross while trading goes to `crossed` (a bug). Before any `H` message a stock counts as trading, the strict default.
- **Not just hiding a number:** the hypothesis was tested, not assumed. After the split, `crossed` is 0 and all 524 are in halted stocks.
- **Full day: 10 crosses while trading remain, all explained.** Every cross while trading is now logged with its stock and time. All 10 fall in the same second a stock resumed trading after a limit-up/limit-down pause (`P` with reason `LUDP` → `T`): MKD (listed that day, paused repeatedly) at 10:54:21, 11:21:52 and 11:47:19, and MBOT at 14:20:41. On resumption Nasdaq runs a reopening auction for the orders that built up during the pause, and the book stays crossed for microseconds until it prints. Not modelled; the count is kept as is, and every one has a known cause.

### D23. First replay of a real day: results

On the first 1.6 GB of 30 Dec 2019 (04:00 to 10:15 ET), release build, warm page cache:

| Check | Result |
|---|---|
| Messages | 55.4 M |
| Decode failures / unknown orders / rejected | 0 / 0 / 0 |
| Crossed while trading | 0 (524 while halted, see D22) |
| AAPL book at 10:15 vs last AAPL trade from an independent Python parser | bid 285.47 / ask 285.50; last trade 285.49, inside the spread |
| Resting orders at 10:15, all stocks | 1.73 M |
| Speed | 3.5 M messages/s, about 285 ns per message |
| Peak memory (RSS) | 2.0 GB, of which the mapped file accounts for about 1.6 GB |

- **The book is the bottleneck, as expected (D16):** scanning and decoding alone ran at 117 M messages/s; updating the books brings it down about 30×. Every new order allocates a `std::list` node, an `unordered_map` entry and often a `std::map` level. This is the baseline for the optimization milestone.
- **Full day (268.7 M messages, all 8.25 GB):** the file was checked with gzip's built-in CRC32 (`gzip -t`), since Nasdaq's md5 file was no longer available. `itch_stats`: 0 decode failures, clean `End`, 4.9 s. `itch_replay`: clean `End`; 0 decode failures, 0 unknown orders, 0 rejected; 10 crosses while trading, all at resumptions after a pause (D22); 4,767 while halted. **0 orders resting at the end of the day:** every order added during the day was removed by a later message, a strong end-to-end check that no add, execution, cancel or replace was missed. 5 M messages/s, about 55 s.

### D24. Virtual orders: exact queue position from L3 data, fills when the aggressor passes us

- **The order never enters the book.** The real market never saw it; we assume it wouldn't have changed anyone's behaviour (small size, no market impact). We only track where it *would* stand.
- **Queue position from the arrival counter (D4):** a virtual order takes the book's next `seq` without using it up. Every order resting at that moment has a smaller seq (ahead of us); the next order to arrive gets that same seq and later ones larger (behind us). So the rule is `seq < ours` ahead, `seq >= ours` behind. (The first version used `>`; the tests caught the off-by-one.)
- **Exact, not estimated:** with order-by-order data we know which cancels and executions are ahead of us. With price-level (L2) data a simulator has to guess whether a cancel came from in front of us or behind, which is the usual source of over-optimistic backtests.
- **Fill rule, checked before the book applies each execution:** we are filled when the aggressor got past us, meaning it traded (a) an order on our side and price that is behind us, or (b) our side at a worse price (a seller below our bid, a buyer above our ask). Each execution fills at most its own size: the shares that went to the order behind us would have come to us instead.
- **Not used for fills:** executions ahead of us (they just move us up), executions at a better price than ours, executions on the other side.
- **Book additions for this:** `find(id)` (read an order before an execution changes it), `next_sequence()`, and `volume_ahead(side, price, seq)`, which stops at the first later order because a level is in arrival order.

### D25. The simulator loop: look, apply, decide; latency on every action

- **Per message:** (1) quotes and pulls that have reached the exchange by this message's time go live; if the message executes an order in our stock, check our quotes against it *before* the book changes (the order may be removed); (2) the replay applies the message; (3) if it was our stock, the strategy decides where to quote.
- **No callback needed:** the simulator sees each message before the replay does, so it decodes `E`/`C` once more itself (a few ns) instead of the replay calling back into it. A callback or handler (D15) would be more flexible; this is simpler and needs no new language feature.
- **Latency applies to new quotes and pulls alike.** A change decided at `t` takes effect at `t + latency`; until then the old quote stays live and can still be filled. That is how a stale quote gets *picked off*: the price moves, we decide to follow, and in the meantime someone trades through our old price. Tested both ways (with and without latency) on the same message sequence.
- **Don't resend what's already on its way:** if the target equals where we're heading (pending, else live), do nothing. Resending would throw away queue position.
- **Naive strategy on purpose:** join the best bid and ask with a fixed size, follow them, pull a side at the inventory limit, quote only inside set hours and only in a two-sided, uncrossed market. It's the baseline later strategies are measured against.
- **Accounting:** inventory in shares; cash in ticks × shares (exact integers); P&L marks inventory at the latest mid, kept as `bid + ask` (twice the mid) so it stays an integer until the final conversion to dollars.
- **Not modelled yet:** exchange fees and maker rebates; fills from hidden (`P`) executions; auction (cross) mechanics, since `C` executions are treated like `E`.

### D26. Markouts measured on the stock's own mid series

- **Markout** of a fill `h` later: buy `mid(t+h) − price`, sell `price − mid(t+h)`, per share. Positive means the price went our way afterwards. At `h = 0` a fill at the touch shows about half the spread; how fast it falls as `h` grows is the adverse selection.
- **Mid series:** the simulator records our stock's mid only when it changes (`bid + ask`, twice the mid, to stay an integer). The mid at any time is the last change at or before it, found by binary search (O(log n)) since the series is in time order.
- **Quoting starts at 09:31**, not 09:30: the opening auction allocates shares by its own rules, which the continuous-book fill model doesn't capture.

### D27. First simulation: the naive strategy loses, as expected

AAPL, 30 Dec 2019, quoting 09:31 to 13:38 ET (the part of the file downloaded at the time), 100 shares per quote, inventory limit ±500, no fees or rebates:

| Latency | Fills | Shares traded | P&L | Markout (cents/share): 0 | 100 ms | 1 s | 10 s | 60 s |
|---|---|---|---|---|---|---|---|---|
| 0 | 5,115 | 297.7 K | −$3,027 | +0.59 | −0.46 | −0.75 | −0.89 | −1.22 |
| 50 µs | 5,008 | 293.0 K | −$2,731 | +0.54 | −0.57 | −0.88 | −1.12 | −1.22 |
| 1 ms | 5,466 | 330.2 K | −$3,910 | +0.29 | −0.75 | −1.07 | −1.20 | −1.38 |

- **What it shows:** at the moment of the fill we're up about half a spread (+0.5 to +0.6 cents), and within 100 ms that has turned into a loss. Joining at the back of the queue means we're mostly filled when the whole level is being taken out, which is when the price is about to move through it: textbook adverse selection.
- **Consistency check:** P&L ≈ shares traded × long-horizon markout (≈ 298 K × −1.0 cent ≈ −$3,000), so the accounting and the markouts agree.
- **Latency:** 1 ms is clearly worse: more fills (stale quotes picked off), a lower markout at the instant of the fill, and the worst P&L. Between 0 and 50 µs the difference is within noise for one stock and one morning.
- **Rebates wouldn't save it:** Nasdaq's maker rebate is a fraction of a cent per share, well short of the roughly 1 cent per share lost to adverse selection.
- **Full day (09:31–16:00 ET), same settings:**

| Latency | Fills | Shares traded | P&L | Markout (cents/share): 0 | 100 ms | 1 s | 10 s | 60 s |
|---|---|---|---|---|---|---|---|---|
| 0 | 8,140 | 468.5 K | −$3,796 | +0.55 | −0.42 | −0.65 | −0.85 | −1.01 |
| 50 µs | 8,111 | 469.8 K | −$3,366 | +0.50 | −0.50 | −0.75 | −1.01 | −1.00 |
| 1 ms | 8,618 | 509.2 K | −$5,046 | +0.30 | −0.68 | −0.92 | −1.17 | −1.21 |

  Same picture as the morning: about half a spread at the fill, turned into a loss of roughly a cent per share within seconds; 1 ms is clearly the worst. P&L is close to shares × 10 s markout (for 0 µs: −$3,969 vs −$3,796), the rest being inventory held longer.
- **This is the baseline.** Milestones 5 and 6 are measured against it: the question is how much of that −1 cent each idea (skew, wider quotes, pulling on a trend signal) recovers.

### D28. Inventory is valued at the mid when quoting ends

- **Bug found on the full day:** P&L first marked inventory at the last mid of the file. After 16:00 the book thins out until every order is gone at the end of after-hours, and the last two-sided mid was about $300.63 for a stock that closed near $291. The implied mark gave it away. Morning-only runs were unaffected (their last mid was during regular hours).
- **Fix:** mark at the mid in force at `end_ns` (16:00), when quoting stops. A test reproduces the case: a far-away quote after the end must not move the P&L.
- **Lesson:** check results against something outside the code (here, the known closing price). The tests passed; the number was still wrong.

### D29. Volatility: squared mid changes on a fixed clock grid

- **What:** sample the mid every `interval` (aligned to whole intervals since midnight), take the squared change between grid points, average over the last `window` intervals, scale to one second. Units: ticks² per second, the sigma² that Avellaneda-Stoikov multiplies by inventory and time.
- **Mean assumed zero:** over seconds the drift is tiny next to the noise; estimating it would add error, not remove it.
- **Quiet time counts:** a grid point with no change since the last one is a zero, not a gap to skip. Otherwise a calm market would look as volatile as its few moves.
- **Rolling window over EWMA:** a deque plus running sum gives exact, easy-to-test numbers and a clear meaning ("the last 5 minutes"). EWMA (RiskMetrics) needs no buffer and reacts smoothly, but its decay is harder to reason about. Either works; this is the simpler one to defend.
- **AAPL, 30 Dec 2019, 09:31-16:00 (149.5 K mid changes):**

  | grid | sigma (cents / √s) | implied 6.5 h move |
  |---|---|---|
  | 10 ms | 1.87 | $2.86 |
  | 100 ms | 1.89 | $2.89 |
  | 1 s | 2.00 | $3.06 |
  | 10 s | 2.15 | $3.29 |
  | 60 s | 2.13 | $3.25 |

  About 1.1 % of a ~$290 stock for the day: a plausible daily volatility for AAPL then (external check). The 5-minute estimate falls through the day, from 2.9 cents/√s at 10:00 to 1.1-1.2 after 15:00: the usual intraday pattern, busy morning, calm afternoon.
- **Surprise (signature plot):** variance per second *rises* with the sampling interval and levels off around 10 s. Trade prices usually show the opposite (bid-ask bounce inflates short intervals). For the mid, short intervals see the price move in small steps in the same direction (positive autocorrelation: quotes catch up gradually), so they underestimate. Short grids read about 15 % low against 10 s. The interval is a parameter; it gets picked with the strategy (5c), matched to how long we hold inventory.

### D30. The strategy is a separate, swappable object; the simulator keeps the rules

- **Split:** the simulator owns the mechanics (latency, queue position, fills, accounting, the mid series). A `Strategy` only answers "where do I want my bid and ask?" given a `MarketView` (time, best bid and ask, shares resting at each, our inventory). Same car, different drivers: that's what makes strategy comparisons fair.
- **Asked on every two-sided message of our stock, also before 09:31,** so a strategy can warm up (the volatility window needs 5 minutes of data before it answers). Outside hours the answer is ignored.
- **Rules no strategy can break:** quoting hours, a two-sided uncrossed market, the inventory limit, positive prices, and **never better than the touch** (a bid above the best bid is moved down to it, an ask below the best ask up to it). A price inside the spread is one nobody in the real data saw; other traders would have reacted to it and the replay can't say how. Clamping also keeps us passive: our bid can never reach the ask and trade.
- **Run-time choice (`virtual`) over compile-time (template):** an abstract base class with one pure virtual function is the clearest way to swap strategies, and the indirect call costs nanoseconds on a path that already takes ~190 ns per message. **Later (optimization milestone):** make the simulator a template on the strategy type, so the call can be inlined, and measure the difference with Google Benchmark.
- **Refactor check:** the 16 existing simulator tests pass unchanged, and the full-day fills files at 0, 50 and 1000 µs are byte-identical to the ones from before the split (D27 numbers unchanged).

### D31. Avellaneda-Stoikov, adapted to quoting only at or behind the touch

- **Formula (ticks):** risk = γ·σ²·H; reservation = mid − q·risk; half spread = base + risk/2; bid/ask = reservation ∓ half spread, rounded to the nearest cent. The simulator then holds both at or behind the touch (D30).
- **Choices:**
  - **q in lots** (one lot = one 100-share quote): AS's q counts units of risk. In shares, the widening term would be a 1/100 of a lot's lean, i.e. nothing.
  - **Fixed holding time H = 10 s** instead of "time to the close": in the original the lean fades to zero towards 16:00, the opposite of what a desk wants. 10 s is about where the markouts stop getting worse (D27).
  - **Fixed base half spread (0.5 cent)** instead of estimating k: for small γ, AS's (2/γ)·ln(1 + γ/k) ≈ 2/k, a constant. Half a cent means a flat book joins a 1-cent market. Estimating k from fill rates is a later refinement.
  - **σ² on a 1 s grid over 5 minutes** (D29). It reads ~15 % low against 10 s, but a constant factor is absorbed by γ, and 1 s gives 10x the samples.
  - **Nearest-cent rounding:** always rounding bids down and asks up would step back from the touch whenever the widening is a fraction of a cent, even when flat.
  - **No quotes until σ² is ready.** In practice it's ready long before 09:31 (the strategy is asked from the first two-sided book).
  - **γ is set by "cents of lean per lot at average volatility"** (`mm_sim --as 0.2`), using AAPL's day-average σ² = 40,000 ticks²/s, so runs are comparable without thinking in units of 1/tick.
- **What it becomes on AAPL:** we never improve on the touch (D30), and the AS quotes are usually inside Nasdaq's spread (half a cent from the reservation price, while the spread is mostly 2-3 cents, D32), so they get moved back to the touch. Leaning can't make the ask more aggressive when long. The only effect is stepping the bid (or, short, the ask) back from the touch, by an amount that grows with inventory and volatility.
  - *Correction (D32):* this first said the spread was "one tick". Sampling it showed Nasdaq's AAPL spread was 1 cent only 13 % of the time that day; the conclusion holds, the reason was wrong.
- **Full day, AAPL, 30 Dec 2019** (cents per share; markouts + good for us):

  | latency | lean / lot | fills | shares | max pos | P&L | P&L / share | markout 0 | 10 s | 60 s |
  |---|---|---|---|---|---|---|---|---|---|
  | 0 | naive | 8,140 | 468.5 K | 500 | −$3,796 | −0.81 | +0.55 | −0.85 | −1.01 |
  | 0 | 0.1 ¢ | 8,060 | 465.1 K | 500 | −$3,752 | −0.81 | +0.56 | −0.83 | −0.97 |
  | 0 | 0.2 ¢ | 7,798 | 453.8 K | 499 | −$3,501 | −0.77 | +0.57 | −0.80 | −0.83 |
  | 0 | 0.5 ¢ | 6,878 | 407.4 K | 487 | −$2,913 | −0.72 | +0.60 | −0.84 | −0.82 |
  | 0 | 1.0 ¢ | 5,680 | 342.4 K | 403 | −$2,315 | −0.68 | +0.61 | −0.83 | −0.75 |
  | 50 µs | naive | 8,111 | 469.8 K | 500 | −$3,366 | −0.72 | +0.50 | −1.01 | −1.00 |
  | 50 µs | 0.1 ¢ | 8,041 | 468.2 K | 500 | −$3,179 | −0.68 | +0.51 | −0.99 | −0.98 |
  | 50 µs | 0.2 ¢ | 7,798 | 457.5 K | 500 | −$3,215 | −0.70 | +0.52 | −0.97 | −0.92 |
  | 50 µs | 0.5 ¢ | 6,983 | 413.5 K | 500 | −$2,803 | −0.68 | +0.55 | −0.97 | −0.88 |
  | 50 µs | 1.0 ¢ | 5,802 | 348.4 K | 464 | −$2,732 | −0.78 | +0.56 | −0.98 | −0.99 |

- **Reading it:**
  - **It still loses everywhere.** Most of the smaller loss comes from trading less (468 K → 342 K shares), not from trading better.
  - **The 10 s markout doesn't move** (−0.85 → −0.83 at 0 µs). That's the adverse selection, and AS isn't built to see it: it reacts to *our* inventory, not to who is on the other side. This was the expectation before the run.
  - **What it does fix is inventory:** max position falls (500 → 403), and the 60 s markout improves (−1.01 → −0.75), because we stop adding to a position as the price runs against it.
  - **At 50 µs, P&L per share is within noise** across all settings. One stock on one day can't separate a few hundredths of a cent.
- **Next:** the P&L split (spread / adverse selection / inventory) will show this directly. The adverse-selection part needs a signal about the other side, which is the microprice (5d) and the trend/range read (milestone 6).

### D32. Queue imbalance predicts the next second, not the next ten

- **Question first, strategy second:** before using the microprice in a strategy, check on the data whether the imbalance at the touch, I = bid size / (bid size + ask size), predicts where the mid goes.
- **Method (`imbalance_study`):** sample the touch every 100 ms from 09:31 to 16:00, group the samples into imbalance deciles, and average the mid change 1 s and 10 s later. Next to it: what the weighted mid (bid + I·spread, the simple microprice) predicted, (I − 0.5)·spread. Sampling on a clock grid, not on every book change, so busy moments don't count more than quiet ones.
- **Spread on Nasdaq's book (AAPL, 30 Dec 2019):** 1 cent 13 %, 2 cents 36 %, 3 cents 32 %, 4 cents 14 %, wider 4 %. Nasdaq's own touch is often wider than the consolidated best bid and offer (other venues quote inside it), which is the one-venue limitation again.
- **Result, mid change 1 s later (cents; 233 K samples):**

  | I | predicted | realized | up | down |
  |---|---|---|---|---|
  | 0.0-0.1 (thin bid) | −1.13 | −0.31 | 28 % | 45 % |
  | 0.2-0.3 | −0.71 | −0.08 | 31 % | 37 % |
  | 0.4-0.5 | −0.14 | +0.01 | 31 % | 30 % |
  | 0.6-0.7 | +0.40 | +0.09 | 35 % | 27 % |
  | 0.9-1.0 (thin ask) | +1.08 | +0.23 | 43 % | 23 % |

- **Reading it:**
  - **The signal is real at 1 s:** the realized move rises with I across the deciles, and the thin side's price is almost twice as likely to move as the other one (45 % vs 28 %).
  - **The weighted mid overreacts about 4x:** it predicts −1.13 cents where −0.31 happens. Its prediction would need scaling by about β ≈ 0.25. This is why Stoikov (2018) estimates the micro-price from data instead of using the weighted mid.
  - **At 10 s it's gone:** every decile is about 50 % up / 47 % down, and the averages (+0.0 to +0.3) are just the day's upward drift. Imbalance is a sub-second signal.
- **What that means for the strategy:** imbalance can help with the part of adverse selection that happens in the first second (markout +0.55 at the fill → −0.65 at 1 s, D27). It can't explain the further loss out to 10-60 s; that needs a slower signal (milestone 6).

### D33. What the book looked like when we were filled

- **Question:** 4a showed imbalance predicts the next second on average. But we aren't filled at average moments: being filled at the back of a thin queue means the whole level is being eaten. So measure the fills themselves, split by the book at that moment.
- **Recorded on every fill** (before the execution is applied to the book, D25):
  - `touch_share`: our side's shares at the touch / both sides' (for a bid fill, the imbalance I; for an ask fill, 1 − I).
  - `shares_left`: real shares still at our price after this execution, i.e. the queue behind us. 0 means the level broke with our fill (or we were traded through).
- **Naive strategy, full day, 0 µs** (cents per share; shares = share of all shares we traded):

  | our touch share | fills | shares | 0 | 100 ms | 1 s | 10 s |
  |---|---|---|---|---|---|---|
  | 0-20 % (thin) | 2,387 | 13 % | +0.37 | −0.56 | −0.79 | −0.79 |
  | 20-40 % | 2,219 | 32 % | +0.49 | −0.45 | −0.71 | −1.02 |
  | 40-60 % | 1,989 | 31 % | +0.58 | −0.40 | −0.57 | −0.84 |
  | 60-80 % | 971 | 15 % | +0.69 | −0.36 | −0.67 | −1.11 |
  | 80-100 % (thick) | 574 | 9 % | +0.63 | −0.24 | −0.55 | +0.12 |

  | shares left at our price | fills | shares | 0 | 100 ms | 1 s | 10 s |
  |---|---|---|---|---|---|---|
  | 0 (level broke) | 4,242 | 56 % | +0.39 | −0.51 | −0.75 | −0.95 |
  | 1-499 | 3,798 | 43 % | +0.74 | −0.31 | −0.54 | −0.73 |
  | 500+ | 100 | 1 % | too few to read | | | |

  At 50 µs the pattern is the same (level broke: 61 % of shares, −0.83 at 1 s vs −0.64).
- **Reading it:**
  - **Thin touch = worse fills in the first second.** At 100 ms the markout rises steadily from thin to thick (−0.56 → −0.24). At 10 s the buckets no longer line up: the effect is short-lived, as in D32.
  - **56 % of our shares were filled as the level broke.** Joining at the back of the queue means we're usually filled only when the whole level is consumed: the moment the price is about to move against us. Those fills are about 0.2 cents worse at 1 s than fills with someone left behind us. Queue position has value: being early in a queue (joining a new level first, not giving up a place by requoting) matters as much as the price.
  - **Thin-touch fills are small:** 29 % of fills but 13 % of shares; we're often filled in part as a small level is taken.
  - **Not a fix on its own:** every bucket is still negative at 10 s. Pulling from thin touches can trim the first-second loss, not the slower one.
  - The 2,000+ shares-left bucket (5 fills) shows extreme numbers; with that few fills it means nothing.
- **Accounting check:** the totals (8,140 fills, −$3,796.21) are unchanged; recording the context doesn't change behaviour.

### D34. "Pull when the queue is thin": a real signal that doesn't turn into a better strategy

- **Rule:** a wrapper (decorator) around any strategy. It asks the inner strategy where to quote, then drops a side whose share of the shares at the touch is below θ. The inner strategy is asked on every message, so its state (AS's volatility window) keeps updating. It only ever removes a side, never adds one.
- **Floating-point catch:** computing the ask share as `1 − bid_share` turns 0.8 into 0.1999…, which pulled an ask exactly at the threshold while the matching bid stayed. Each share is now divided out on its own, with a test for both sides.
- **New metric, quote messages:** every new quote, price change or pull sent. Exchanges watch message rates and order-to-trade ratios, and each pull and rejoin gives up our place in the queue.
- **Full day, AAPL, 0 µs unless noted** (cents per share):

  | strategy | fills | shares | msgs / fill | P&L | P&L / share | 0 | 100 ms | 1 s | 10 s | level broke |
  |---|---|---|---|---|---|---|---|---|---|---|
  | naive | 8,140 | 468 K | 16.7 | −$3,796 | −0.81 | +0.55 | −0.42 | −0.65 | −0.85 | 56 % |
  | pull θ = 0.1 | 5,818 | 411 K | 26.6 | −$3,302 | −0.80 | +0.56 | −0.43 | −0.67 | −0.80 | 55 % |
  | pull θ = 0.2 | 4,562 | 340 K | 36.9 | −$2,998 | −0.88 | +0.58 | −0.43 | −0.67 | −0.88 | 55 % |
  | pull θ = 0.3 | 3,129 | 242 K | 63.5 | −$1,619 | −0.67 | +0.61 | −0.44 | −0.73 | −0.78 | 53 % |
  | AS 0.5 ¢ + pull 0.2 | 3,958 | 299 K | 49.7 | −$2,510 | −0.84 | +0.63 | −0.39 | −0.65 | −0.86 | 55 % |
  | naive, 50 µs | 8,111 | 470 K | 16.7 | −$3,366 | −0.72 | +0.50 | −0.50 | −0.75 | −1.01 | 61 % |
  | pull 0.2, 50 µs | 5,558 | 361 K | 30.7 | −$3,138 | −0.87 | +0.52 | −0.53 | −0.81 | −1.08 | 61 % |

- **Reading it:**
  - **The markouts don't improve.** At 100 ms and 1 s every pull setting is the same as naive or slightly worse, even though D33 showed thin-touch fills are worse. The smaller loss in dollars comes from trading less (468 K → 242 K shares), as with AS (D31). P&L per share moves around between −0.67 and −0.88 with no pattern: noise.
  - **The bad fills just move.** About 55 % of our shares are still filled as the level breaks, with or without the rule. Pulling a thin side and rejoining when it thickens puts us at the back of the queue again, so we're still the last order standing when the level is eaten. D33's thin-touch fills were a symptom; the cause is queue position.
  - **It costs messages:** 16.7 → 63.5 quote messages per fill. A real exchange would notice, and every rejoin is a lost place in line.
  - **Lesson: a signal that predicts isn't yet a strategy.** Acting on it changes which fills we get, and the replay has to show whether that helps. Here it didn't.
- **Caveat, not yet checked:** one aggressive order that eats a level shows up as several executions, likely with the same timestamp. At 0 µs the simulator can react between them, which a real system can't (it sees the sweep after it's done). That would flatter the 0 µs pull results, so the real effect is at most as good as shown.
- **What this points to:** queue position (joining early, not giving up a place) and the slower part of the loss (10-60 s), which no queue signal touches. Microprice β is skipped: D32 already shows it would barely move a quote that only joins or steps back from the touch.

### D35. P&L split into spread, adverse selection and inventory

- **Identity, per fill** (q shares at price p, s = +1 buy / −1 sell; mids in ticks):
  `s·q·(mid_end − p) = s·q·(mid_before − p) + s·q·(mid_after_h − mid_before) + s·q·(mid_end − mid_after_h)`
  = **spread** + **adverse selection** (the move within h) + **inventory** (holding it after h). The three terms telescope, so over all fills they add up to the mark-to-market P&L exactly. The rebate is separate, on top (5b).
- **Choices:**
  - **Spread is measured from the mid just before the fill** (`Fill::mid_x2_before`, the last two-sided mid before the execution is applied), not the mid at the fill's timestamp. A fill that breaks a level moves the mid at that same timestamp; that move is the counterparty's information, so it belongs to adverse selection. (The markout at 0 in D27 uses the mid after, which is why it reads +0.55 while the spread captured is +1.03.)
  - **The horizon stops at the end of quoting** (16:00), for the same reason as D28.
  - **Total recomputed from the fills alone** (cash + position × end mid) and checked against the simulator's own accounting, by a test and by `mm_sim` on every run.
  - **h is a choice, so `mm_sim` shows three** (1 s, 10 s, 60 s): a longer h moves loss from "inventory" into "adverse selection"; the total never changes.
- **Full day, AAPL** (dollars, and cents per share):

  | strategy | shares | spread | adverse 1 s | adverse 10 s | adverse 60 s | inventory (10 s) | total |
  |---|---|---|---|---|---|---|---|
  | naive, 0 µs | 468 K | +$4,838 (+1.03) | −1.69 | −$8,779 (−1.87) | −2.04 | +$145 (+0.03) | −$3,796 (−0.81) |
  | naive, 50 µs | 470 K | +$4,569 (+0.97) | −1.72 | −$9,276 (−1.97) | −1.96 | +$1,341 (+0.29) | −$3,366 (−0.72) |
  | AS 1.0 ¢, 0 µs | 342 K | +$3,703 (+1.08) | −1.74 | −$6,530 (−1.91) | −1.83 | +$512 (+0.15) | −$2,315 (−0.68) |
  | pull 0.3, 0 µs | 242 K | +$2,600 (+1.08) | −1.81 | −$4,474 (−1.85) | −2.20 | +$255 (+0.11) | −$1,619 (−0.67) |

- **Reading it:**
  - **The whole loss is adverse selection, and most of it comes in the first second.** We earn about 1 cent a share of spread and give back about 1.7 cents within 1 s (about 90 % of the 10 s figure). Inventory is close to zero at every horizon.
  - **Every strategy has the same adverse selection per share** (−1.69 to −1.81 at 1 s). AS and the pull rule lose fewer dollars only because they trade fewer shares. That's the D31 and D34 conclusions in one table.
  - **Inventory isn't the problem, which is why AS couldn't help much:** it manages a cost that is already near zero.
  - The ~1 cent per share of spread is below half of Nasdaq's average AAPL spread (mostly 2-3 cents, D32), because we're often filled as the spread is narrowing or at a level that's about to break.
- **So the target is clear:** cut the first-second adverse selection (faster reaction, better queue position, knowing when the other side is informed) or earn enough on top (rebates, 5b) to cover it.

### D36. Maker rebate: real money, not enough to cover adverse selection

- **What:** exchanges pay whoever adds displayed liquidity a rebate per share when their order is executed (Nasdaq's maker-taker model; the taker pays a fee). We only ever add liquidity, so every share we trade earns it. `mm_sim --rebate <cents per share>` adds it to the breakdown, on top of the three parts (D35). It doesn't change behaviour, so it's a parameter, not a strategy.
- **Size:** tiered by monthly volume. Roughly 0.2 cents a share for a mid-size firm and about 0.3 for the top tiers is the usual range for Nasdaq in 2019; the exact schedule hasn't been checked. Default 0, so no result depends on an assumed number unless asked.
- **Full day, AAPL, 0 µs** (cents per share):

  | strategy | total | + 0.2 ¢ rebate | + 0.3 ¢ rebate |
  |---|---|---|---|
  | naive | −$3,796 (−0.81) | −$2,859 (−0.61) | −$2,391 (−0.51) |
  | AS 1.0 ¢ (342 K shares) | −$2,315 (−0.68) | −$1,630 (−0.48) | −$1,288 (−0.38) |
  | pull 0.3 (242 K shares) | −$1,619 (−0.67) | −$1,135 (−0.47) | −$893 (−0.37) |

  (AS and pull rows: shares × rebate added to their totals from D35.)
- **Reading it:** the rebate covers about a quarter of the naive loss. To break even the naive strategy would need 0.81 cents a share, almost three times the best tier. Rebates are why market makers can live on thin edges, but they can't pay for being picked off: the first-second adverse selection (D35) has to come down.

### D37. Confidence intervals: block bootstrap, paired comparisons

- **Question:** one day gives one number per strategy. Is a difference between two strategies more than that day's noise? (Not the same as overfitting: that needs days never looked at. This filter stops us from reading noise as a finding, the most common way to overfit.)
- **Pipeline:** `mm_sim` writes raw fills (`write_fills_csv`, D37 columns: prices, mid before, mids 1/10/60 s later capped at 16:00, book context). `research/bootstrap.py` (Python, standard library only, unit-tested) does the statistics. Research in Python, simulation in C++, as planned.
- **What's measured:** P&L per share at horizon h = spread + adverse selection (D35), as total P&L / total shares. Inventory isn't bootstrapped: it is the move from h to the close, one path for the whole day, and can't be cut into blocks.
- **Block bootstrap:** 09:31-16:00 cut into 5-minute blocks (78); draw 78 blocks with replacement, recompute, 2,000 times; the middle 95 % is the interval. Blocks, not single fills, because fills close in time share one price move; treating them as independent makes the interval falsely narrow. Empty blocks stay in.
- **Paired:** both runs replay the same day, so each resample uses the same blocks for both, and the interval is on the difference. A block where the market moved hard hurts both runs, and that shared noise cancels. The difference intervals come out much narrower than either run's own interval.
- **Results, AAPL, 10 s horizon, 5-minute blocks** (cents per share):

  | run | estimate | 95 % interval | vs naive 0 µs | 95 % interval | verdict |
  |---|---|---|---|---|---|
  | naive, 0 µs | −0.84 | [−1.02, −0.67] | | | |
  | AS 0.5 ¢ | −0.83 | [−1.03, −0.64] | +0.01 | [−0.11, +0.13] | noise |
  | AS 1.0 ¢ | −0.83 | [−1.03, −0.64] | +0.02 | [−0.14, +0.15] | noise |
  | pull 0.2 | −0.87 | [−1.10, −0.64] | −0.03 | [−0.14, +0.09] | noise |
  | pull 0.3 | −0.78 | [−1.03, −0.52] | +0.07 | [−0.12, +0.25] | noise |
  | naive, 50 µs | −1.00 | [−1.23, −0.78] | −0.16 | [−0.32, −0.03] | **worse** |
  | pull 0.2, 50 µs | −1.07 | [−1.32, −0.83] | −0.23 | [−0.40, −0.06] | **worse** |

  At 1 s the same picture (every strategy vs naive: noise; pull 0.3 at −0.08 [−0.19, +0.02] is the closest to "worse"). With 1-minute and 15-minute blocks the verdicts don't change.
- **Reading it:**
  - **The loss is real** (in-sample): the naive interval is well below zero.
  - **None of the strategy ideas so far is distinguishable from naive** on what they control, the 10 s P&L per share. Their smaller dollar losses (D31, D34) are fewer shares traded, now confirmed: per share they are the same.
  - **Latency is the one difference that shows up:** 50 µs is about 0.16 cents a share worse than 0 µs at 10 s, and the interval excludes zero. This turns around the earlier reading. In total P&L, 50 µs looked *better* (−$3,366 vs −$3,796, D27), but that gap was the inventory part (+$1,341 vs +$145, D35), i.e. the day's path, not the strategy. Splitting the P&L and putting intervals on the controllable part is what shows it.
  - Still one day: these say what is noise *on this day*. Whether anything holds on other days is the out-of-sample question for the optimization phase.

### D38. Bars for the indicators: mid prices, any interval, no look-ahead

- **Why bars:** the indicators to try in milestone 6 (VWAP, SMA 20, EMA 9/21, Bollinger (20, 2), Stochastic RSI (14, 14, 3, 3)) are defined on closed bars. One `BarBuilder` makes them for every indicator and every trial, tested once.
- **Any interval, several at once:** the interval is a parameter. The plan is 1 s and 1 min side by side. The founder's view is that the market is fractal, so the same rules should work on a 1 s chart. That's a hypothesis to test, not an assumption: D32 already shows rules that hold at 1 s and are gone at 10 s. And 1 s fits the problem, since about 90 % of the adverse selection happens within 1 s (D35).
- **Mid, not trade prices:** at 1 s many bars would have no trade, and trade prices jump between bid and ask while the price hasn't moved (bid-ask bounce). The mid is always defined and has no bounce. Volume still comes from the trades. At 1 min the difference from a trade-price chart is about half a spread.
- **Quiet bars stay in:** a second with no change is a flat bar (open = high = low = close = last mid, volume 0), as in D29. Skipping it would make a quiet market look busy and stretch "9 bars" over an unknown length of time.
- **Aligned to the clock:** bars are whole intervals since midnight, so the 1-min bars are 09:30:00-09:31:00 and so on, as on a chart.
- **No look-ahead:** a bar is handed out only when an event at or after its end arrives, so nothing can use it before it has closed. A change exactly at a bar's end belongs to the next bar and is that bar's open (the same "in force at or before" rule as `mid_x2_at`). This is the most common backtest bug, and a test pins it down.
- **Trade-off:** each call returns a `std::vector` of the bars it closed. That's simple to use and test, and allocates only when a bar closes. A callback would avoid the vector, if profiling ever shows it matters.

### D39. Indicators: streaming, TradingView's definitions, defined edge cases

- **Set:** SMA, EMA (founder uses 9/21), Bollinger (20, 2) with %B, RSI (14), Stochastic RSI (14, 14, 3, 3), VWAP. The defaults are the founder's chart settings; every length is a parameter, so trials can try others.
- **Streaming:** each indicator takes one closed bar's value at a time (`update`) and answers `value()`, which is `nullopt` until it is warm. It never sees the future, and the bar builder (D38) only hands out closed bars.
- **TradingView's definitions,** so the numbers can be checked against a chart:
  - EMA, alpha = 2/(n+1), seeded with the SMA of its first n values. Starting from the first value would weight that one bar too much for a while.
  - Bollinger uses the population standard deviation (÷ n).
  - RSI uses Wilder's smoothing (alpha = 1/n), seeded with the plain average of the first n changes.
  - Stochastic RSI is the RSI's place in its last-n range, then %K = SMA 3, %D = SMA 3 of %K.
- **Warm-up:** Stochastic RSI (14, 14, 3, 3) needs 32 closes. RSI is ready at 15, the first stoch at 28, %K at 30, %D at 32. That's 32 minutes on 1-minute bars, so the bars are fed from pre-market (founder, 23 Nov 2026: pre-market can be used). VWAP's start time is a parameter: 09:30 by default, earlier to include pre-market, both to be tried.
- **Edge cases, defined instead of left to 0/0** (common on a 1-second chart):
  - No move at all gives RSI 50.
  - A flat RSI gives Stochastic RSI 50.
  - A flat Bollinger window has bands on the mean and %B 0.5.
  - Wilder's averages only decay after a move, so a quiet stretch after one keeps the old reading rather than going to 50. That is the standard definition.
- **Bollinger's standard deviation is computed in two passes** (mean, then squared gaps from it), not from running sums. At ~29 M ticks, mean(x²) − mean(x)² subtracts two numbers near 8 × 10¹⁴ to find a variance of a few ticks², and a double can't hold that difference (catastrophic cancellation). That's O(n) per bar with n ≈ 20, which is nothing. A test pins it with AAPL-sized prices moving one tick.
- **VWAP in integers:** Σ price × shares in int64 is exact (about 10¹³ for a day, against a limit of about 9 × 10¹⁸). It is updated on every trade, not per bar.
- **Composable:** SMA averages any series, which is how Stochastic RSI builds %K and %D. Each class is small and has no virtual calls; a trial owns the ones it needs.

### D40. Trades from ITCH, and one `ChartFeed` per timeframe

- **What counts as a trade:**
  - **E:** a displayed order executed. The message has no price; the trade is at the resting order's price, read before the book applies the message (the book may remove the order).
  - **C:** executed at another price. Counted only when printable; a non-printable C was counted by Nasdaq elsewhere, and counting it again would double the volume.
  - **P:** a hidden order executed. It never touches the book, but it is real volume.
  - An E for an order the book doesn't have is skipped: there's no price to give it, and the replay already counts it as an unknown order (D21).
- **Q, the auction prints (added after D42):** the opening and closing crosses are each one `Q` message with the whole auction's shares at the cross price. The resting orders the auction filled get non-printable C messages, which is why those are skipped: the volume is in the Q. A cross where nothing matched still sends a Q with 0 shares, which isn't a trade. Other venues' trades aren't in ITCH at all (one venue, see Known limitations), so volume and VWAP are Nasdaq's only.
- **`ChartFeed`:** one timeframe's bars plus every indicator, with the founder's settings as defaults (`ChartSettings`). Each event returns one `ChartPoint` per bar that closed, with the indicators as they were at that bar's close. A quiet stretch closes several bars at once, and reading the values only at the end would give every one of them the last bar's numbers; a test pins this. `latest()` is the last closed bar's point, never the open bar's, so that's what a strategy may use.
- **VWAP stays outside `ChartFeed`:** it updates per trade, not per bar, so the 1 s and 1 min charts share one.

### D41. `chart_dump`: the indicators on the real day, checked against a chart

- **What:** replays the file and writes every closed 1 s and 1 min bar for one stock, with every indicator and the VWAP at that bar's close, in chart units (dollars, 0-100). Bars start at `--from` (09:00 by default, so pre-market warms the indicators up) and end at 16:00. VWAP starts at `--vwap-from` (09:30 by default). An indicator still warming up is an empty field, not a 0.
- **Order of work per message:** close the bars that ended at or before its time, then take the trade (before the book applies it, D40), then apply it and feed a changed mid. So a bar's row never includes anything stamped at or after its end, VWAP included.
- **AAPL, 30 Dec 2019 (full day, ~75 s):**
  - 67 K trades counted. Regular-session Nasdaq volume is 5.68 M shares, about a sixth of the consolidated volume (the one-venue limitation).
  - The day's high and low from our 1 min bars are $292.69 / $285.235, against roughly $292.69 / $285.22 on consolidated charts. The mid tracks the traded price.
  - Of the 23,400 regular-session 1 s bars, 47 % have no trade and 26 % have no mid move at all. A trade-price chart would be half empty at 1 s, which is why bars use the mid (D38).
- **Bug found by looking at the output:** Stochastic RSI printed "-0.00". SMA kept a running total, and adding then subtracting the same doubles leaves rounding dust (0.1 + 0.1 + 0.1 − 0.1 − 0.1 − 0.1 = 2.8 × 10⁻¹⁷). SMA now sums its window afresh each time (n ≤ 21, so it's cheap), and a test pins it. Same lesson as D28: check the output, not just the tests.
- **Checked against an independent source in D42** (TradingView has no 1-minute history for 2019).

### D42. Bars and indicators cross-checked against Databento

- **Source:** Databento's XNAS.ITCH OHLCV bars (1 min and 1 s) for AAPL, 30 Dec 2019, 09:00-16:00 ET. They are built from the same Nasdaq feed with someone else's code, from trade prices. Cost $0.008 (approved 23 Nov 2026), 0.2 MB, in `data/databento/` (git-ignored). `research/check_chart.py` does the comparison. It recomputes the indicators from their textbook definitions, independently of the C++.
- **Volume (tests `trade_of`):**
  - Identical in 389 of 390 regular-session minutes, and in 23,399 of 23,400 seconds.
  - The one difference is 09:30:00: 269,367 shares, the opening cross, which we don't read (D40). That's 4.5 % of the day's Nasdaq volume in one print.
  - So E, printable C and P are exactly Nasdaq's trades. The 16:00 closing cross falls after our last bar.
- **Indicator code (C++ vs the Python reference, both on our closes):** every value agrees to the CSV's last printed digit (max 0.005 cents for EMAs, 0.005 points for RSI and Stochastic RSI). The C++ is right.
- **Mid vs trade price (our closes vs theirs):**
  - Closes: median 0.5 cent, 95 % within 2.5 cents.
  - The same indicators computed on their trade closes differ from ours as follows (median, 95 %):
    - EMA 9: 0.27 / 0.82 cents.
    - EMA 21: 0.22 / 0.64 cents.
    - RSI: 0.34 / 1.0 points.
    - Stochastic RSI %K: 1.0 / 4.4 points.
  - Moving averages barely notice the choice of price. Stochastic RSI does, since it's a ratio of small RSI moves. A rule that fires on a %K threshold could flip near the line depending on mid vs trade.
- **Opening cross added (`Q`, D40):** with it, volume is identical in all 390 minutes and all 23,400 seconds, and the totals are equal (5,947,543 shares). VWAP at the 09:30 bar's close moves from $289.11 to $289.37. The simulator doesn't use `trade_of`, and the milestone 5 runs were rerun to check: their fills are byte-identical.

### D43. One stock's slice of the file, for fast iteration

- **Why:** every run read the whole 8 GB day (~2 minutes) to simulate one stock. Milestone 6 means trying many ideas, so each try should take seconds.
- **What:** `itch_extract` copies every message for one stock locate, plus the system-wide ones (locate 0), byte for byte with their length prefixes. The output is an ordinary ITCH file, so every tool runs on it unchanged. The stock's directory message (`R`) carries its own locate, so tools still find it by name.
- **AAPL, 30 Dec 2019:** 1.52 M of 268.7 M messages, 46 MB (0.6 % of the file).
  - `mm_sim` takes 0.56 s instead of 129 s.
  - `chart_dump` takes 0.5 s instead of 75 s.
  - Fills, the report and the chart CSV are byte-identical to the full-file runs. Nothing outside the stock changes its book, so nothing is lost.
- **Limit:** a study across stocks still needs the full file (or one extract per stock). For one-stock simulation, the slice is all there is.
- **Also:** `find_locate` is now a library function with a test. Three tools had their own copy; the new tool uses the library one.

### D44. The strategy sees the charts: one shared, read-only `MarketState`

- **What:** the simulator keeps one `MarketState` for our stock: 1 s and 1 min `ChartFeed`s and the VWAP. It feeds it on every message of the stock, and `MarketView::market` points at it, read-only. A trial reads, for example, `view.market->one_minute().latest()->ema_fast`. The window and the settings are in `SimConfig::market`: charts 09:00-16:00 (pre-market warms them up), VWAP from 09:30, and the founder's chart settings.
- **Order per message of our stock:**
  1. Close the bars that are over, so nothing at `now` gets into them. Since D45 this runs first of all, before the fill check.
  2. Count the message if it's a trade, before the book applies it (D40).
  3. Apply it.
  4. Feed a changed mid.
  5. Ask the strategy.

  So the strategy sees closed bars only, and a VWAP that includes the trade at `now`, which has happened and which the book already shows.
- **No extra latency on indicators:** our quotes wait `latency_ns`, the charts don't. A microsecond doesn't move a 1 s or 1 min bar's indicators in any way that matters.
- **One class for the simulator and `chart_dump`:** what a strategy sees is exactly what was checked against Databento (D42). Two copies could drift apart.
- **Bug the regression check caught:** the mid in force at 09:00 usually changed before 09:00, and an "only feed changes" loop never gave it to the charts. The first bar waited for the next change, at 09:00:09 on the dev day. `MarketState` now carries the last mid from before the window and opens the bars with it at `from_ns`. The old `chart_dump` fed it at the first message after 09:00 (09:00:04), close but not exact. Only the first ten minutes of 1 s rows changed. The Databento check is unchanged (volume 100 %, indicator code to the last digit).
- **Bars are never lost:** `on_trade` and `on_mid` can also close bars. These wait in a buffer and come out with the next `advance()`, whatever the call order.
- **Pointer, not reference, in `MarketView`:** tests build views by hand without charts (`nullptr`). The simulator always sets it.
- **Regression:** no strategy reads the charts yet, so the milestone 5 runs are byte-identical, and so is the chart CSV after 09:10.

### D45. Every fill records the charts as they were at that moment

- **What:** a `Fill` now carries the last closed 1 s and 1 min `ChartPoint` and the VWAP, copied when the fill happens. The fills CSV gets 15 more columns: `ema_fast, ema_slow, sma, bb_pct, rsi, stoch_k, stoch_d` for `_1s` and then `_1m`, and `vwap`. Prices are ticks to 2 places, %B to 3, RSI and stoch to 2. A field is empty while that indicator is warming up, or outside the chart window.
- **Why:** stage 1 of the testing plan asks whether a chart reading separates the toxic fills from the good ones. That needs, for each fill, what the chart said at that moment, next to its markout. Recording it in the simulator means the study reads exactly what a strategy would have seen, with no second calculation in Python that could disagree (D42, D44).
- **What "at that moment" means:**
  - **Bars:** every bar closed by the fill's own time. So closing the bars (`advance`) now runs first on each message, before the fill check. It only reads the past, so moving it changes nothing else.
  - **VWAP:** the trades before this one, not the trade that filled us. The fill check runs before that trade is counted. A strategy deciding to be in the queue couldn't have known it.
- **Only the latest bar:** a study that needs, say, the EMA slope uses the 1 s chart CSV next to the fills, joined on time.
- **Its own number format:** the new fields are formatted in a separate stream. Setting `std::fixed` on the file's stream would change how `touch_share` prints, and the older columns must stay byte-identical.
- **Check:** for a fill at 09:57:48.055 the snapshot equals the `chart_dump` rows for the 09:57:47 1 s bar and the 09:56 1 min bar, to the last digit. All 8,140 naive fills have a snapshot: quoting starts at 09:31, and the charts have warmed up since 09:00.
- **Regression:** the milestone 5 reports and bootstrap tables are identical, and the first 10 columns of every fills CSV are byte-identical. `bootstrap.py` reads columns by name, so the new ones don't affect it.

### D46. The hub: botbot's structure, TypeScript for the charts, C++ for the numbers

- **What:** a local page for looking at a day: 1 s and 1 min candles, EMA 9/21 and VWAP, our fills marked and colored by markout, and a Stoch RSI panel. It lives in `hub/`, in the same shape as the founder's botbot research lab:
  - **`hub/web`:** Vite + React + TypeScript + lightweight-charts, with vitest tests.
  - **`hub/api`:** FastAPI, later.
- **Two phases:**
  - **Phase 1 (6c-2b):** frontend only. A small exporter writes the chart and fills as JSON into `data/hub/` (git-ignored), and Vite serves it as static files.
  - **Phase 2 (with the trials):** the API lists runs, starts an `mm_sim` trial (under a second on the slice, D43) and compares two runs. `make dev` starts both, as in botbot.
- **Why not one plain HTML file:** it would be simplest on day one, but the hub will grow to several pages (runs, a run's detail, signal tables, a day picker), and that is where a single file stops scaling. The founder already knows this stack, and botbot's chart components are a working example.
- **One difference from botbot, on purpose:** botbot recomputes EMA and VWAP in TypeScript. The hub doesn't compute any indicator. Every number comes from the C++ `MarketState`, the one checked against Databento (D42). Two calculations could show a chart that disagrees with what the strategy saw.
- **Languages:** simulation in C++, research in Python, display in TypeScript. Only the code goes in git; `node_modules` and the data stay out.
- **Phase 1 as built (6c-2b):**
  - **Exporter (`research/hub_export.py`):** reads `chart_dump`'s CSV and `mm_sim`'s fills CSVs and writes `data/hub/<date>_<symbol>/chart_1s.json`, `chart_1m.json`, `fills_<run>.json`, plus an `index.json` of days and runs. A second export adds runs to the index instead of replacing it. Markouts use `bootstrap.py`'s own formula, in cents a share.
  - **Time:** Eastern wall-clock time is written as if it were UTC. The chart library shows UTC, so its axis reads 09:30 at 09:30 ET with no time-zone code in the page.
  - **Fills grouped per bar:** a sweep fills us several times in one second, and a 1 min bar holds dozens of fills. One arrow per side per bar, colored by the share-weighted markout (green above 0, red below), keeps the chart readable; the legend lists the bar's fills. This grouping is the only arithmetic in the page, and it is display, not an indicator.
  - **Packages:** only what one page needs: react, react-dom and lightweight-charts, plus vite, typescript and vitest for development (76 MB). botbot's versions, pinned exactly. Tailwind, the router and react-query come when a page needs them.
  - **One chart, two panes:** Stoch RSI sits under the price in the same chart, so the two time axes can't drift apart when scrolling.
  - **Check:** the page's run summary for naive 0 µs is 8,140 fills, 468,479 shares, −0.84 ¢ a share at 10 s, the same as RESULTS.md.

### D47. Signal study: flagged fills against the rest, with the tests counted

- **Question (stage 1 of the testing plan):** when the chart said something at the moment of a fill, was that fill worse than the others? This doesn't ask whether an indicator predicts the price, only whether it marks the market maker's bad fills in advance. If it does, a rule on it is worth a C++ trial (stage 2). If it doesn't, no trial gets written.
- **Readings:** each signal reads the snapshot (D45) as −1 (price low or falling), +1 (high or rising) or 0:

  | signal | −1 when | +1 when |
  |---|---|---|
  | 1m trend | 1 min EMA9 < EMA21 | EMA9 > EMA21 |
  | 1s trend | 1 s EMA9 < EMA21 | EMA9 > EMA21 |
  | VWAP | mid < VWAP | mid > VWAP |

- **Bollinger and Stoch RSI were dropped before any result:** the founder doesn't expect them to work now. Testing them anyway would only add tests, and every test added is another chance of a false "real".
- **The EMA rule is deliberately the simplest one:** only the order of the two EMAs, with no range state. The founder expects it to be noisy in a range and, on 1 s bars, mostly noise. Market structure (swings, higher highs and lows) is the next round, with the EMAs as a confirming filter.

- **Flagged:** a buy at −1 or a sell at +1, i.e. a fill against the reading. These are the fills a rule on the signal would have stepped away from.
- **Flagged against the rest, not against "with":** the question is whether buying below the lower band is worse than an ordinary buy. A comparison with only the fills on the other side of the band would answer something else.
- **Measure:** P&L per share (spread + adverse selection) at 1 s and 10 s, flagged minus the rest. The interval is `bootstrap.py`'s paired block bootstrap (5 min blocks). The two groups are resampled with the same blocks, so a block where the price ran hurts both and cancels out.
- **The tests are counted:** 3 signals × 2 horizons × 3 periods (whole day, morning, afternoon) = 18 tests.
  - The 3 at 10 s over the whole day are the primary ones. They also get a Bonferroni-corrected interval: 98.3 % for 3 tests.
  - The rest support the primary ones: a real effect should show in both halves of the day.
  - The count runs across rounds, in the private trial log.
  - On random synthetic fills, with 5 signals (30 tests), 1 came out "flagged worse" at 95 %. That is what multiple testing does, and why only the primary tests count.
- **Readings, not conclusions:** −1 means "low" whether it reads as trend (bad for a buy) or mean reversion (good). Which way the founder expects each signal to go is written in the private trial log **before** the study runs on the day. The script only measures.
- **An upper bound:** "without the flagged fills" ignores that stepping away changes our queue position and the fills we'd get instead. A C++ trial (stage 2) answers that.
- **Fills without the indicator** (warming up, VWAP before 09:30) are left out of that signal's test and counted.

### D48. Market structure: ZigZag swings scaled by ATR, and a HH/HL score

- **Why:** round 1 showed the EMA order can't tell a trend from a range: it calls every moment either up or down (D47). The founder reads trend from structure (higher highs and higher lows), with the EMAs as support. This turns that read into code, as he described it.
- **Swing = a meaningful pullback (ZigZag):** a high is confirmed once a later bar's low is at least delta below it; a low, once a later high is delta above it. It runs on closed bars' highs and lows, so a 1 min structure is what the 1 min chart shows.
- **Delta = c × ATR(14) of the same bars.** A fixed size would make the open's noise look like swings and hide the afternoon's real ones. ATR follows the market, and the same rule on 15 s, 1 min and 5 min bars gives each chart its own structure: the founder's fractal hypothesis, as code. ATR is TradingView's (Wilder, gaps included), so it can be checked on a chart.
- **Score = sign(high₂ − high₁) + sign(low₂ − low₁):** +2 up, −2 down, anything else a range. It changes only when a swing is confirmed. So an uptrend ends at the first lower high (score 0), and a downtrend needs a lower low after it (−2). Dipping under the last low isn't enough. This is the founder's call: "yeni LH/LL oluşması daha sağlıklı".
- **No look-ahead:** a swing is known only at the end of the bar that confirmed it (`confirmed_ns`), always later than the bar that made it.
- **Two ambiguities, resolved conservatively:**
  - A bar that makes a new extreme can't also confirm it: inside one bar the order of high and low is unknown.
  - Before the first swing, one bar that holds both extremes waits for a later bar.
- **During the ATR warm-up** the extremes are followed, but nothing is confirmed.
- **Choosing c:** chosen by eye on the hub, from a few precomputed values, with the fills hidden. Picking it from the results would be a test per value.

### D49. Structure on every chart and every fill; 15 s and 5 min charts

- **Inside `ChartFeed`:** each timeframe's chart now also runs its own `MarketStructure`. `ChartPoint` gains `atr` and `structure`. The structure is just another indicator of the bars: computed in one place, shown to strategies, written to the CSVs and drawn on the hub (D42, D46).
- **Four timeframes:** `MarketState` adds 15 s and 5 min charts to the 1 s and 1 min ones. The founder reads structure on 1 min and 5 min, and thinks 15 s may show it too.
- **On every fill:** `Fill` keeps the 15 s and 5 min points too. The fills CSV gains `structure_1s, _15s, _1m, _5m`: an integer from −2 to +2, empty before there is one.
- **Chart CSV:** gains `atr` (dollars) and `structure` columns, and 15 s and 5 min rows. `chart_dump --swings` also writes every confirmed swing (when it was made, when it was confirmed, and higher/lower than the last). `--c` sets the swing size for all timeframes, so the hub can show several c values side by side.
- **Regression:**
  - The 1 s and 1 min rows are byte-identical in their old 17 columns.
  - The Databento check is unchanged (volume 100 %, indicator code to the last digit).
  - The milestone 5 reports and bootstrap tables are identical, and so are the first 25 columns of every fills CSV.
- **What c = 2 gives on the dev day:**

  | timeframe | swings | regular session up / down / range |
  |---|---|---|
  | 1 s | 3,278 | 30 / 31 / 38 % |
  | 15 s | 186 | 37 / 27 / 36 % |
  | 1 min | 52 | 33 / 35 / 32 % |
  | 5 min | 5 | no score until 15:20 |

  These are descriptive only: no markouts were looked at.
- **Known gap, the 5 min warm-up:** ATR(14) on 5 min bars needs 70 minutes from the 09:00 start, and two highs and two lows of 2 ATR take most of the day. 83 % of the naive fills have no 5 min score. Fixes to weigh in 6c-2b:
  - start the charts at 04:00 (pre-market is thin, but it warms the ATR);
  - carry the previous day's bars over;
  - use a smaller c for 5 min.
  Any of them changes the round 1 inputs slightly, so it's recorded before round 2 runs.
- **Resolved: 5 min is parked** (founder, 23 Nov 2026: "hft için çok üst bir time frame gibi duruyor… şimdilik park edilebilir"). With the charts started at 04:00 the 5 min score does cover the session, but:
  - premarket is thin: 34 K shares from 04:00 to 09:30, against 5.9 M in the regular session;
  - the 5 min structure is far too slow for market making: with 11 swings all day, it called 72 % of the session "down" and saw the 10:15-12:00 rally (about $7) only at 13:55.
  The 5 min chart stays in the data and on the hub, but round 2 doesn't test it, and the charts still start at 09:00. 1 min is the primary timeframe; 15 s is secondary (the fractal question).

### D50. The hub draws the structure, so c can be chosen by eye

- **What it shows:** the ZigZag line through the swings, each swing labeled the way a trader would mark it (HH, HL, LH, LL), and the background shaded by the score (green up, red down, grey range). Every timeframe is there: 1 s, 15 s, 1 min, 5 min. A picker switches between c = 1, 1.5, 2 and 3.
- **The numbers stay in C++:** `research/export_hub.sh` runs `chart_dump` once per c (in parallel, about 2 s on the slice) and `hub_export.py` turns the swings and the score's change points into JSON. The page only carries each score forward to the bars after it, and picks colors.
- **Fills are off when the page opens:** c is chosen with the outcomes hidden. Choosing it while looking at the markouts would be fitting c to the result: one hidden test per value tried (D48).
- **When the shading turns:** a bar's color is the score known at its close, including a swing that bar confirmed. That is what a strategy deciding after that bar sees.
- **The labels sit at the bar that made the swing:** the line and labels show where the highs and lows were. The shading shows when they were known, which is later. The gap between them is the confirmation delay.

### D51. Swing size per timeframe, and round 2 of the signal study

- **c picked by eye, with the fills hidden** (founder, 23 Nov 2026): **1.5 on 1 min** ("iyi duruyor") and **2 on 15 s** ("1.5 biraz noisy"). `MarketStateConfig::swing_c` holds one c per timeframe; 1 s and 5 min keep 2 and aren't tested. `chart_dump --c` still sets one value for all, for the hub's side-by-side views.
- **Round 2 tests** (`signal_study.py --round 2`). 1 min is primary; 15 s supports it (the fractal question):

  | test | flagged fills | the rest |
  |---|---|---|
  | **A**, 1 min structure | buys in a downtrend (−2), sells in an uptrend (+2) | everything else |
  | **B**, A + EMA | the same, only when the 1 min EMA9/21 agree | everything else |
  | **C**, 1 min regime | fills in a range (score not ±2), either side | fills in a trend |
  | 15 s structure, 15 s range | as A and C on 15 s | supporting |

  That is 5 signals × 2 horizons × 3 periods = 30 tests, 3 of them primary, with a Bonferroni interval for 3 (98.3 %).
- **C is a different kind of test:** A and B ask whether being on the wrong side of a trend hurts. C asks whether market making itself does better in a range, whatever the side. It is the founder's strongest prior.
- **What the eye sees vs what the score knows:** on the hub the founder reads 09:40-10:10 as a range, but the real-time score keeps switching between down and range there. The eye sees the finished box; the score only knows the past. Round 2 measures the real-time version, the only one a strategy can use.
- **Regression:** the milestone 5 reports are identical, and so are the first 27 columns of every fills CSV; only `structure_1m` changed (c 2 → 1.5). Round 1's study reproduces to the digit.

### D52. More days: Databento's MBO turned back into ITCH, checked against the native day

- **Why:** two rounds of the signal study ended "right direction, can't tell": one day gives intervals of about ±1 ¢, and a 0.5 ¢ effect needs 4-5 days. Nasdaq's free files are from 2019-2020, too old for a market-making study today. Databento has every XNAS.ITCH day from May 2018 to yesterday, per stock: AAPL is about $0.40 and 0.3 GB a day, against 4-17 GB to download for a whole Nasdaq day.
- **But Databento doesn't sell ITCH:** it sells its own MBO records. `research/dbn_to_itch.py` turns them back into ITCH 5.0 bytes, so the whole C++ side reads any day unchanged. The alternative, a DBN reader in C++, would add a dependency and a second input path into the simulator.
- **How:** every ITCH message became one or more MBO records with the same `sequence` number. Each group is turned back into its message:
  - A → A;
  - C → D or X (tracking each order's shares left);
  - C+A → U;
  - T+F+C → E (or a printable C at another price);
  - F+C → non-printable C;
  - T → P.
  
  A replace must stay one message: a D followed by an A would show, for one message, a book without the order. The mid would jump and the simulator would requote on a book that never existed. Times go from UTC to New York time, summer time included.
- **Validated on 30 Dec 2019** ($0.11), against the native ITCH file:
  - Message counts before 19:00 ET are identical for every book message: A (with F), D, X, U, E, C, and P (with Q). The bought range ended at 19:00 ET; ITCH runs to 20:00.
  - Not in MBO, and read by nothing here: H, I, L, Y (trading status, auction imbalance, market-participant position, short-sale flag).
  - The 7 milestone 5 runs give byte-identical fills CSVs and bootstrap tables. Only the report's locate number differs.
  - The chart CSV holds the same 27,385 rows. Two 15 s rows are written a few messages later, because the native file's imbalance messages (I) closed those bars on an earlier message. The swing file is byte-identical.
- **Known differences, harmless here:** auction prints (Q) come out as P, so they count as trades the same way. Match numbers are new, but nothing reads them.

### D53. The study days: random development days, the latest days held out

- **Development, 10 days:** drawn at random from January-September 2026 by `research/pick_days.py`, seed 20261123. They are 4 Feb, 3 Mar, 9 Mar, 21 May, 27 May, 7 Jul, 23 Jul, 30 Jul, 5 Aug and 2 Sep.
  - A random draw spreads them over many months and market moods.
  - No one picks "interesting" days, which would bias every result.
- **Hold-out, 10 days:** the last 10 trading days to 20 Nov 2026 (9-20 Nov). They are not looked at until a rule is final. Then they give the verdict, once.
  - Being later than every development day, they test what matters: a rule built on the past, used on the future.
- **The calendar:** full trading days only. 2026 holidays and the two early closes are left out, since quoting runs to 16:00. Each day's Databento size was checked before buying; none is near empty.
- **One stock for now:** AAPL, on which the converter (D52) and every result so far stand. A rule that passes on AAPL is then checked once on MSFT, unchanged. A basket comes in milestone 7.
- **Recorded before any of these days was downloaded.** Cost: $8.48 for the 20 days, 7.6 GB before compression.
- **Update, 24 Nov 2026: the hold-out is bought only when a rule is final.** Data that isn't on disk can't be looked at by accident, and the money waits until it has a use. The 10 development days were bought on 24 Nov for $5.51 (busier days than the hold-out, so more than half of $8.48). The hold-out stays these 10 days, decided now.

### D54. The 10 development days arrive: Databento's midnight snapshot, and a replay check per day

- **Bought 24 Nov 2026, $5.51:** AAPL MBO for the 10 development days (D53), each from 08:00 UTC to 02:00 UTC the next day. 1.4 GB as downloaded (88-230 MB a day), 1.3 GB as ITCH; 3.6 M to 10.0 M messages a day.
- **Found on the first day: a snapshot in the middle of the file.** At every UTC midnight (19:00 or 20:00 New York time) Databento writes a book clear (R) and then the whole resting book again as adds, flagged `F_SNAPSHOT` (32). In a file that starts at UTC midnight, that is how it hands over the book. Ours start before Nasdaq's day begins, so the feed has already built the book and the snapshot only repeats it: 4,500-9,600 adds a day for orders that already exist. ITCH has no such message. Converted as-is, the book refused every one as a duplicate (8,342 on 4 Feb).
  - **Fix:** the converter skips and counts snapshot records. 30 Dec 2019 had none (Databento added them later), and its file is byte-identical to before.
  - **The alternative**, ending each day at UTC midnight, would cut the last hour of the post-market in winter, and was not what was bought.
- **Another guard:** ITCH time is "nanoseconds since midnight", so a record from the next New York day would land at 00:00 inside today's file. Records from another New York date than the first record's are skipped and counted. None so far: each day ends at 21:00 or 22:00 New York time.
- **The check, per day (`research/convert_days.sh`):** a full replay through the books must end clean with 0 decode failures, 0 unknown orders, 0 rejected, 0 crossed while trading, and **0 orders resting at the end**. That last one only holds if every add, execution, cancel and replace came through. All 10 days pass, including the 5 in summer time.
  - No native ITCH file exists for these days to compare against. The 2019 check (D52) covers the message mapping; this one covers each day being complete.
  - The 30 Dec 2019 file ends with 919 orders resting, since it was bought only to 19:00 ET. That is expected, which is why the script takes a date prefix.

### D55. Explore freely on the development days, confirm once on the hold-out; the 2026 baseline

- **Founder, 24 Nov 2026:** a strict decision is too hard this early; we need to try things, and trial and error may find something. Two modes, replacing the "primary tests with a Bonferroni level" of D47 and D51 for the development days:
  - **Exploration, on the 10 development days:** any number of tries (signals, combinations, c, thresholds), every one written in the private trial log. Intervals are shown, but there is no pass/fail level: they only rank candidates.
  - **Confirmation, on the 10 hold-out days (D53):** 1-2 candidates, frozen before the data is bought, tested once, with a Bonferroni level over the candidates.
  - **Why:** a single day already showed that a few strict tests end in "can't tell". The hold-out is what makes free exploration safe: the best of many tries looks better than it is ("winner's curse"), and days it never saw correct that.
  - **Always reported together:** cents per share **and** shares traded. Trading less makes the loss per share smaller without any edge (milestone 5: Avellaneda-Stoikov and pull-when-thin).
- **The baseline on the 10 days (6d-1):** naive, 0 µs, quoting 09:31-16:00, as in milestone 5. Descriptive only, no test counted. Cents per share, adverse selection over 10 s:

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

  - **The same story as 2019, every day:** adverse selection is larger than the spread on all 10 days, and 9 of 10 lose. 9 Mar breaks even only on inventory (+0.37 ¢).
  - **Smaller than 2019** (−0.29 ¢ against −0.84 ¢ a share), and about 3 times the fills a day (20-39 K against 8 K): AAPL trades more, and its touch moves more often.
  - Signals have more to work with than one day gave them: 265 K fills against 8 K.

### D56. The signal study over several days: compare within each day, resample within each day

- **The difference is taken within each day, then averaged, each day weighted by its shares.** Pooling all fills would let a day that is bad for everyone pass for a signal: if the busy, volatile day has more flagged fills, the flagged group looks worse even when, on every single day, it is better (Simpson's paradox; the test builds exactly that case: +0.1 ¢ each day, −0.7 ¢ pooled). Within a day, a bad day moves both groups alike and cancels.
  - Weighted by shares, not equally: a day with 2 M shares says more than one with 0.5 M. The same reason as the ratio of sums in D37.
  - A day with no flagged fills (or no others) is left out, not counted as 0.
  - The pooled flagged and rest ¢ are still printed, as a description only.
- **Stratified block bootstrap:** each resample draws every day's 5-minute blocks from that day only, so each day keeps its size and character. The same draws serve every signal, as in D37's pairing.
  - **Rejected:** resampling whole days. It is the most honest about day-to-day variation, but with 10 days its interval is too wide to rank anything. "Days worse" (below) covers that question instead.
  - Rejected: resampling blocks across days, which mixes a quiet day's blocks into a busy one.
- **Days worse:** on how many days the flagged fills did worse. 8 of 10 in the same direction says more than an average that one big day could drive.
- **One day is the old study exactly:** with one day, the draws are bootstrap.py's draws in the same order, and the weighted average of one day is that day's difference. The 30 Dec 2019 rounds 1 and 2 reproduce to the digit (a test checks it too).
- **Speed:** 10 days, 265 K fills, round 2: 4.5 s.

### D57. Trend strength: Kaufman's efficiency ratio on every chart

- **Why:** the structure score (D48) gives a direction but not a strength. A market that makes HH/HL slowly and one that runs both read +2. And "range" there means only "not a clean trend": it includes the turn from one trend to the next. Round 2's exploratory idea was that the structure helps only when the trend is strong (D51).
- **What:** ER = |close now − close n bars ago| / Σ |close-to-close changes| over those n bars. 1 is a straight line, near 0 is a lot of movement going nowhere. It has no direction: the structure gives that.
  - Example over 5 bars: +10 +8 +12 +6 +9 is 45 / 45 = 1.0; +10 −12 +10 −12 +9 is 5 / 53 = 0.09.
  - **n = 10 bars on every timeframe** (founder, 24 Nov 2026), Kaufman's own period: 10 minutes on 1 min, 2.5 minutes on 15 s.
  - A price that didn't move at all is 0 / 0, read as 0: not a trend.
  - Closes, like the EMAs. The path is summed again each bar: with 10 changes that costs nothing, and a running sum of doubles would drift.
- **Rejected:** ADX (better known, but smoothed twice, so late, and hard to explain); regression R² and the variance ratio (two more choices each, for the same question).
- **Where:** each `ChartFeed` (1 s, 15 s, 1 min, 5 min) has one, so it is in every `ChartPoint`, in the chart CSV (`er`), and at every fill (`er_1s, er_15s, er_1m, er_5m`, the last columns, so the first 29 stay byte-identical). The signal study reads them; an older CSV without them reads as missing.
- **Thresholds come next (6e-2):** the hub shades trend and range from the structure and the ER, with the two ER thresholds picked by eye, fills hidden, as c was. The hub only compares the ER with a threshold; it computes nothing.
- **Regression:** the milestone 5 runs and the 10-day baseline give byte-identical first 29 columns and identical reports and bootstrap tables (only the run time line changes).

### D58. Trend vs range on the chart: structure for the direction, ER for the strength

- **Why:** before the ER enters the signal study, its thresholds are picked by eye on the chart, with the fills hidden, as the swing size c was (D48). Seeing the outcome first would fit the thresholds to it.
- **The rule, per bar:**

  | Structure score | ER ≤ low | low < ER < high | ER ≥ high |
  |---|---|---|---|
  | +2 | range (grey) | weak trend up (light green) | strong trend up (dark green) |
  | −2 | range (grey) | weak trend down (light red) | strong trend down (dark red) |
  | 0, ±1 | range (grey) | unclear (no color) | unclear (no color) |

  - A low ER wins over a +2 score: the ER sees the last 10 bars, while the swings behind the score can be much older. A choppy last 10 minutes is a range even inside an old uptrend.
  - An efficient move without a clean structure (score 0 or ±1, ER high) is left uncolored: often the turn from one trend to the next, and calling it either way would be a guess.
  - Missing score or ER (warming up): no color.
- **Opening thresholds 0.3 and 0.6,** the usual reading of Kaufman's ratio; each moves in steps of 0.05 and stops at the other. On 4 Feb 2026's 1 min chart about half the bars are at or below 0.3 and 15 % at or above 0.6.
- **The page computes nothing:** the score and the ER come from C++ (D48, D57); the page only compares the ER with the two thresholds. The ER also has its own pane (0-1) with the two lines.
- **The 10 development days in the hub:** `research/export_days.sh` exports each day with the baseline's fills and the structure at c = 1.5 and 2 only (the defaults, D48), about 13 MB a day. `export_hub.sh` now writes its charts to one folder per day, so days never overwrite each other.
- **Every switch opens on the full day** (09:00-16:00, also for 1 s bars), anchored to the bars themselves; a Full day button goes back after zooming. Each loaded file remembers the day, timeframe and c it was for, and the chart only gets files that match the current choice: one stale file (the last day's ZigZag) used to stretch the time axis over two days and push the new bars off screen.
- **Next:** the thresholds picked here go into the signal study as round 3 (structure + ER), on the 10 days.

### D59. Before building a better trend read: how much could a perfect one be worth? (round 7a)

- **Why:** the founder's question (24 Nov 2026): if the trend were identified very well, would it cut adverse selection? Answer it before spending more on the trend read. And separately: we always join the back of the queue, so we fill mostly when the whole level breaks. Is that a second, separate loss?
- **An oracle sets the ceiling:** a ZigZag leg drawn with hindsight (1 min, c = 1.5): each fill is on an up leg or a down leg, as a chart drawn after the close shows it. It uses the future, so it is never a strategy; it bounds what any trend read at this swing size could do. If even it doesn't help, no real-time read will.
  - Not "the price 1 s later": that would be the adverse move itself, not the trend.
  - Fills inside a turning bar (before or after the extreme?) and after the last swing (its leg never finished) are left out.
- **The same swings in real time:** the leg since the last *confirmed* swing. Oracle minus confirmed = what the confirmation lag costs, with nothing else changed.
- **Five splits, each flagged vs rest with the signal study's machinery (D56):** level broke (nothing left at our price as we filled), oracle leg, confirmed leg, 1 min structure (round 2's A), strong trend (±2 with ER 1m ≥ 0.6, the hub's opening threshold). Plus a cross table, level broke × oracle leg, to see whether the queue loss is the trend loss in disguise.
- **Exploration (D55):** questions and expectations written before the run, results in the private log; 5 primary tests, 30 in all.
- **What it decides:** whether the next step is a ladder (more fills while the level holds, 7b-1), a trend strategy, or a faster trend read.

### D60. The ladder: orders waiting behind the touch, to be ahead when the price gets there

- **Why:** we always join the back of the queue, and the back fills only once the whole level has traded, which is often just before the price moves through it (D33, D59). A firm that wants queue priority leaves orders resting a tick or two behind the touch, so that when the price comes to them they are already well up the queue.
- **The rule (`SimConfig.levels`, `mm_sim --levels n`):** the strategy still names one price per side (the top). The simulator holds an order there and at each of the next n − 1 ticks behind it (bids below, asks above).
  - An order whose price stays in that window is never touched, so it keeps its place in its queue.
  - An order that leaves the window moves to the window's price that has none, nearest the touch first (one message, like the single quote's move); when none is left it is pulled.
  - Example, 2 levels: bid 100.00 → we rest at 100.00 and 99.99. The bid rises to 100.01: a new order joins 100.01 at the back, 100.00 stays where it was in its queue, 99.99 leaves the window and is the one moved. The level at 100.00 breaks: we are already queued at 99.99.
- **The limit counts the whole ladder:** position + every resting order on a side ≤ the limit, as if all of it filled at once. When it binds the deepest order goes first. With one level this is the old rule exactly; with more it binds earlier, which the comparison has to keep in mind.
- **One execution fills at most its own size:** the aggressor reaches our better prices first, so those fill first, and each executed share fills one of ours at most.
- **Why the simulator builds the ladder, not the strategy:** every strategy (naive, Avellaneda-Stoikov, the pull rule, a later trend rule) gets it unchanged, and `levels = 1` is the old simulator: the milestone 5 runs and the 10-day baseline come out byte-identical (fills CSVs, reports and bootstrap tables; only the run time line changes).
- **Data structure:** one `QuoteSide` per level in a `std::vector`, at most 8 (`kMaxLevels`); the per-message bookkeeping uses `std::array<bool, 8>` on the stack, so nothing is allocated on the hot path. A handful of orders, so a linear scan beats a map.
- **Compared paired:** `research/compare_runs.py` takes other runs minus a base run, cents a share within each day, with the signal study's stratified block bootstrap (D56): the same 5 min blocks for both runs. Plus shares traded and the share filled as the level broke. Exploration (D55): expectations written before the run, results in the private log; 2 primary tests at 0 µs, 1 more at 50 µs (`LATENCY_US`), where a ladder already queued at the next level might have offset arriving late.

### D61. How fast would a trend read have to be? The delayed-oracle curve (round 7c)

- **Why:** round 7a found that knowing the 1 min leg with hindsight is worth a lot, and knowing it once its swing is confirmed is worth nothing (D59). In between is a question of time: how fast does the value fade as the trend is learned later? The answer is the speed any trend read must reach.
- **Rule:** the hindsight leg, learned L seconds after its turning bar ends; until then the trader still believes the leg before (legs alternate, so: the opposite direction). L = 0 is the oracle exactly (a test checks it fill for fill). L = 0, 5, 15, 30, 60, 120, 300 s.
  - The first leg of a day has no leg before it: its fills before the lag has passed are left out.
- **Measured:** fills against what is believed minus the rest, at 1 s and 10 s, within days with the stratified block bootstrap (D56), and the share of the no-lag gap left at each L. For context, the median leg length and the median confirmation delay of the real swings.
- **A descriptive curve:** no primary test (14 supporting). It sets the speed a read needs, not whether a rule works. `corrected_level` now returns 95 % for a set with no primary test, instead of dividing by zero.

### D62. Every trend read on one scale: its equivalent lag (round 7d-1)

- **Why:** round 7c gives a ruler: the 1 s gap of a trader who learns the hindsight 1 min leg L seconds late, from L = 0 (the oracle) to the lag where it is worth nothing. Any real-time read can be put on it: a read whose fills-against-it gap equals the 30 s point is "as good as knowing the leg 30 s late". One number per read, comparable across very different reads (structure, EMAs, later candle patterns and order flow).
- **Equivalent lag:** straight lines between the curve's points, read only up to where the curve first reaches 0 (past that point a later belief is the wrong leg, and the curve turns back). A gap at or below the oracle's is 0 s; a gap at or above 0 is "nothing". The curve is computed in the same run, from the same fills and the same bootstrap draws.
- **Also per read:** coverage (share of shares it gives a direction to) and agreement with the hindsight leg where both have one. Agreement alone is not the measure: a read can match the leg most of the time and still be worth nothing, if it is right mid-leg and wrong at the turns.
- **Reads in this round:** the ones already in every fill: structure on 1 min, 15 s and 1 s; a 15 s strong trend (±2 and ER ≥ 0.6); the EMA 9/21 trend on 1 s and 1 min; the confirmed 1 min leg (D59). Exploration, descriptive (no primary test); results in the private log. Next (7d-2): reads that see a turn as it happens, among them price-action setups the founder picks.

### D63. Reads that see a turn as it happens: price-action setups at levels (round 7d-2)

- **Why:** round 7c showed a read must know the 1 min leg within about 30 s of its turn to keep half its value, and round 7d-1 found that no read we had (structure, EMAs, confirmed swings) gets there. The founder's own read is price action in Al Brooks' sense: a bar pattern at a level, seen on a short chart, inside the context of a longer one. 7d-2 codes the two he trades most, on 15 s bars (Brooks' 5 min structure maps onto our 1 min chart, so his trigger bars map onto 15 s), with the 1 min chart as context and the 1 min ATR as the yardstick.
  - **B1, failed breakout at a level (a trap):** a bar pokes past a level by a little (at most a quarter of an ATR) and within two bars a bar closes back on the other side. Read: against the poke.
  - **B2, a pullback ends at the EMA 20 or VWAP:** the 1 min EMAs are stacked (9 > 20 > 50, or the reverse), a 15 s pullback reaches the EMA 20 or VWAP and a reversal bar closes in the trend's direction. Read: with the trend.
  - Each read holds its direction for 45 s after its bar closes, then says nothing. The point is not to trade them but to know which side of our quote is about to be picked off.
- **Step 1, the levels (`levels_at`):** everything a trader would mark, as known at that moment: the last 3 confirmed 1 min swing highs and lows, the day's open, high and low so far (from 09:30; the earlier warm-up bars don't count), VWAP, the 1 min EMA 20, and the round 50 ¢ prices just below and above. Lowest first, a stable sort so equal prices keep a fixed order. A plain vector rebuilt when asked: about a dozen levels, so a scan beats keeping a sorted structure up to date.
- **Step 2, B1 (`FailedBreakout`):** fed each closed 15 s bar with the levels as they were when it opened (so a level the bar itself makes, like a new day high, can't be poked by it) and the 1 min ATR.
  - A poke: the bar opens at or on one side of a level and its high (low) goes past it by more than 0 and at most ¼ ATR.
  - The trap: that bar, or one of the next 2, closes back on the first side. If any bar first goes more than ¼ ATR past, the breakout worked and the poke is dropped; after 2 bars it is dropped too.
  - Traps each way in the same bar cancel (no read). No ATR yet: no read.
  - The read starts when the completing bar ends (nothing earlier is known) and lasts 45 s; a newer trap replaces it.
  - Data structure: a short vector of pokes still waiting, rebuilt each bar. Rarely more than a few, so no index.
- **EMA 20 and 50 on every chart (`ema_mid`, `ema_long`):** added next to the existing 9 and 21, not replacing them, so every older output stays byte-identical. Brooks' EMA is the 20; the 50 completes the 9 / 20 / 50 stack.
- **Exploration (D55):** descriptive, measured on the equivalent-lag scale (D62); expectations written before the run.

## Build

- CMake presets: `debug` (ASan + UBSan, `-Wall -Wextra -Wpedantic -Werror` on our targets) and `release` (benchmarks).
- GoogleTest 1.17.0 fetched at configure time, pinned by SHA-256.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# a real day (release build; the file lives in data/, which git ignores)
cmake --preset release && cmake --build --preset release
./build/release/cpp/itch_extract data/itch/12302019.NASDAQ_ITCH50 AAPL data/itch/12302019.AAPL.ITCH50   # AAPL only, 46 MB: every tool below runs on it in ~1 s
./build/release/cpp/itch_stats data/itch/12302019.NASDAQ_ITCH50
./build/release/cpp/itch_replay data/itch/12302019.NASDAQ_ITCH50 AAPL QQQ MSFT
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50   # latency in µs
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 --as 0.2   # Avellaneda-Stoikov, 0.2 cents lean per lot
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 --pull 0.2   # no quote on a side under 20% of the touch
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 --rebate 0.2   # add a 0.2 cent/share maker rebate
./build/release/cpp/imbalance_study data/itch/12302019.NASDAQ_ITCH50 AAPL   # does queue imbalance predict the mid?
./build/release/cpp/chart_dump data/itch/12302019.NASDAQ_ITCH50 AAPL data/sim/chart/AAPL_chart.csv --swings data/sim/chart/AAPL_swings_c2.csv   # 1 s, 15 s, 1 min, 5 min bars, indicators, structure [--from 09:00] [--vwap-from 09:30] [--c 2: one c for all; default 1.5 on 1 min, 2 elsewhere]

# research (Python, standard library only)
research/.venv/bin/python -m unittest discover -s research
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 0 data/sim/ci/naive_0us.csv
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 0 data/sim/ci/as10_0us.csv --as 1.0
research/.venv/bin/python research/bootstrap.py data/sim/ci/naive_0us.csv data/sim/ci/as10_0us.csv   # first = baseline

# every milestone 5 number in docs/RESULTS.md: 7 runs in parallel + bootstrap tables
research/run_m5.sh data/itch/12302019.NASDAQ_ITCH50   # writes data/sim/m5/

# the hub (D46, D50, D58): export a day (charts for every c + the milestone 5 fills), then open http://127.0.0.1:5174
research/export_hub.sh data/itch/12302019.AAPL.ITCH50 2019-12-30 AAPL
research/export_days.sh AAPL 2026   # the 10 development days: charts, structure at c = 1.5 and 2, baseline fills (D58)
(cd hub/web && npm install && npm test && npm run dev)

# signal study (D47): flagged fills vs the rest, per signal, 18 tests counted
research/.venv/bin/python research/signal_study.py data/sim/m5/naive_0us.csv --round 2   # --round 1: EMA trend and VWAP
research/.venv/bin/python research/signal_study.py data/sim/days/AAPL/*/naive_0us.csv --round 2   # several days (D56)
research/.venv/bin/python research/fill_anatomy.py data/sim/days/AAPL/*/naive_0us.csv   # queue and trend, oracle vs real time (D59); needs export_days.sh
research/.venv/bin/python research/oracle_curve.py data/sim/days/AAPL/*/naive_0us.csv   # the oracle learned L s late, L = 0-300 s (D61)
research/.venv/bin/python research/turn_reads.py data/sim/days/AAPL/*/naive_0us.csv   # each read's equivalent lag on that curve (D62)

# any day from Databento (D52): MBO for one stock -> ITCH 5.0, then every tool runs on it
research/.venv/bin/python research/dbn_to_itch.py data/databento/AAPL_20191230_XNAS_mbo.dbn.zst AAPL data/itch/20191230.AAPL.dbn.ITCH50
research/convert_days.sh AAPL 2026   # every downloaded day: convert (skips done ones) + replay check (D54)
research/run_days.sh AAPL 2026   # baseline on every converted day, one line per day (D55): data/sim/days/AAPL/
research/run_days.sh AAPL 2026 data/sim/days ladder2_0us --levels 2   # any run by name, with mm_sim options (D60)
LATENCY_US=50 research/run_days.sh AAPL 2026 data/sim/days ladder2_50us --levels 2   # the same with 50 µs latency
research/.venv/bin/python research/compare_runs.py naive_0us ladder2_0us ladder3_0us   # runs vs a base, paired by day and block (D60)
research/.venv/bin/python research/pick_days.py   # the development and hold-out days (D53)

# chart bars + indicators vs Databento's bars for the same day (D42; needs the two .dbn.zst files)
research/.venv/bin/python research/check_chart.py data/sim/chart/AAPL_chart.csv data/databento/AAPL_20191230_XNAS_ohlcv-1m.dbn.zst data/databento/AAPL_20191230_XNAS_ohlcv-1s.dbn.zst
```
