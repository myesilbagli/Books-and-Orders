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
| ITCH 5.0 parser | — | milestone 2 |
| Real-day replay + validation | — | milestone 3 |
| Market-making simulator (queue-aware fills) | — | milestone 4 |
| Quoting strategy, markouts, P&L attribution | — | milestone 5 |
| Trend/range signal for the market maker | — | milestone 6 |

## Data flow

```
order events (tests now; ITCH from milestone 2)
        │
        ▼
OrderBook::add / cancel          (replay: rest as told, no matching)
OrderBook::submit → [Trade...]   (our own orders: match first, rest the remainder)
        │
        ├── bids_  std::map<Price, std::list<Order>, std::greater>   best = begin() = highest
        ├── asks_  std::map<Price, std::list<Order>>                 best = begin() = lowest
        └── index_ std::unordered_map<OrderId, {side, price, list iterator}>
```

The book holds only **displayed** orders. Nasdaq ITCH does not publish hidden, midpoint or the reserve part of iceberg orders; they show up only when they trade. Displayed orders keep priority over non-displayed ones at the same price, so queue position at a price is still correct, but hidden liquidity at a better price can take fills we would otherwise expect (the simulator in milestone 4 will be slightly optimistic for that reason).

## Decisions

### D1. Prices are signed 64-bit integer ticks

- **Choice:** `using Price = std::int64_t`, measured in ticks (for US equities, 1 tick = $0.01, so `10025` = $100.25).
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

## Build

- CMake presets: `debug` (ASan + UBSan, `-Wall -Wextra -Wpedantic -Werror` on our targets) and `release` (benchmarks).
- GoogleTest 1.17.0 fetched at configure time, pinned by SHA-256.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```
