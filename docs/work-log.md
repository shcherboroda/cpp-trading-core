# Work Log

## Current Objective

Evaluate one isolated SPSC queue-layout variant against the local WSL2 pipeline baseline.

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
- benchmark command: `CPUS=0,2 LATENCY_MODE=pre-push ./scripts/collect_mt_baseline.sh 7 2000000 42`.
- CPU affinity: Linux vCPUs `0,2`, confirmed inside a child process.
- core type: unknown; WSL virtual topology cannot establish P-core/E-core identity.
- benchmark result: median 3.482 M events/s; p50/p95/p99 1.033/1.900/3.309 ms. Raw CSV is uncommitted in `results/mt-baseline-20260805T100655Z/`.

## Decisions

- Reuse the existing repository.
- Prefer one focused experiment over new features.
- Treat WSL2 limitations explicitly.
- Do not imply commercial trading or HFT experience.

## Changes Made

- Added a synchronized benchmark start, explicit latency semantics, CSV output, and a fixed-affinity multi-run collector for `trading_mt_bench`.
- Added a reproducible `SPSC_QUEUE_PAD_INDICES` CMake experiment switch. It is disabled by default because the controlled result is inconclusive for p99 latency.
- Retained raw benchmark evidence in `results/` and documented its status in `results/README.md`.

## Results

- Seven-run `pre-push` baseline and seven-run latency-off control were collected after a successful Release build/test.
- Latency sampling reduced median throughput from 4.736 to 3.482 M events/s in this setup; treat this as benchmark instrumentation cost, not as an algorithm comparison.
- In balanced 14-run-per-variant cache-line-padding measurement, padded median throughput was 3.996 vs 3.475 M events/s and p50/p95 improved, but p99 was unchanged within noise. Do not claim an optimization; see `doc/performance.md`.

## Open Issues

- WSL exposes virtual CPU topology only; P-core/E-core mapping remains unverified and must not be inferred from vCPU numbers.
- WSL2 scheduler/background variability is material; retain all seven runs and report ranges for every comparison.

## Next Step

Choose a new single-factor hypothesis only after reviewing the inconclusive
cache-line-padding result. Preserve the same command and result-archive
discipline for the next experiment.
