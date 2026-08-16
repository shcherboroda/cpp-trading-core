# Work Log

## Current Objective

Measure small, single-factor changes in the local WSL2 market-data and matching paths.

## Confirmed Environment

- Host: Windows 11
- Development: WSL2
- CPU: Intel Core i7-1255U with P-cores and E-cores
- Repository: `cpp-trading-core`
- Remote: `https://github.com/shcherboroda/cpp-trading-core`

## Verified Baseline

- commit: `a145ee5b1d2a4657cab849a03568ba7066f6efb0` plus the uncommitted benchmark-methodology changes recorded in collector hashes.
- compiler: GCC 15.2.0; CMake 4.2.3.
- build type: `Release`, `-O3 -DNDEBUG`.
- build/test command: `./scripts/build_release.sh`.
- test result: 18/18 passed on 2026-08-05.
- benchmark command: `CPUS=0,2 PRODUCER_CPU=0 CONSUMER_CPU=2 LATENCY_MODE=pre-push ./scripts/collect_mt_baseline.sh 7 2000000 42`.
- CPU affinity: producer pinned to Linux vCPU `0`, consumer to `2`; requested/start/end CPU recorded in CSV.
- core type: unknown; WSL virtual topology cannot establish P-core/E-core identity.
- benchmark result: explicit-placement baseline (`0,2`) median 4.952 M events/s; p50/p95/p99 0.777/1.267/1.963 ms. Raw CSV is in `results/mt-affinity-distinct-*-20260807T1518*/`.

## Decisions

- Reuse the existing repository.
- Prefer one focused experiment over new features.
- Treat WSL2 limitations explicitly.
- Do not imply commercial trading or HFT experience.

## Changes Made

- Added a synchronized benchmark start, explicit latency semantics, CSV output, and a fixed-affinity multi-run collector for `trading_mt_bench`.
- Added a reproducible `SPSC_QUEUE_PAD_INDICES` CMake experiment switch. It is disabled by default because the controlled result is inconclusive for p99 latency.
- Retained raw benchmark evidence in `results/` and documented its status in `results/README.md`.
- Added `--backoff=yield|pause` to the pipeline benchmark and collector. Default remains `yield`.
- Added explicit per-thread CPU pinning, fail-fast affinity validation, and requested/start/end CPU fields in the benchmark CSV.
- Added a compile-time copy-versus-move transfer variant for `TimedEvent` in the SPSC benchmark; default remains copy transfer.
- Reworked the fixed L2 snapshot microbenchmark into a CSV-capable snapshot/delta benchmark and added a fixed-CPU multi-run collector.
- Added L2 snapshot/delta correctness tests and a flat-vector L2 representation for explicit snapshot-versus-delta trade-off experiments.
- Enabled ordered insertion hints by default for `MarketDataOrderBook` snapshots.

## Results

- Seven-run `pre-push` baseline and seven-run latency-off control were collected after a successful Release build/test.
- Latency sampling reduced median throughput from 4.736 to 3.482 M events/s in this setup; treat this as benchmark instrumentation cost, not as an algorithm comparison.
- In balanced 14-run-per-variant cache-line-padding measurement, padded median throughput was 3.996 vs 3.475 M events/s and p50/p95 improved, but p99 was unchanged within noise. Do not claim an optimization; see `doc/performance.md`.
- Active-desktop and quiet 14-run-per-variant backoff measurements disagree on throughput/p50/p95; `pause` has no established benefit. `yield` remains the default.
- With explicit pinning, distinct WSL virtual cores (`0,2`) had better balanced medians than sibling vCPUs (`0,1`); retain `0,2` for future comparisons.
- Explicit-placement cache-line-padding replication improved p50/p95 but lowered throughput and worsened p99; leave padding disabled.
- In a balanced 14-run copy-versus-move comparison under explicit pinning, move transfer lowered median throughput (6.450 vs 6.547 M events/s) and worsened p50. It is not accepted; raw data is retained in `results/`.
- A separate 14-run replication accepts `OrderBook` reserve=1,300,000 for this fixed workload: 6.952 vs 5.145 M events/s and p99 1.158 vs 2.064 ms, with non-overlapping p99 ranges. It remains opt-in because real input bounds may be unknown.
- A five-point 14-run-per-value reserve sweep confirms a monotonic improvement from 0 through 1,300,000. For this fixed workload, 1,300,000 is the best tested setting: 7.607 M events/s and p99 1.089 ms versus 3.908 M and 2.648 ms at the default.
- In a balanced 14-run SPSC capacity comparison, 256 slots reduced p99 from 2.520 to 0.193 ms, with 8.3% lower median throughput. Keep 4096 as the default because the priority is workload-dependent.
- In a balanced 14-run capacity comparison, 16,384 slots lowered throughput and worsened p99 (7.795 vs 2.088 ms); reject the larger queue for this workload.
- A separate 14-run index-wrap replication did not reproduce the mask direction; mask was slightly worse. Retain the conditional branch and reject the mask variant.
- In a 252-run L2 depth experiment, snapshot rebuild p50 rose from 0.418 µs at 10 levels/side to 404.864 µs at 5,000; mixed insert/update/delete delta p99 rose from 87 to 566 ns. Fixed-size overwrites remained near 120 ns p99 at 5,000 levels/side.
- In five L2-tail optimization experiments at 5,000 levels/side, map insertion hints improved snapshot p99 from 1.192 to 0.900 ms and are now default. Sorted map reconciliation reached 0.162 ms p99 and flat vectors 0.036 ms, but flat vectors regressed mixed-delta p99 to 6.827 µs versus 0.549 µs for the map; neither is default.
- In 15 interleaved fixed-CPU runs of a Bybit-shaped full snapshot handler at 1,000 levels/side, direct fixed-point conversion cut median conversion time from 187.981 to 40.147 µs and full measured handler work from 771.614 to 614.228 µs. At the subscribed 50 levels/side, total median fell from 54.923 to 42.534 µs. Raw evidence is in `results/bybit-l2-handler-{50,1000}-20260808T103*`.
- A second 15-run-per-variant Bybit handler experiment found that a bounded SAX prototype, which materializes only `b`/`a` levels, lowers full snapshot p50/p99 from 738.905/1,779.071 to 401.217/1,057.350 µs at 1,000 levels/side. Copying the input frame had no measurable cost in this setup. The prototype is not yet in the live path: it must first gain topic/type/timestamp and delta/resnapshot tests.
- The SAX decoder is now used by the live L2 application; DOM decoding remains available for trade messages. It has targeted snapshot/delta coverage and a real five-frame Bybit smoke run (one snapshot, three deltas). Full Release tests: 25/25.
- A post-adoption 15-run replication against the same DOM+fixed-point baseline confirms SAX: at 50 levels/side total p50/p99 is 20.132/37.516 versus 40.982/80.962 µs; at 1,000 it is 409.283/1,236.610 versus 768.482/1,909.079 µs. Raw CSV: `results/bybit-l2-handler-{50,1000}-20260808T110*`.
- Current delta baseline at 1,000 levels/side, 15 fixed-CPU runs: a 16-update overwrite batch is 86/89 ns p50/p99; mixed update/delete/insert is 116/141 ns. `apply_delta` is not the next bottleneck relative to JSON decode.
- Small-payload decode sweep (15 runs, DOM baseline): SAX full-path p50/p99 at 2, 8 and 16 levels/message is 0.873/1.442, 2.061/3.328 and 2.895/4.995 µs, versus DOM 1.658/3.069, 4.285/7.776 and 6.477/12.246 µs. Decode, not `apply_delta`, dominates delta handling.
- Live handler timing now starts before SAX decoding. In a 100-frame Bybit smoke run, 98 deltas had 2/4/5 levels at p50/p95/p99; the matching full-handler time was 14.038/31.909/52.760 µs. The earlier 2/8/16-level synthetic sweep covers the observed median and tail batch sizes.
- Decoder no longer copies the Bybit topic string; it compares it directly while SAX parsing. A one-run live diagnostic lowered decode p99 from 52.968 to 31.596 µs despite a larger observed p99 batch (21 versus 15 levels), but this is preliminary because the two real-time message mixes differ. Accept/reject only after replaying a fixed frame corpus.
- Captured a 1,000-frame public Bybit BTCUSDT orderbook corpus (one snapshot, 998 deltas) for fixed-input replay. Capture I/O is excluded from latency evidence.
- Two 15-run interleaved fixed-corpus replays do not establish a measurable benefit from avoiding the small `topic` copy. The first slightly favored copying and the replication slightly favored direct comparison; retain direct comparison for its simpler data flow, but make no latency claim for it. Raw CSV: `results/bybit-l2-replay-{20260808T125821Z,20260810T082857Z}/raw.csv`.
- An intrusive 15-run replay diagnostic on the same corpus records 0.408/0.411 microseconds p50 inside the `b`/`a` array regions for direct/copy topic variants, against 1.970/2.012 microseconds full decode p50. Level parsing is therefore about one fifth of median decoder time; JSON SAX traversal and metadata are the next candidate, not `MarketDataOrderBook::apply_delta`. Raw CSV: `results/bybit-l2-replay-20260810T083114Z/raw.csv`.
- A fixed-corpus scanner lower-bound experiment exposed and corrected an off-by-one cursor bug in the prototype; a targeted tick-value test now covers it. After the fix, the scanner processes all 1,000 captured frames and reaches 0.940/3.977 microseconds p50/p99 total handler time versus SAX 2.378/8.444 microseconds in the same 15-run active-desktop series. It is not accepted: it does not yet validate `topic`, `type`, `ts`, or `cts`, and accepts the subscription acknowledgement as an empty delta. Raw CSV: `results/bybit-l2-replay-20260810T090246Z/raw.csv`.
- An experimental bounded decoder adds the missing orderbook-envelope validation without changing the live application. It handles all 999 valid orderbook frames in the fixed corpus and, in 15 interleaved active-desktop runs, has total p50/p99 1.315/5.735 microseconds versus SAX 2.689/10.364 microseconds. The result is preliminary until corpus-equivalence validation and quiet-session replication; raw CSV: `results/bybit-l2-replay-20260810T090533Z/raw.csv`.
- A frame-by-frame verifier now compares SAX and bounded decoding over the full 1,000-frame corpus. It passed: parse success, `topic/type/ts/cts`, bid levels and ask levels match for every frame. This proves equivalence for the captured input, not for arbitrary JSON or future protocol changes.
- Quiet-session ABBA replication accepts the bounded decoder for the current Bybit L2 application: 30 runs per variant, all with 19,980 valid replayed frames, give median total p50/p99 0.800/3.250 microseconds for bounded versus 1.738/5.684 for SAX (54%/43% lower). The p99 ranges slightly overlap, so report the median improvement and ranges rather than claiming completely separated tails. Raw CSV: `results/bybit-l2-replay-{20260810T125802Z,20260810T125811Z}/raw.csv`.
- The live Bybit L2 application now uses the accepted bounded decoder. A 20-WebSocket-message public smoke run processed one snapshot and 18 deltas without decode errors. It is a correctness check only; its network-influenced handler numbers are not part of the CPU comparison.
- Bybit recommends a 20-second ping for connection maintenance, but it is intentionally not added to the current synchronous WebSocket client. Boost.Beast documents a shared WebSocket stream as not thread-safe; a safe concurrent read/ping design requires asynchronous operations serialized by an Asio strand. Treat that as a separate reliability refactor, not a small latency change.
- Added a live WebSocket depth collector and buffered-read diagnostics. In three 30-message public runs per depth, depth 50 had 1–2 buffered reads and 5.5–6.0 microseconds handler p50, so there is no evidence for reader/engine decoupling in the current single-symbol workload. Depth 1 is snapshot-only best quote; depth 1,000 has 10–15 buffered reads and a heavier initial snapshot, so it is a data-depth trade-off rather than a latency optimization. Raw logs: `results/bybit-ws-depth-20260814T081058Z/`.
- Renamed `local_now_ms - msg.ts_ms` output from data latency to a clock-offset sample after public runs showed negative values. Without synchronized clocks it must not be presented as one-way network latency.
- A pinned ABBA depth-50 WebSocket probe found no stable callback improvement from `TCP_NODELAY`; the temporary option was removed. This matches its expected outbound-write scope rather than a claim about inbound exchange delivery. Raw logs: `results/bybit-ws-depth-{20260814T083337Z,20260814T083343Z,20260814T083348Z,20260814T083354Z}/`.
- Bybit accepted `permessage-deflate` with no context takeover. In a pinned depth-1,000 off/on/on/off probe, callback p50 varied with the live message mix and did not demonstrate a stable local CPU improvement. Beast exposes the inflated payload, so wire-byte savings were not measured. Compression remains disabled; raw logs: `results/bybit-ws-depth-{20260814T083705Z,20260814T083716Z,20260814T083728Z,20260814T083740Z}/`.
- The reader/engine SPSC split remains deferred: pinned depth-50 live runs have only short buffered streaks, while the existing SPSC benchmark shows workload-dependent queue-tail trade-offs. Its admission criterion is sustained backlog in a realistic multi-symbol or high-depth capture.
- A strictly validating one-pass variant of the bounded Bybit L2 decoder reproduced SAX and bounded parsing on both the 1,000-frame historical corpus and a fresh 200-frame capture. Its 30-run hot replay improved median total p50 modestly (0.955 to 0.795 microseconds), but a 20-ms paced replay did not establish a p50 improvement (9.912 versus 10.400 microseconds, three runs per variant). It is retained only as `BYBIT_L2_DECODER=one-pass`; the validated bounded decoder remains default. Tests now reject missing separators and root-level `b`/`a` fields outside `data`.
- The depth-1,000 investigation rejected replacing the map-backed book with the existing flat book: snapshot p50 improved from about 58 to 3 microseconds, but normal delta update and mixed update/delete p50 degraded from 86/117 to 135/320 ns. A narrower sorted-snapshot reconciliation keeps the map delta path and reduces the median of per-run repeated real-capture snapshot-apply p50 values from 144.9 to 19.6 microseconds (7-run-per-variant ABBA). It is enabled by default, falls back to clear-and-build for unsorted or duplicate levels, and has targeted semantic tests. Raw CSV and reproduction command: `results/bybit-l2-resnapshot-20260816T121024431339597Z/`.

## Open Issues

- WSL exposes virtual CPU topology only; P-core/E-core mapping remains unverified and must not be inferred from vCPU numbers.
- WSL2 scheduler/background variability is material; retain all seven runs and report ranges for every comparison.
- Historical results before explicit per-thread pinning used a process CPU mask only; do not treat them as placement-controlled comparisons.

## Next Step

Keep the bounded decoder and network-path notes current. The next substantial
network task is a correctly serialized async/strand client that adds Bybit's
heartbeat and can collect backlog under a realistic multi-symbol load; it is a
reliability/architecture change, not a small latency patch.
