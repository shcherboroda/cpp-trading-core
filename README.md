# cpp-trading-core

`cpp-trading-core` is an experimental C++17/20 trading core library built as a learning project around
limit order books, market-data style event processing and low-latency friendly design.

The goal is **not** to build a full production trading system, but to have a small, clean codebase that can be:
- used to practice modern C++ (RAII, STL, containers, iterators),
- extended towards low-latency / HFT-style architectures,
- referenced in interviews as a concrete example of systems-level C++ work.

> ⚠️ This project is for educational and research purposes only.  
> It is **not** production-ready code and **not** financial advice.

---

## Features (current state)

- **Single-instrument in-memory limit order book**
  - Separate books for bids and asks.
  - Price–time priority within each price level.
  - `add_limit_order(Side, Price, Quantity)`:
    - inserts a limit order into the appropriate side,
    - maintains a per-order index for O(1) cancellation by id.
  - `cancel(OrderId)`:
    - removes the order from both the price level and the index,
    - cleans up empty price levels.
  - `execute_market_order(Side, Quantity)`:
    - for `Buy` – consumes liquidity from the ask book starting from best ask (lowest price),
    - for `Sell` – consumes liquidity from the bid book starting from best bid (highest price),
    - respects price–time priority within each level,
    - returns `MatchResult { requested, filled, remaining }`.

- **Best quotes / book inspection**
  - `best_bid()` / `best_ask()` return:
    - price of the best level,
    - total quantity at that level,
    - `valid` flag when the side is non-empty.
  - `empty()` to check if the whole book is empty.

- **Unit tests**
  - Tests are written with GoogleTest (fetched via CMake `FetchContent`):
    - empty book state,
    - basic add-limit behaviour and best bid/ask,
    - market order scenarios:
      - partial and full fills,
      - multiple levels on each side,
      - correct best-level selection (best bid/ask first),
    - cancellation scenarios:
      - cancelling the only order on a side,
      - cancelling one of multiple orders on the same price level,
      - cancelling bids does not affect asks and vice versa,
      - cancelling a non-existing id leaves the book unchanged.

---

## Design overview

### Data structures

Internally the order book uses:

```cpp
struct Order {
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;
};

using Level  = std::list<Order>;
using Levels = std::map<Price, Level>; // price -> FIFO queue of orders at this price

Levels bids_; // buy orders
Levels asks_; // sell orders

struct OrderRef {
    Side side;
    Price price;
    Level::iterator it; // iterator into the list at a given price level
};

std::unordered_map<OrderId, OrderRef> index_;
OrderId next_id_{1};
```

Key points:

- **Price–time priority**:
  - `Levels` groups orders by `Price`,
  - each `Level` is a `std::list<Order>` to preserve FIFO order at that price,
  - market orders always consume the best price first (best ask for Buy, best bid for Sell),
  - within a price level, orders are filled in FIFO order using `Level::front()`.
- **Order id index**:
  - `index_` maps `OrderId` to a concrete list iterator at a price level,
  - cancellation by id becomes O(1) on average:
    - find in `index_`,
    - erase from the list,
    - possibly remove the now-empty price level from the map.

This is intentionally **not** the most cache-friendly or low-latency optimal structure,
but it is:
- easy to reason about,
- easy to extend,
- good enough to discuss complexity and potential optimizations in interviews.

---

## Complexity (big-picture)

Let **N** be the number of price levels and **K** the number of orders in a given level.

- `add_limit_order`:
  - `std::map::find/emplace` – O(log N),
  - `std::list::push_back` – O(1),
  - `unordered_map` insert – O(1) average.
  - **Total:** ~O(log N).

- `cancel(OrderId)`:
  - `unordered_map::find` – O(1) average,
  - list erase via iterator – O(1),
  - optional `map::erase` of an empty level – O(log N).
  - **Total:** ~O(log N) in the worst case, O(1) when the level stays non-empty.

- `execute_market_order(Side, qty)`:
  - walks price levels from best bid/ask outwards:
    - each fully consumed level is erased in O(log N),
    - partially consumed levels stay,
    - within each level, orders are consumed in O(K) (K = number of orders at that price).
  - **Total:** proportional to the number of touched orders and levels.

This gives a good base for discussing:
- where the bottlenecks are,
- what could be improved for low-latency (cache-friendly containers, custom allocators, flat maps, etc.).

---

## Build & run

### Requirements

- CMake ≥ 3.16
- A C++20-capable compiler (GCC, Clang, MSVC)

### Configure & build

```bash
mkdir -p build
cd build
cmake .. -DBUILD_TESTING=ON
cmake --build .
```

### Run tests

```bash
ctest --output-on-failure
```

GoogleTest is fetched automatically via `FetchContent` on the first configure step.

---

## Example usage

```cpp
#include "trading/order_book.hpp"
#include <iostream>

int main() {
    using namespace trading;

    OrderBook book;

    // Add some liquidity
    book.add_limit_order(Side::Buy,  100, 10); // bid: 100 x 10
    book.add_limit_order(Side::Sell, 105,  5); // ask: 105 x 5

    auto bb = book.best_bid();
    auto ba = book.best_ask();

    std::cout << "Best bid: " << (bb.valid ? std::to_string(bb.price) : "none")
              << " qty=" << bb.qty << "\n";

    std::cout << "Best ask: " << (ba.valid ? std::to_string(ba.price) : "none")
              << " qty=" << ba.qty << "\n";

    // Execute a market buy for quantity 3 – consumes from asks
    auto result = book.execute_market_order(Side::Buy, 3);

    std::cout << "Market buy requested=" << result.requested
              << " filled=" << result.filled
              << " remaining=" << result.remaining << "\n";

    return 0;
}
```

---

## Future work / ideas

This project is intentionally minimal. Possible extensions:

1. **Market data replay tool**
   - Text/CSV format for events: `ADD`, `CANCEL`, `MKT`,
   - `replay` app that reads a file and feeds the book,
   - statistics on processed events / total filled volume.

2. **Micro-benchmarks**
   - Generate synthetic order flow,
   - measure per-event latency and throughput (chrono / rdtsc),
   - compare different data structures or configurations.

3. **Multi-symbol support**
   - `OrderBook` per symbol,
   - `BookManager` that routes events by symbol id.

4. **Concurrency**
   - single-writer / multi-reader model,
   - SPSC or MPSC queues between feed handler, book and strategy,
   - later: experiments with lock-free structures.

5. **Memory & performance**
   - custom allocators / pools for orders and levels,
   - more cache-friendly layouts (e.g. flat containers, struct-of-arrays),
   - hot-path micro-optimizations based on profiling.

Even in its current form, this project is useful as a concrete example of C++ systems code
to discuss in interviews (data structures, trade-offs, complexity and further evolution).


**Author**: https://www.linkedin.com/in/sergey-shcherbakov/