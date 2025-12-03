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

Internally the order book uses a flat storage for orders plus two
price books (bids / asks) that reference orders by index:

```cpp
struct Order {
OrderId id{0};
Side side{Side::Buy};
Price price{0};
Quantity qty{0};
bool active{false};
};


using OrderIndex = std::uint32_t;


struct Level {
// Indices into the flat `orders_` array.
std::vector<OrderIndex> indices;
};


using BidBook = std::map<Price, Level, std::greater<Price>>;
using AskBook = std::map<Price, Level, std::less<Price>>;


class OrderBook {
BidBook bids_; // best bid at bids_.begin()
AskBook asks_; // best ask at asks_.begin()


std::vector<Order> orders_; // flat storage
std::vector<OrderIndex> free_indices_; // reusable slots in `orders_`
std::unordered_map<OrderId, OrderIndex> id_to_index_;
};
```

Key points:

- Best levels are always at bids_.begin() and asks_.begin() due to comparators.
- Each Level holds indices into a flat orders_ array:
  - this keeps per-price queues cache-friendly;
  - cancelled/filled orders are marked inactive and their indices are recycled.
- A hash map id_to_index_ provides O(1)-ish lookup for cancels.

On top of that, a unified matching core:
```cpp
template <typename Book, typename PricePredicate>
Quantity match_on_book(Book& book, Quantity qty, PricePredicate&& should_cross);
```
implements both:
- market order execution (execute_market_order), and
- the taker leg of incoming limit orders (match_incoming_limit),
with different price predicates for BUY/SELL and limit prices.

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

See [docs/performance.md](docs/performance.md) for detailed benchmarks and profiling notes.

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

| Version      | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -----------: | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline     |       0.34 |   0.30 |   0.39 |   0.43 |     200000 |    5 |        128 |
| flat storage |       0.44 |   0.41 |   0.45 |   0.53 |     200000 |    5 |        128 |

#### `OrderBook::add_limit_order`

| Version      | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -----------: | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline     |     283.70 | 202.89 | 380.54 | 514.32 |     200000 |    5 |        128 |
| flat storage |     242.74 | 199.24 | 350.24 | 467.71 |     200000 |    5 |        128 |

#### `OrderBook::execute_market_order`

| Version      | Mean ns/op | p50 ns | p95 ns | p99 ns | Iterations | Runs | Batch size |
| -----------: | ---------: | -----: | -----: | -----: | ---------: | ---: | ---------: |
| baseline     |      58.51 |   7.01 | 375.57 | 488.27 |     200000 |    5 |        128 |
| flat storage |      17.52 |  16.74 | 19.74  | 22.33  |     200000 |    5 |        128 |

For a more detailed before/after comparison and additional context see docs/performance.md.

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

| Mode         | Events | Threads              | Queue            | Throughput (Mev/s) | Mean ns/event | Latency p50 ns | p95 ns  | p99 ns  |
|--------------|--------|----------------------|------------------|--------------------|---------------|----------------|---------|---------|
| mt_bench v1  | 200000 | 2 (producer/engine)  | SPSC, big buffer | 2.83               | 353           | 23_199_405     | 32_158_376 | 33_408_312 |
| mt_bench v2  | 200000 | 2 (producer/engine)  | SPSC, 4096, warmup | 2.62             | 382           | 1_546_276     | 1_655_484 | 1_720_337 |

Planned improvements:

* replace `std::this_thread::yield()` with a tighter spin/backoff strategy - NOTE: backoff with current implementation on VPS shown worst results,
* experiment with different build flags (`-O2` / `-O3` / `-march=native`) and compare throughput and latency under the same event flow,
* experiment with different `Level` containers (`std::list` vs `std::deque` / flat structures) and measure their impact on both microbenchmarks and the pipeline.

---

### Bybit REST order book snapshot → OrderBook build

We can fetch a real order book snapshot from Bybit via REST and rebuild our `OrderBook` from it multiple times to measure performance.

Example commands (Release build):

```bash
./trading_bybit_orderbook_snapshot                    # BTCUSDT, limit=50, runs=1000
./trading_bybit_orderbook_snapshot BTCUSDT 50 5000
./trading_bybit_orderbook_snapshot ETHUSDT 100 2000
```

Results on my VPS (Release, single thread):

| Symbol  | Depth (bids×asks) | Levels | Runs | ns/snapshot | ns/level |
| ------- | ----------------- | ------ | ---- | ----------- | -------- |
| BTCUSDT | 50×50             | 100    | 1000 | 32,271.4    | 322.7    |
| BTCUSDT | 50×50             | 100    | 5000 | 29,808.1    | 298.1    |
| ETHUSDT | 100×100           | 200    | 2000 | 88,431.6    | 442.2    |

HTTP request time for the snapshot itself is roughly 270–630 ms and is dominated by network latency and Bybit’s API, not by the order book implementation.

These numbers are consistent with the microbenchmarks for `OrderBook::add_limit_order` and show that the engine can rebuild a 50×50–100×100 depth snapshot in tens of microseconds.

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

### 6. Live Bybit WebSocket orderbook (BTCUSDT, depth=50)

We also measure live WebSocket feed processing for the aggregated orderbook:

* Exchange: Bybit (public WS)
* Symbol: `BTCUSDT`
* Topic: `orderbook.50.BTCUSDT`
* Message types: 1 initial `snapshot` + `delta` updates
* Instrumented in `trading_bybit_ws_orderbook_live`

Example run:

```bash
./trading_bybit_ws_orderbook_live BTCUSDT 1000
```

Output (summarized):

```text
Messages: 999 (snapshots=1, deltas=998)

Processing time (handler):
  mean: 110310 ns
  p50 : 104713 ns
  p95 : 161639 ns
  p99 : 215189 ns

Data latency (local_now_ms - msg.ts_ms):
  mean: 92.38 ms
  p50 : 90 ms
  p95 : 93 ms
  p99 : 115 ms
```

Summary table (baseline run, Release build on VPS):

| Symbol  | Depth | Messages | Handler mean ns |  p50 ns |  p95 ns |  p99 ns | Latency mean ms | p50 ms | p95 ms | p99 ms |
| ------- | ----- | -------- | --------------: | ------: | ------: | ------: | --------------: | -----: | -----: | -----: |
| BTCUSDT | 50    | 999      |         110,310 | 104,713 | 161,639 | 215,189 |           92.38 |     90 |     93 |    115 |

Notes:

* The handler includes JSON parsing, applying Bybit deltas to an aggregated level book
  and rebuilding `trading::OrderBook` on every message.
* Even with a full rebuild on each `delta`, handler time stays in the ~0.1–0.2 ms range.
* Data latency is dominated by network + exchange processing (~90–115 ms here),
  so CPU-side processing is only a small fraction of end-to-end latency.

---

### 7. Benchmarks summary

This section summarizes the main performance numbers across the different stages of the project.

#### 7.1 OrderBook microbench (single-thread)

Config (for all rows):

* iterations: 200,000
* runs: 5
* batch size: 128
* warmup: 20,000

`trading_bench_order_book` (steady_clock-based), after subtracting empty loop overhead.

| Operation            | runs | iters   | batch | mean ns/op |  p50 ns |  p95 ns |  p99 ns | Mops/s |
| -------------------- | ---- | ------- | ----- | ---------: | ------: | ------: | ------: | -----: |
| add_limit_order      | 5    | 200,000 | 128   |     333.70 | 308.761 | 451.194 | 552.638 |   3.00 |
| execute_market_order | 5    | 200,000 | 128   |       9.62 |   8.523 |  12.971 |  18.625 | 103.90 |

TSC-based variant (`trading_bench_order_book_tsc`) gives similar values with slightly lower overhead (empty loop ~0.14 ns/op), confirming the order of magnitude above.

---

#### 7.2 Multi-threaded pipeline benchmark (`trading_mt_bench`)

Single-producer / single-consumer pipeline:

* bounded lock-free-ish queue
* explicit warmup
* producer uses `std::this_thread::yield()` when the queue is full

Example run:

```bash
./trading_mt_bench 200000 42
```

Summary:

| Mode                                      | events  | throughput (M ev/s) | mean ns/event | p50 latency ns | p95 latency ns | p99 latency ns |
| ----------------------------------------- | ------- | ------------------: | ------------: | -------------: | -------------: | -------------: |
| SP/SC, bounded queue + warmup + `yield()` | 200,000 |                2.62 |        382.27 |      1,546,276 |      1,655,484 |      1,720,337 |

Latency here is measured as `(dequeue_ts - enqueue_ts)` inside the benchmark.

---

#### 7.3 Bybit HTTP snapshots → `trading::OrderBook`

Public REST snapshots for spot orderbook, measured in `trading_bybit_orderbook_snapshot`.

Config:

* Exchange: Bybit
* Endpoint: `/v5/market/orderbook`
* Depth: `limit` levels per side (bids + asks)

Benchmark summary:

| Symbol  | Depth (per side) | runs | total levels (bids+asks) | mean ns/snapshot | mean ns/level |
| ------- | ---------------: | ---: | -----------------------: | ---------------: | ------------: |
| BTCUSDT |               50 | 5000 |                      100 |         29,808.1 |        298.08 |
| ETHUSDT |              100 | 2000 |                      200 |         88,431.6 |        442.16 |

This measures building a fresh `trading::OrderBook` from the JSON snapshot on every run.

---

#### 7.4 Bybit WS snapshots → `trading::OrderBook`

WebSocket snapshot (`type = snapshot`) for the same aggregated orderbook, measured in `trading_ws_orderbook_snapshot`.

Example run:

```bash
python tools/bybit_ws_orderbook.py | ./trading_ws_orderbook_snapshot BTCUSDT 2000
```

Summary:

| Symbol  | Depth (per side) | runs | total levels (bids+asks) | mean ns/snapshot | mean ns/level |
| ------- | ---------------: | ---: | -----------------------: | ---------------: | ------------: |
| BTCUSDT |               50 | 2000 |                      100 |         21,603.7 |        216.04 |

The WS snapshot path is slightly faster per level than the HTTP snapshot path, primarily because we skip the HTTP client overhead and work directly from an already-received JSON message.

---

#### 7.5 Live Bybit WS orderbook (handler + data latency)

For detailed numbers see section **6. Live Bybit WebSocket orderbook**.
Here is a compact summary table for the baseline run:

| Symbol  | Depth | Messages | Handler mean ns |  p50 ns |  p95 ns |  p99 ns | Latency mean ms | p50 ms | p95 ms | p99 ms |
| ------- | ----: | -------: | --------------: | ------: | ------: | ------: | --------------: | -----: | -----: | -----: |
| BTCUSDT |    50 |      999 |         110,310 | 104,713 | 161,639 | 215,189 |           92.38 |     90 |     93 |    115 |

Handler time includes parsing the WS JSON and applying deltas to a simple level book + rebuilding `trading::OrderBook` on every message. Data latency is `local_now_ms - exchange_ts_ms` and is dominated by network + exchange processing, not CPU time.

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
