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

- **Market data replay tool**
  - Simple CSV-like event format:
    - `ADD,BUY,price,qty`
    - `ADD,SELL,price,qty`
    - `MKT,BUY,qty`
    - `MKT,SELL,qty`
    - `CANCEL,orderId`
    - lines starting with `#` are treated as comments.
  - `trading_replay` app (`app/replay_main.cpp`):
    - reads events from a file,
    - feeds them into `OrderBook`,
    - prints a summary:
      - counts of ADD / MKT / CANCEL,
      - total filled quantity,
      - final best bid/ask.

- **Microbenchmark harness and order book benchmark app**
  - Header-only benchmark harness in `include/utils/benchmark.hpp`:
    - batch-based timing to reduce `clock::now()` overhead and OS noise,
    - per-batch ns/op samples with p50, p95, p99,
    - multi-run aggregation: averages mean, p50, p95, p99 across runs.
  - `trading_bench_order_book` app (`app/bench_order_book_main.cpp`):
    - benchmarks:
      - `OrderBook::add_limit_order`,
      - `OrderBook::execute_market_order`,
      - and a synthetic `empty_loop` to estimate harness overhead.
    - configurable parameters via CLI:
      - `iterations` (operations per run),
      - `runs` (how many times to repeat the benchmark),
      - `batch_size` (operations per timed batch).

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
cd build
ctest --output-on-failure
```

GoogleTest is fetched automatically via `FetchContent` on the first configure step.

---

## Replay tool

Build as above, then from the `build` directory:

```bash
./trading_replay path/to/events.csv
```

Example event file:

```text
# type,side,price,qty_or_id
ADD,BUY,100,10
ADD,SELL,105,5
MKT,BUY,3
CANCEL,1
```

Output includes:

- counts of ADD / MKT / CANCEL events,
- total filled quantity,
- final best bid / best ask.

---

## Microbenchmarks

### Benchmark harness overview

The microbenchmark harness lives in `include/utils/benchmark.hpp` and provides:

* `run_benchmark_with_percentiles_batched(...)`:

  * measures operations in batches to amortize timer overhead,
  * collects per-batch ns/op samples,
  * computes p50 / p95 / p99.
* `run_multi_benchmark(...)`:

  * runs a benchmark multiple times,
  * aggregates mean / p50 / p95 / p99 across runs,
  * prints results in a compact format.

The `trading_bench_order_book` app (`app/bench_order_book_main.cpp`) uses this harness
to benchmark:

* an empty loop (to estimate harness overhead),
* `OrderBook::add_limit_order`,
* `OrderBook::execute_market_order`.

### Running the benchmark

From the `build` directory (Release build):

```bash
./trading_bench_order_book [iterations] [runs] [batch_size]

# Example (defaults):
./trading_bench_order_book
# iterations = 200000, runs = 5, batch_size = 128
```

### Baseline results

Environment:

* Linux VPS, single thread
* Release build (`-O3`), C++20
* Command: `./trading_bench_order_book` (iterations = 200000, runs = 5, batch size = 128)

These numbers are a **baseline** for future optimizations.

#### `empty_loop` (harness overhead)

| Version  | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -------- | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline |       0.34 |   0.30 |   0.39 |   0.43 |     200000 |    5 |        128 |

#### `OrderBook::add_limit_order`

| Version  | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -------- | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline |     283.70 | 202.89 | 380.54 | 514.32 |     200000 |    5 |        128 |

#### `OrderBook::execute_market_order`

| Version  | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -------- | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline |      58.51 |   7.01 | 375.57 | 488.27 |     200000 |    5 |        128 |

Future changes to `OrderBook` (e.g. different containers, memory layout, allocators)
can be measured with the same command and added as new rows in these tables.

---

## Multithreaded pipeline benchmark (experimental)

The `trading_mt_bench` app (`app/mt_bench_main.cpp`) benchmarks a **two-thread pipeline**:

* a **producer** thread:

  * generates synthetic events (ADD / MARKET / CANCEL) using a simple random model,
  * timestamps each event and pushes it into a lock-free SPSC ring buffer (`utils::SpscQueue`),
* a **consumer** thread:

  * pops events from the queue,
  * applies them to `OrderBook` (`add_limit_order_with_id`, `execute_market_order`, `cancel`),
  * records end-to-end latency for each event from enqueue → processed.

This benchmark focuses on:

* total throughput (events per second),
* end-to-end event latency distribution (p50 / p95 / p99) across the full pipeline.

> Note: the current version (`mt_bench v1`) still uses `std::this_thread::yield()` as a simple backoff strategy in both producer and consumer.
> This makes the latency numbers sensitive to OS scheduler jitter and represents a **pessimistic baseline** before tighter spin/backoff and CPU pinning.

Example command (from `build`):

```bash
./trading_mt_bench 200000 42
```

Example output:

```text
mt_bench: processed 200000 events in 0.0706884 s
  throughput: 2.82932e+06 events/s
  mean:       353.442 ns/event
Latency (enqueue -> processed):
  p50: 23199405 ns
  p95: 32158376 ns
  p99: 33408312 ns
Final best bid valid=0, price=0, qty=0
Final best ask valid=1, price=101, qty=2
```

Summary (current state, `mt_bench v1`):

| Mode        | Events | Threads               | Queue            | Throughput (Mev/s) | Mean ns/event | Latency p50 ns | p95 ns     | p99 ns     |
| ----------- | ------ | --------------------- | ---------------- | ------------------ | ------------- | -------------- | ---------- | ---------- |
| mt_bench v1 | 200000 | 2 (producer / engine) | SPSC ring buffer | 2.83               | 353           | 23_199_405     | 32_158_376 | 33_408_312 |

Planned improvements:

* replace `std::this_thread::yield()` with a tighter spin/backoff strategy,
* optionally add a warm-up phase and re-measure p50 / p95 / p99,
* experiment with different build flags (`-O2` / `-O3` / `-march=native`) and compare throughput and latency under the same event flow,
* experiment with different `Level` containers (`std::list` vs `std::deque` / flat structures) and measure their impact on both microbenchmarks and the pipeline.


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
              << " qty=" << bb.qty << "
";

    std::cout << "Best ask: " << (ba.valid ? std::to_string(ba.price) : "none")
              << " qty=" << ba.qty << "
";

    // Execute a market buy for quantity 3 – consumes from asks
    auto result = book.execute_market_order(Side::Buy, 3);

    std::cout << "Market buy requested=" << result.requested
              << " filled=" << result.filled
              << " remaining=" << result.remaining << "
";

    return 0;
}
```

---

## Future work / ideas

This project is intentionally minimal. Possible extensions:

1. **More realistic market data replay**
   - timestamps and sequence numbers,
   - basic latency statistics,
   - different event distributions (e.g. bursty flow, quote stuffing).

2. **More advanced microbenchmarks & profiling**
   - `rdtsc`-based timing,
   - CPU affinity / pinning,
   - comparisons of different data structures (flat maps, arrays, custom pools).

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
