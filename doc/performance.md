# Performance & Benchmarking

This document tracks how the performance of `cpp-trading-core` evolves over time:

* what exactly is measured,
* on which hardware / build configuration,
* which tools are used,
* what each optimization actually improves.

---

## 1. Measurement setup

### 1.1 Environment

*(fill in your actual VPS details once, so you can always reference them)*

* OS: Ubuntu XX.XX (64-bit)
* Kernel: `uname -r`
* CPU: `lscpu` (model, cores, threads)
* Compiler: `g++ --version`
* Build type: `Release` with `-O3`, `-DNDEBUG`
* Extra flags:

  * `-fno-omit-frame-pointer` (for better `perf` stack traces)

### 1.2 Tools

We use a few different layers of measurement:

1. **Microbenchmarks (C++):**

   * `include/utils/benchmark.hpp`
   * Executables:

     * `trading_bench_order_book` — microbench for `OrderBook`
     * `trading_bench_order_book_tsc` — the same with TSC-based timer

2. **Integration benchmarks with real exchange data (Bybit):**

   * `trading_bybit_orderbook_snapshot` — REST snapshot + `OrderBook` build
   * `trading_ws_orderbook_snapshot` — WebSocket snapshot + `OrderBook` build
   * `trading_bybit_ws_orderbook_live` — live WS orderbook with per-message stats

3. **CPU profiling:**

   * `perf record` / `perf report` on Linux

4. **(Optional, future) memory/alloc profiling:**

   * `valgrind --tool=massif` or `heaptrack` for heap usage

---

## 2. Microbenchmarks: OrderBook

We focus on two core operations:

* `add_limit_order_with_id` (inserts a new limit order)
* `execute_market_order` (matches a market order against the book)

All microbenchmarks use the same harness in `include/utils/benchmark.hpp`:

```cpp
BenchmarkConfig cfg;
cfg.iterations = 200000;
cfg.runs       = 5;
cfg.batch_size = 128;
cfg.warmup     = 20000;

// Example:
run_benchmark_multi("OrderBook::add_limit_order", cfg, [&](auto& timer) {
    for (int i = 0; i < cfg.iterations; ++i) {
        benchmark::scope s(timer);
        // call OrderBook::add_limit_order_with_id(...)
    }
});
```

### 2.1 Baseline: `trading_bench_order_book` (clock-based timer)

Command:

```bash
./trading_bench_order_book
```

Config (at the time of measurement):

* `iterations = 200000`
* `runs       = 5`
* `batch_size = 128`
* `warmup     = 20000`

Results:

| Benchmark                         | mean ns/op | p50 ns | p95 ns | p99 ns | Notes                      |
| --------------------------------- | ---------: | -----: | -----: | -----: | -------------------------- |
| `empty_loop`                      |       0.40 |   0.39 |   0.41 |   0.57 | overhead of harness        |
| `OrderBook::add_limit_order`      |     333.70 | 308.76 | 451.19 | 552.64 | `add_limit_order_with_id`  |
| `OrderBook::execute_market_order` |       9.62 |   8.52 |  12.97 |  18.63 | “happy path” short matches |

### 2.2 Baseline: `trading_bench_order_book_tsc` (TSC timer)

Command:

```bash
./trading_bench_order_book_tsc
```

Same config (200k iters, 5 runs, batch=128, warmup=20000).

Results:

| Benchmark                             | mean ns/op | p50 ns | p95 ns | p99 ns | Notes              |
| ------------------------------------- | ---------: | -----: | -----: | -----: | ------------------ |
| `empty_loop_tsc`                      |       0.14 |   0.12 |   0.17 |   0.17 | TSC-based overhead |
| `OrderBook::add_limit_order_tsc`      |     283.42 | 258.43 | 418.84 | 529.20 | TSC-derived timing |
| `OrderBook::execute_market_order_tsc` |       7.95 |   7.00 |  10.70 |  13.37 | TSC-derived timing |

These numbers form the **initial baseline** for future `OrderBook` optimizations.

---

## 3. Bybit REST snapshot → OrderBook

Executable: `trading_bybit_orderbook_snapshot`

Usage:

```bash
./trading_bybit_orderbook_snapshot <SYMBOL> <depth> <runs>
# examples:
./trading_bybit_orderbook_snapshot BTCUSDT 50 5000
./trading_bybit_orderbook_snapshot ETHUSDT 100 2000
```

The program:

1. Fetches a full orderbook snapshot via Bybit public REST (limit N bids / N asks).
2. Builds an internal `OrderBook` from that snapshot.
3. Benchmarks repeated `OrderBook` rebuilds from the snapshot.

Sample results:

#### BTCUSDT, depth 50×50

* 2000 runs, 100 levels per snapshot (50 bids + 50 asks):

| Symbol  | Depth | Runs | Levels/snapshot | total time (ns) | ns/snapshot | ns/level |
| ------- | ----: | ---: | --------------: | --------------: | ----------: | -------: |
| BTCUSDT |    50 | 2000 |             100 |     149,040,698 |    29,808.1 |    298.1 |

#### ETHUSDT, depth 100×100

* 2000 runs, 200 levels per snapshot:

| Symbol  | Depth | Runs | Levels/snapshot | total time (ns) | ns/snapshot | ns/level |
| ------- | ----: | ---: | --------------: | --------------: | ----------: | -------: |
| ETHUSDT |   100 | 2000 |             200 |     176,863,140 |    88,431.6 |    442.2 |

---

## 4. Bybit WebSocket snapshot → OrderBook

Python helper:

```bash
python tools/bybit_ws_orderbook.py | ./trading_ws_orderbook_snapshot BTCUSDT 2000
```

The Python script subscribes to `orderbook.50.BTCUSDT`, prints JSON;
C++ binary reads a **snapshot** from stdin, extracts top-of-book levels and benchmarks repeated `OrderBook` builds from that snapshot.

Sample result:

| Symbol  | Depth | Runs | Levels/snapshot | ns/snapshot | ns/level |
| ------- | ----: | ---: | --------------: | ----------: | -------: |
| BTCUSDT |    50 | 2000 |             100 |    21,603.7 |    216.0 |

This shows that **pure C++ `OrderBook` build** is comfortably in the few hundred-ns-per-level range, independent of REST/WS overhead.

---

## 5. Live Bybit WebSocket orderbook (handler & data latency)

Executable: `trading_bybit_ws_orderbook_live`

Usage:

```bash
./trading_bybit_ws_orderbook_live BTCUSDT 1000
# max_messages = 1000, 0 = infinite
```

The program:

* connects to Bybit WS orderbook stream (`orderbook.50.SYMBOL`),
* processes one initial snapshot + N deltas,
* maintains an internal `OrderBook`,
* for each message:

  * measures **handler processing time** (how long we spend updating `OrderBook`),
  * measures **data latency** = `local_now_ms - msg.ts_ms`,
* prints percentiles at the end.

Sample result for `BTCUSDT`, `max_messages=1000`:

```text
=== Live WS orderbook stats ===
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
Done.
```

Interpretation:

* **Per-message handler time** ~ 100–200 µs — this is "our" CPU time spent updating the `OrderBook`.
* **Feed latency** ~ 90–115 ms — mostly network + Bybit infrastructure, not `OrderBook` performance.

---

## 6. CPU profiling (perf) — baseline findings

Command:

```bash
perf record ./trading_bench_order_book
perf report
```

Key hotspots (simplified summary):

* `std::map<Price, Level>` (price levels for bids/asks):

  * `std::__detail::_Map_base<...>` — ~28% CPU
* `std::unordered_map<OrderId, OrderRef>` (`index_` for fast cancel):

  * `std::_Hashtable<...>` — ~15–20% CPU
* Dynamic allocations:

  * `operator new`, `malloc`, `_int_malloc`, `cfree` — **~40%+** combined
* Core `OrderBook` methods:

  * `OrderBook::add_limit_order_with_id` — ~12%
  * `OrderBook::match_incoming_limit` — ~9%

High-level conclusion:

* Current `OrderBook` design (`std::map<Price, std::list<Order>>` + `std::unordered_map` index) is **allocation-heavy** and dominated by:

  * pointer-chasing in tree / hash structures,
  * frequent malloc/free.
* This is a perfect candidate for:

  * removing `std::list`,
  * replacing `unordered_map` index with array-based storage,
  * using more cache-friendly layouts.

These profiled numbers form the **starting point** for all future `OrderBook v2` experiments.

---

## 7. How to document future optimizations

For each optimization experiment, add a short subsection like this:

### 7.x Experiment: <short title>

* **Date:** YYYY-MM-DD

* **Goal:** (e.g. "reduce allocations in `OrderBook` by replacing `std::list` with array-based storage + ID queues")

* **Change summary:**

  * e.g. "introduced `OrderBook::reserve_orders()`, pre-reserved index_ in benchmarks"
  * e.g. "switched from per-order heap-allocated nodes to a flat `std::vector<OrderMeta>` + per-price queues of IDs"

* **Benchmarks compared:**

  * `trading_bench_order_book` before/after
  * `trading_bench_order_book_tsc` before/after
  * (optional) `trading_bybit_orderbook_snapshot` ns/level before/after
  * (optional) `trading_bybit_ws_orderbook_live` handler ns/message before/after

* **Results:**

  | Benchmark                 | Before (mean ns/op) | After (mean ns/op) |  Δ % |
  | ------------------------- | ------------------: | -----------------: | ---: |
  | `add_limit_order`         |                 xxx |                yyy | -zz% |
  | `execute_market_order`    |                 xxx |                yyy | -zz% |
  | `snapshot build ns/level` |                 xxx |                yyy | -zz% |

* **Conclusion:** 1–3 short sentences. Example:
  "This change reduced add_limit_order latency by ~25%, mostly by removing per-order heap allocations and avoiding unordered_map rehashing."

### 7.1 Experiment: OrderBook v2 – unified matching core & flat storage

* **Date:** 2025-12-02

* **Goal:** simplify and de-duplicate matching logic for bids/asks while keeping or improving overall performance, and reduce allocation pressure.

* **Change summary:**

  * Replaced per-level intrusive lists with a flat `std::vector<Order>` plus per-level `std::vector<OrderIndex>` indices.
  * Introduced a unified matching routine:

    ```cpp
    template <typename Book, typename PricePredicate>
    Quantity match_on_book(Book& book, Quantity qty, PricePredicate&& should_cross);
    ```

    which is used for both market orders and the taker leg of limit orders.
  * Kept the public `OrderBook` API stable (`add_limit_order(_with_id)`, `execute_market_order`, `cancel`, `best_bid/ask`).

* **Benchmarks compared:**

  * `trading_bench_order_book` (microbench, clock)
  * `trading_mt_bench` (2-thread pipeline)
  * `trading_bybit_orderbook_snapshot` (REST snapshot → `OrderBook`)
  * `trading_bybit_ws_orderbook_live` (live WS orderbook handler)

#### 7.1.1 Microbenchmarks: `trading_bench_order_book`

Config:

* `iterations = 200000`
* `runs       = 5`
* `batch_size = 128`
* `warmup     = 20000`

Command:

```bash
./trading_bench_order_book
```

Results (OrderBook v2):

| Benchmark                         | mean ns/op | p50 ns | p95 ns | p99 ns |
| --------------------------------- | ---------: | -----: | -----: | -----: |
| `empty_loop`                      |       0.44 |   0.41 |   0.45 |   0.53 |
| `OrderBook::add_limit_order`      |     242.74 | 199.24 | 350.24 | 467.71 |
| `OrderBook::execute_market_order` |      17.52 |  16.74 |  19.74 |  22.33 |

Compared to the baseline in section 2:

* `add_limit_order` improved by roughly **27%** in mean ns/op.
* `execute_market_order` became more expensive in isolation but still stays well below 20 ns/op.
* Harness overhead remains in the same sub-ns range.

#### 7.1.2 Multithreaded pipeline: `trading_mt_bench`

Command:

```bash
./trading_mt_bench 200000 42
```

Summary (two-thread pipeline, producer + engine):

* **Events:** 200000
* **Throughput:** 2.63e6 events/s
* **Mean:** 380.06 ns/event
* **Latency (enqueue → processed):**

  * p50: 1,437,543 ns
  * p95: 2,173,710 ns
  * p99: 2,285,251 ns

Compared to the earlier `mt_bench v2` run in section 7.2, the mean cost per event stays around 380 ns and end-to-end latency remains in the same ballpark.

#### 7.1.3 REST snapshot → `OrderBook`

Command:

```bash
./trading_bybit_orderbook_snapshot BTCUSDT 50 5000
```

Config:

* Symbol: `BTCUSDT`
* Depth: 50 bids + 50 asks (100 levels per snapshot)
* Runs: 5000

Results (OrderBook v2):

| Symbol  | Depth | Runs | Levels/snapshot | total time (ns) | ns/snapshot | ns/level |
| ------- | ----: | ---: | --------------: | --------------: | ----------: | -------: |
| BTCUSDT |    50 | 5000 |             100 |     265,399,664 |    53,079.9 |    530.8 |

Compared to the earlier snapshot benchmark in section 3, the normalized `ns/level` increased from ~298 ns to ~531 ns (~+78%). The main reason is that snapshot builds currently use the full `add_limit_order_with_id` + matching path, even though no crossing is possible.

A future optimization is to introduce a dedicated `build_from_snapshot(...)` path that inserts orders directly into per-price levels without matching.

#### 7.1.4 Live WS orderbook handler: `trading_bybit_ws_orderbook_live`

Command:

```bash
./trading_bybit_ws_orderbook_live BTCUSDT 1000
```

Results (BTCUSDT, depth=50, max_messages=1000):

* Handler time:

  * mean: **133,626 ns**
  * p50 : **128,725 ns**
  * p95 : **188,062 ns**
  * p99 : **232,049 ns**

* Data latency (`local_now_ms - msg.ts_ms`):

  * mean: **92.47 ms**
  * p50 : **91 ms**
  * p95 : **93 ms**
  * p99 : **149 ms**

In practice, after the OrderBook v2 refactor the handler stays in the same 100–200 µs range per message, and data latency continues to be dominated by Bybit + network timing rather than the local matching logic.

---

## 8. Summary

This file is the **single source of truth** for performance:

* Baseline numbers for `OrderBook`.
* Behaviour under real Bybit REST / WS load.
* Profiler findings (where the CPU actually goes).
* A chronological log of optimizations and their effect.

Whenever we change the internals of `OrderBook` or exchange integration, we:

1. Run the same set of benchmarks.
2. Update the tables above.
3. Add a new "Experiment" subsection with a short analysis.

Over time, this turns `cpp-trading-core` into a **documented performance story**, not just "some code that seems fast".
