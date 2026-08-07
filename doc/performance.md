# Performance & Benchmarking

This document tracks how the performance of `cpp-trading-core` evolves over time:

* what exactly is measured,
* on which hardware / build configuration,
* which tools are used,
* what each optimization actually improves.

## Local WSL2 baseline — 2026-08-05

This is the current comparison baseline for the two-thread SPSC pipeline. It
uses the repository state recorded by the binary and source hashes in the raw
output; it is not a claim about native hardware limits or production trading
performance.

**Build and validation**

* WSL2 kernel: `6.18.33.2-microsoft-standard-WSL2`; compiler: GCC 15.2.0.
* CMake 4.2.3, `Release`, `-O3 -DNDEBUG`.
* `./scripts/build_release.sh`: 18/18 tests passed.
* The WSL process was pinned with `taskset -c 0,2`. WSL reports those as
  distinct virtual cores, but does not establish their P-core/E-core identity.
  No core-type claim is made.

**Method**

Seven separate runs used a fixed seed (42), 2,000,000 events, a 4,096-entry
queue, and a 20,000-event warm-up. `pre-push` timestamps immediately before
the first enqueue attempt and therefore includes waiting for a full queue.
`off` disables per-event timestamps and sample writes; it is a measurement
overhead control, not an implementation variant. The collector recorded the
command, CPU topology, load average, compiler/build settings, git state, and
binary/source hashes.

> Retrospective methodology correction: these initial runs used `taskset` to
> constrain the process CPU mask only. They did not prove separate producer and
> consumer placement, so they are retained as exploratory historical data.
> Explicit per-thread pinning was added for the placement experiment below.

| Mode | Throughput median (M events/s) | Throughput range | p50 latency | p95 latency | p99 latency |
| --- | ---: | ---: | ---: | ---: | ---: |
| `pre-push` | 3.482 | 3.018–4.137 | 1.033 ms | 1.900 ms | 3.309 ms |
| `off` | 4.736 | 3.825–5.150 | n/a | n/a | n/a |

Raw CSV is retained in the versioned result archive under
`results/mt-baseline-20260805T100655Z/` (`pre-push`) and
`results/mt-baseline-20260805T100700Z/` (`off`). The throughput range shows
material host/scheduler variability under WSL2. Future queue variants must use
the same `pre-push` command, affinity, event stream, and seven-run procedure;
`off` should remain a separate control for timestamp/sample overhead.

## Controlled experiment: SPSC index cache-line separation — 2026-08-07

**Question:** does putting the producer and consumer indices on separate
64-byte-aligned cache lines improve the existing two-thread pipeline?

**Variant:** `SPSC_QUEUE_PAD_INDICES=ON`; the baseline is the identical source
with `SPSC_QUEUE_PAD_INDICES=OFF`. This changes only the `head_`/`tail_`
layout; capacity, atomics, event stream, compiler, affinity, and command are
unchanged. Both configurations built successfully and passed all 18 tests.

To reduce run-order bias, measurements were collected in two seven-run blocks
per configuration: unpadded → padded, then padded → unpadded. The table uses
all 14 runs per configuration. Full ranges overlap materially under WSL2.

| Mode | Variant | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `pre-push` | unpadded | 3.475 | 2.325–4.442 | 0.993 ms | 1.945 ms | 2.874 ms |
| `pre-push` | padded | 3.996 | 2.707–4.718 | 0.911 ms | 1.812 ms | 2.884 ms |
| `off` control | unpadded | 3.418 | 2.680–4.154 | n/a | n/a | n/a |
| `off` control | padded | 4.696 | 2.723–5.533 | n/a | n/a | n/a |

The balanced median points toward higher throughput and lower p50/p95 with
padding, but p99 did not improve and ranges overlap. The result is therefore
**inconclusive for the stated tail-latency question**. Padding remains a
reproducible experiment behind a CMake option, disabled by default; it is not
presented as an accepted optimization.

Raw data is retained in the result archive. The paired blocks are
`mt-spsc-unpadded-paired-20260807T144646Z/` and
`mt-spsc-padded-paired-20260807T144652Z/`; the reverse-order blocks are
`mt-spsc-padded-reverse-20260807T144731Z/` and
`mt-spsc-unpadded-reverse-20260807T144735Z/`, with corresponding `-off`
directories.

## Controlled experiment: `yield` versus x86 `pause` backoff — 2026-08-07

**Question:** does avoiding scheduler yield while the SPSC queue is empty or
full reduce end-to-end tail latency on this WSL2 laptop?

**Variant:** `--backoff=pause` executes `_mm_pause()` in the retry loops;
baseline `--backoff=yield` retains `std::this_thread::yield()`. All other
benchmark inputs are identical. `pause` deliberately occupies the pinned CPU
while waiting, so this experiment does not evaluate power use or system-wide
fairness.

The runs were balanced as yield → pause → pause → yield, with seven runs in
each block (14 per mode). Browser, office, Telegram, and other local activity
may interfere with WSL scheduling; load averages and timestamps are preserved
in every environment record.

| Backoff | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `yield` | 4.362 | 3.472–5.727 | 0.876 ms | 1.546 ms | 2.406 ms |
| `pause` | 4.611 | 3.601–5.313 | 0.838 ms | 1.479 ms | 2.249 ms |

`pause` has directionally better medians (throughput +5.7%; p99 −6.5%), but
the run ranges overlap and the effect was not stable in every seven-run block.
It is therefore a **promising but unaccepted** latency variant: default remains
`yield`, and no production or universal-performance claim is made.

Raw data: `mt-backoff-yield-paired-20260807T145927Z/`,
`mt-backoff-pause-paired-20260807T145931Z/`,
`mt-backoff-pause-reverse-20260807T145935Z/`, and
`mt-backoff-yield-reverse-20260807T145940Z/`.

### Quiet-session replication

The same experiment was repeated after the browser, office suite, and Telegram
were closed and no other local tasks were intentionally started. The starting
load average was 0.07/0.12/0.25. This does not make WSL2 a dedicated benchmark
host, but it reduces known interactive interference.

| Backoff | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `yield` | 5.614 | 4.824–7.040 | 0.676 ms | 1.132 ms | 1.737 ms |
| `pause` | 5.369 | 4.672–6.717 | 0.709 ms | 1.159 ms | 1.718 ms |

The quieter repeat reverses the earlier throughput, p50, and p95 direction;
only p99 is marginally lower for `pause`. This fails to establish a stable
benefit. `pause` remains unaccepted, and `yield` remains the default.

Raw data: `mt-backoff-yield-quiet-paired-20260807T150935Z/`,
`mt-backoff-pause-quiet-paired-20260807T150939Z/`,
`mt-backoff-pause-quiet-reverse-20260807T150942Z/`, and
`mt-backoff-yield-quiet-reverse-20260807T150946Z/`.

## Controlled experiment: explicit producer/consumer placement — 2026-08-07

**Question:** does placing the producer and consumer on sibling WSL vCPUs or
on distinct WSL virtual cores affect the SPSC pipeline?

The benchmark now pins each worker with Linux thread affinity, fails if that
setup fails, and records requested/start/end CPU in every CSV row. Every one
of the 28 runs observed the requested CPU at both sample points. This is a
placement experiment, not a P-core/E-core experiment: WSL topology cannot
establish core type.

The order was balanced as distinct (`0,2`) → sibling (`0,1`) → sibling →
distinct, with seven runs per block and `--backoff=yield`.

| Placement | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| distinct virtual cores (`0,2`) | 4.952 | 3.565–6.373 | 0.777 ms | 1.267 ms | 1.963 ms |
| sibling vCPUs (`0,1`) | 4.269 | 3.836–5.323 | 0.875 ms | 1.462 ms | 2.194 ms |

The balanced medians consistently favour distinct virtual cores: +16.0%
throughput and lower p50/p95/p99. Individual ranges still overlap, and the
second distinct block was slower, so this is evidence of placement sensitivity
rather than a universal hardware claim. Future variant comparisons use explicit
`producer=0, consumer=2` placement as the new baseline.

Raw data: `mt-affinity-distinct-paired-20260807T151812Z/`,
`mt-affinity-sibling-paired-20260807T151816Z/`,
`mt-affinity-sibling-reverse-20260807T151820Z/`, and
`mt-affinity-distinct-reverse-20260807T151824Z/`.

### Replication: cache-line separation with explicit placement

The earlier cache-line-padding result predated per-thread placement control, so
it was repeated with `producer=0`, `consumer=2`, and `--backoff=yield`. The
order was balanced as unpadded → padded → padded → unpadded, with seven runs
per block and 14 per configuration. All runs confirmed the requested placement.

| Layout | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| unpadded | 5.549 | 4.291–6.535 | 0.772 ms | 1.351 ms | 1.968 ms |
| padded | 5.328 | 3.473–6.055 | 0.689 ms | 1.288 ms | 2.120 ms |

Padding improves p50/p95 medians but lowers throughput and worsens p99; the
two execution orders also disagree. It is therefore **not an accepted
optimization**. The default remains `SPSC_QUEUE_PAD_INDICES=OFF`.

Raw data: `mt-padding-unpadded-affinity-paired-20260807T152414Z/`,
`mt-padding-padded-affinity-paired-20260807T152417Z/`,
`mt-padding-padded-affinity-reverse-20260807T152421Z/`, and
`mt-padding-unpadded-affinity-reverse-20260807T152425Z/`.

## Controlled experiment: `TimedEvent` copy versus move transfer — 2026-08-07

**Question:** does moving `TimedEvent` values into and out of the SPSC queue
improve the explicitly pinned pipeline?

**Variant:** `SPSC_QUEUE_MOVE_TRANSFER=ON` moves the producer value into the
queue and the queue value into the consumer output. The baseline retains copy
transfer. `TimedEvent` and its contained `Event` are scalar-only, so this is a
targeted check rather than an assumed optimization. Both Release builds passed
18/18 tests. Each variant received 14 runs in ABBA order, with
`producer=0`, `consumer=2`, 2,000,000 measured events, `pre-push` latency, and
`yield` backoff.

| Transfer | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | --- | ---: | ---: | ---: |
| copy | 6.547 | 5.622–7.316 | 0.555 ms | 0.878 ms | 1.447 ms |
| move | 6.450 | 5.219–7.252 | 0.583 ms | 0.856 ms | 1.411 ms |

Move is not accepted: throughput and p50 are worse, while the small p95/p99
differences are inside overlapping run ranges. The default stays copy transfer.

Raw data: copy `mt-baseline-20260807T152956Z/` and
`mt-baseline-20260807T153005Z/`; move `mt-baseline-20260807T152959Z/` and
`mt-baseline-20260807T153002Z/`.

## Controlled experiment: `OrderBook` pre-reservation — 2026-08-07

**Question:** does reserving order-book storage before measurement reduce
allocation-related stalls in the pipeline?

**Variant:** `--book-reserve=1300000` is applied before the synchronized start;
the baseline is `--book-reserve=0`. The reserve covers the maximum expected
add-order count for the fixed 2,000,000-event input with margin. Release tests
passed 18/18. Both variants received 14 runs in ABBA order with
`producer=0`, `consumer=2`, `pre-push` latency, and `yield` backoff.

| Book reserve | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | --- | ---: | ---: | ---: |
| 0 | 3.759 | 3.041–4.546 | 0.982 ms | 1.548 ms | 2.271 ms |
| 1,300,000 | 3.971 | 3.020–4.934 | 0.917 ms | 1.426 ms | 1.935 ms |

The direction favours reservation, especially at p99, but all observed ranges
overlap. This is not accepted as an optimization without replication; it is
retained as evidence that allocation growth is worth isolating further.

Raw data: `mt-reserve-none-paired-20260807T153226Z/`,
`mt-reserve-1300000-paired-20260807T153231Z/`,
`mt-reserve-1300000-reverse-20260807T153236Z/`, and
`mt-reserve-none-reverse-20260807T153241Z/`.

## Controlled experiment: small SPSC capacity — 2026-08-07

**Question:** does a 256-slot SPSC queue reduce tail latency relative to the
4096-slot default, and what throughput does it cost?

**Variant:** runtime `--queue-capacity=256` versus `4096`; no other source or
build setting changes. Release tests passed 18/18. Both variants received 14
ABBA-ordered runs with `producer=0`, `consumer=2`, no book pre-reservation,
`pre-push` latency, and `yield` backoff.

| Queue capacity | Throughput median (M events/s) | Throughput range | p50 | p95 | p99 |
| --- | ---: | --- | ---: | ---: | ---: |
| 4096 | 3.901 | 2.217–5.049 | 0.925 ms | 1.732 ms | 2.520 ms |
| 256 | 3.575 | 2.751–4.464 | 0.058 ms | 0.113 ms | 0.193 ms |

The 256-slot setting has a strong latency advantage, including non-overlapping
p99 ranges, but its median throughput is 8.3% lower. This is a measured
latency/throughput trade-off, not a universal replacement; the default remains
4096 pending a workload-level priority decision.

Raw data: `mt-capacity-4096-paired-20260807T161049Z/`,
`mt-capacity-256-paired-20260807T161053Z/`,
`mt-capacity-256-reverse-20260807T161058Z/`, and
`mt-capacity-4096-reverse-20260807T161104Z/`.

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

### 7.2 Experiment: MarketDataOrderBook – dedicated market-data layer & WS handler

**Date:** 2025-12-08

**Goal:** Separate the market-data representation from the matching engine by introducing `MarketDataOrderBook` (price-level book in integer ticks) and using it in both REST snapshot and live WS orderbook paths. Measure:

* the raw cost of `MarketDataOrderBook::apply_snapshot`,
* the cost of building the book from a real Bybit REST snapshot,
* the handler time of the live WS orderbook feed after refactoring to `MarketDataOrderBook`.

#### 7.2.1 Microbenchmark: `trading_bench_market_data_book`

Executable: `trading_bench_market_data_book`

Usage:

```bash
./trading_bench_market_data_book
```

Config:

* `runs`  = 5000
* `levels` per snapshot = 100 (50 bids + 50 asks)
* synthetic snapshot: evenly spaced price levels, qty = 1

Results:

| Snapshot source      | runs | levels/snapshot | ns/snapshot | ns/level |
| -------------------- | ---: | --------------: | ----------: | -------: |
| synthetic (internal) | 5000 |             100 |    10094.10 |  100.941 |

Interpretation:

* `MarketDataOrderBook::apply_snapshot` builds a full 100-level book in ~10 µs.
* Normalized cost is ~100 ns per price level (~10M levels/s on a single core), which is more than sufficient for typical spot depth (50–100 levels).

#### 7.2.2 Bybit REST snapshot → `MarketDataOrderBook`

Executable: `trading_bybit_orderbook_snapshot`

Usage (unchanged):

```bash
./trading_bybit_orderbook_snapshot BTCUSDT 50 5000
```

Config:

* Symbol: `BTCUSDT`
* Depth: 50 bids + 50 asks (100 levels per snapshot)
* Runs: 5000
* Implementation: REST JSON → `OrderBookSnapshot` with `PriceLevel` in ticks → `MarketDataOrderBook::apply_snapshot(bids, asks)`

Results (BTCUSDT, depth=50, runs=5000):

| Symbol  | Depth | Runs | Levels/snapshot | total time (ns) | ns/snapshot | ns/level |
| ------- | ----: | ---: | --------------: | --------------: | ----------: | -------: |
| BTCUSDT |    50 | 5000 |             100 |      65,873,993 |    13,174.8 |  131.748 |

Notes:

* Compared to the earlier snapshot benchmark that built the book via `OrderBook::add_limit_order_with_id` + matching, the normalized cost per level dropped from ~530 ns to ~130 ns (~4× faster).
* The remaining gap between synthetic (~101 ns/level) and real snapshot (~132 ns/level) is small and expected, given the different data distribution and extra work around JSON parsing.

#### 7.2.3 Live WS orderbook handler: `trading_bybit_ws_orderbook_live`

Executable: `trading_bybit_ws_orderbook_live`

Usage (WS bench):

```bash
./trading_bybit_ws_orderbook_live BTCUSDT 1000
# or via script:
./scripts/run_ws_live_bench.sh BTCUSDT 1000
```

Config:

* Symbol: `BTCUSDT`
* Depth: 50 (top-50 bids + top-50 asks in feed subscription)
* `max_messages` = 1000 (1 snapshot + 999 deltas, 999 processed total)
* Implementation:

  * first snapshot → `MarketDataOrderBook::apply_snapshot(...)`,
  * subsequent messages → `MarketDataOrderBook::apply_delta(...)`,
  * per-message handler time and data latency are recorded.

Results (BTCUSDT, depth=50, max_messages=1000):

**Handler time (per WS message):**

| Metric | Value (ns) |
| ------ | ---------: |
| mean   |   33,476.8 |
| p50    |     28,772 |
| p95    |     61,162 |
| p99    |     89,552 |

**Data latency (local_now_ms − msg.ts_ms):**

| Metric | Value (ms) |
| ------ | ---------: |
| mean   |      91.37 |
| p50    |         88 |
| p95    |         90 |
| p99    |        217 |

Comparison vs pre-`MarketDataOrderBook` WS handler:

* Mean handler time improved from ~133k ns to ~33k ns (≈4× faster).
* High percentiles (p95/p99) also shrank correspondingly, while data latency remains dominated by network / exchange side (80–200 ms), not by local processing.

Overall, `MarketDataOrderBook` provides a cheap and clean market-data layer, and refactoring the WS handler to use it significantly reduced per-message processing cost without changing the external API.

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
