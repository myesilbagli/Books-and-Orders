# Design

A limit order book and matching engine in C++20 (`cpp/`), and on top of it a market-making simulator and strategy research (`research/`).

## Goal

Build a market-making model that uses discretionary market reading (trend days vs range days) as a signal, and test it honestly: replay real Nasdaq ITCH days order by order, put the strategy's quotes into the real queue, and count a fill only when the queue ahead of it is used up. Success is measured with markouts and a P&L split into spread captured, adverse selection and inventory.

Why this order: the simulator is only as honest as the book underneath it. Most retail backtests assume a fill the moment price touches a level; a queue-aware replay is what removes that bias.

## Components

| Component | File | Status |
|---|---|---|
| Core types (`Price`, `Quantity`, `OrderId`, `Sequence`, `Side`, `Order`) | `cpp/include/orderbook/order.hpp` | done |
| Order book: add, cancel, best bid/ask, queue per price | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| Matching: `submit` with price-time priority, `Trade` records | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| ITCH 5.0: big-endian byte readers | `cpp/include/orderbook/itch/bytes.hpp` | done |
| ITCH 5.0: message decoder (`A F E C X D U P R H`) | `cpp/include/orderbook/itch/messages.hpp`, `cpp/src/itch/messages.cpp` | done |
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
| Microprice, P&L attribution | — | milestone 5 (5d, 5e) |
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

### D31. Avellaneda-Stoikov, adapted to a one-tick market

- **Formula (ticks):** risk = γ·σ²·H; reservation = mid − q·risk; half spread = base + risk/2; bid/ask = reservation ∓ half spread, rounded to the nearest cent. The simulator then holds both at or behind the touch (D30).
- **Choices:**
  - **q in lots** (one lot = one 100-share quote): AS's q counts units of risk. In shares, the widening term would be a 1/100 of a lot's lean, i.e. nothing.
  - **Fixed holding time H = 10 s** instead of "time to the close": in the original the lean fades to zero towards 16:00, the opposite of what a desk wants. 10 s is about where the markouts stop getting worse (D27).
  - **Fixed base half spread (0.5 cent)** instead of estimating k: for small γ, AS's (2/γ)·ln(1 + γ/k) ≈ 2/k, a constant. Half a cent means a flat book joins a 1-cent market. Estimating k from fill rates is a later refinement.
  - **σ² on a 1 s grid over 5 minutes** (D29). It reads ~15 % low against 10 s, but a constant factor is absorbed by γ, and 1 s gives 10x the samples.
  - **Nearest-cent rounding:** always rounding bids down and asks up would step back from the touch whenever the widening is a fraction of a cent, even when flat.
  - **No quotes until σ² is ready.** In practice it's ready long before 09:31 (the strategy is asked from the first two-sided book).
  - **γ is set by "cents of lean per lot at average volatility"** (`mm_sim --as 0.2`), using AAPL's day-average σ² = 40,000 ticks²/s, so runs are comparable without thinking in units of 1/tick.
- **What it becomes on AAPL:** the spread is one tick and we never improve on the touch, so leaning can't make the ask more aggressive when long. The only effect is stepping the bid (or, short, the ask) back from the touch, by an amount that grows with inventory and volatility. On a stock with a wide spread the full formula would act.
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

## Build

- CMake presets: `debug` (ASan + UBSan, `-Wall -Wextra -Wpedantic -Werror` on our targets) and `release` (benchmarks).
- GoogleTest 1.17.0 fetched at configure time, pinned by SHA-256.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# a real day (release build; the file lives in data/, which git ignores)
cmake --preset release && cmake --build --preset release
./build/release/cpp/itch_stats data/itch/12302019.NASDAQ_ITCH50
./build/release/cpp/itch_replay data/itch/12302019.NASDAQ_ITCH50 AAPL QQQ MSFT
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50   # latency in µs
./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 --as 0.2   # Avellaneda-Stoikov, 0.2 cents lean per lot
```
