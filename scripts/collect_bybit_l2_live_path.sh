#!/usr/bin/env bash
set -euo pipefail
corpus=${1:?corpus path required}
runs=${2:-15}
cpu=${CPU:-0}
repeats=${REPEATS:-20}
warmup=${WARMUP:-1}
expected_topic=${EXPECTED_TOPIC:-orderbook.50.BTCUSDT}
out="results/bybit-l2-live-path-$(date -u +%Y%m%dT%H%M%S%NZ)"
mkdir -p "$out"
printf 'run,type,frames,decode_p50_ns,decode_p95_ns,decode_p99_ns,envelope_p50_ns,bids_p50_ns,asks_p50_ns,apply_p50_ns,apply_p95_ns,apply_p99_ns,total_p50_ns,total_p95_ns,total_p99_ns,vector_capacity_growths\n' > "$out/raw.csv"
for ((run=1; run<=runs; ++run)); do
  taskset -c "$cpu" ./build/trading_bench_bybit_l2_live_path "$corpus" "$repeats" "$warmup" "$expected_topic" | tail -n +2 | sed "s/^/$run,/" >> "$out/raw.csv"
done
printf 'corpus=%s\nruns=%s\ncpu=%s\nrepeats=%s\nwarmup=%s\nexpected_topic=%s\n' "$corpus" "$runs" "$cpu" "$repeats" "$warmup" "$expected_topic" > "$out/environment.txt"
printf '%s\n' "$out/raw.csv"
