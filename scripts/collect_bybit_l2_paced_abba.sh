#!/usr/bin/env bash
set -euo pipefail

corpus=${1:?corpus path required}
cpu=${CPU:-0}
repeats=${REPEATS:-2}
inter_frame_delay_us=${INTER_FRAME_DELAY_US:-20000}
out="results/bybit-l2-paced-abba-$(date -u +%Y%m%dT%H%M%S%NZ)"

mkdir -p "$out"
printf 'run,variant,frames,decode_p50_ns,decode_p99_ns,level_arrays_p50_ns,level_arrays_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns\n' > "$out/raw.csv"
run=0
for variant in bounded one-pass one-pass bounded bounded one-pass; do
  run=$((run + 1))
  taskset -c "$cpu" ./build/trading_bench_bybit_l2_replay "$corpus" "$variant" "$repeats" "$inter_frame_delay_us" |
    tail -n 1 | sed "s/^/$run,/" >> "$out/raw.csv"
done
printf 'corpus=%s\ncpu=%s\nrepeats=%s\ninter_frame_delay_us=%s\norder=bounded,one-pass,one-pass,bounded,bounded,one-pass\n' \
  "$corpus" "$cpu" "$repeats" "$inter_frame_delay_us" > "$out/environment.txt"
printf 'variant,total_p50_median_ns,total_p99_median_ns\n' > "$out/summary.csv"
for variant in bounded one-pass; do
  p50=$(awk -F, -v variant="$variant" 'NR > 1 && $2 == variant {print $10}' "$out/raw.csv" | sort -n | sed -n '2p')
  p99=$(awk -F, -v variant="$variant" 'NR > 1 && $2 == variant {print $11}' "$out/raw.csv" | sort -n | sed -n '2p')
  printf '%s,%s,%s\n' "$variant" "$p50" "$p99" >> "$out/summary.csv"
done
printf '%s\n' "$out"
