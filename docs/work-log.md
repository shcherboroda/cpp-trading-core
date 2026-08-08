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

## Open Issues

- WSL exposes virtual CPU topology only; P-core/E-core mapping remains unverified and must not be inferred from vCPU numbers.
- WSL2 scheduler/background variability is material; retain all seven runs and report ranges for every comparison.
- Historical results before explicit per-thread pinning used a process CPU mask only; do not treat them as placement-controlled comparisons.

## Next Step

The L2 depth and snapshot-tail questions are baselined. Next, isolate the
order-ID-aware matching `OrderBook` operation mix without the producer/
consumer queue.
