# Work Log

## Current Objective

Measure small, single-factor changes in the explicitly pinned local WSL2 pipeline.

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

## Results

- Seven-run `pre-push` baseline and seven-run latency-off control were collected after a successful Release build/test.
- Latency sampling reduced median throughput from 4.736 to 3.482 M events/s in this setup; treat this as benchmark instrumentation cost, not as an algorithm comparison.
- In balanced 14-run-per-variant cache-line-padding measurement, padded median throughput was 3.996 vs 3.475 M events/s and p50/p95 improved, but p99 was unchanged within noise. Do not claim an optimization; see `doc/performance.md`.
- Active-desktop and quiet 14-run-per-variant backoff measurements disagree on throughput/p50/p95; `pause` has no established benefit. `yield` remains the default.
- With explicit pinning, distinct WSL virtual cores (`0,2`) had better balanced medians than sibling vCPUs (`0,1`); retain `0,2` for future comparisons.
- Explicit-placement cache-line-padding replication improved p50/p95 but lowered throughput and worsened p99; leave padding disabled.
- In a balanced 14-run copy-versus-move comparison under explicit pinning, move transfer lowered median throughput (6.450 vs 6.547 M events/s) and worsened p50. It is not accepted; raw data is retained in `results/`.
- In a balanced 14-run `OrderBook` reserve comparison, reserving 1,300,000 entries directionally improved all medians (p99 1.935 vs 2.271 ms), but ranges overlap. Retain it as an unaccepted lead, not a claim.
- In a balanced 14-run SPSC capacity comparison, 256 slots reduced p99 from 2.520 to 0.193 ms, with 8.3% lower median throughput. Keep 4096 as the default because the priority is workload-dependent.
- In a balanced 14-run capacity comparison, 16,384 slots lowered throughput and worsened p99 (7.795 vs 2.088 ms); reject the larger queue for this workload.
- In a balanced 14-run index-wrap comparison, a power-of-two mask directionally improved throughput (5.039 vs 4.420 M events/s) and p50/p95, but all ranges overlap. Keep the branch default pending replication.

## Open Issues

- WSL exposes virtual CPU topology only; P-core/E-core mapping remains unverified and must not be inferred from vCPU numbers.
- WSL2 scheduler/background variability is material; retain all seven runs and report ranges for every comparison.
- Historical results before explicit per-thread pinning used a process CPU mask only; do not treat them as placement-controlled comparisons.

## Next Step

All five follow-up ideas have been measured with explicit pinning and raw
evidence. Replicate the two directional leads (pre-reservation and mask wrap)
on a separate quiet session before changing defaults.
