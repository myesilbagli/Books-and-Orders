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
| ITCH 5.0: message decoder (`A F E C X D U P R`) | `cpp/include/orderbook/itch/messages.hpp`, `cpp/src/itch/messages.cpp` | done |
| ITCH 5.0: message splitter (`ItchReader`, zero-copy) | `cpp/include/orderbook/itch/reader.hpp` | done |
| ITCH 5.0: memory-mapped file (`MappedFile`, RAII) | `cpp/include/orderbook/itch/mapped_file.hpp`, `cpp/src/itch/mapped_file.cpp` | done |
| ITCH 5.0: `scan` (count types, check decoders) + `itch_stats` tool | `cpp/include/orderbook/itch/scan.hpp`, `cpp/src/itch/scan.cpp`, `cpp/tools/itch_stats.cpp` | done |
| Book: `reduce` (ITCH E/C/X) and `replace` (ITCH U) | `cpp/include/orderbook/book.hpp`, `cpp/src/book.cpp` | done |
| Replay: one book per stock (by locate) + problem counters | `cpp/include/orderbook/replay.hpp`, `cpp/src/replay.cpp` | done |
| Real-day replay run (`itch_replay`): checks + speed | — | milestone 3, next |
| Market-making simulator (queue-aware fills) | — | milestone 4 |
| Quoting strategy, markouts, P&L attribution | — | milestone 5 |
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
```
