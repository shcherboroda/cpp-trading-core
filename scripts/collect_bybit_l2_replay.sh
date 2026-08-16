#!/usr/bin/env bash
set -euo pipefail
corpus=${1:?corpus path required}; runs=${2:-15}; cpu=${CPU:-0}
variants=${VARIANTS:-"direct-topic copy-topic"}
out="results/bybit-l2-replay-$(date -u +%Y%m%dT%H%M%SZ)"; mkdir -p "$out"
printf 'run,variant,frames,decode_p50_ns,decode_p99_ns,level_arrays_p50_ns,level_arrays_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns\n' > "$out/raw.csv"
for ((run=1; run<=runs; ++run)); do
  for variant in $variants; do
    taskset -c "$cpu" ./build/trading_bench_bybit_l2_replay "$corpus" "$variant" 20 | tail -n 1 | sed "s/^/$run,/" >> "$out/raw.csv"
  done
done
printf 'corpus=%s\nruns=%s\ncpu=%s\n' "$corpus" "$runs" "$cpu" > "$out/environment.txt"
echo "$out/raw.csv"
