#!/usr/bin/env bash
set -euo pipefail

corpus=${1:?corpus path required}
runs_per_variant=${2:-7}
cpu=${CPU:-0}
repeats=${REPEATS:-20}
warmup=${WARMUP:-1}
expected_topic=${EXPECTED_TOPIC:-orderbook.1000.BTCUSDT}
baseline_build=${BASELINE_BUILD:-build-snapshot-baseline}
reuse_build=${REUSE_BUILD:-build-snapshot-reuse}
out="results/bybit-l2-resnapshot-$(date -u +%Y%m%dT%H%M%S%NZ)"

cmake -S . -B "$baseline_build" -DCMAKE_BUILD_TYPE=Release -DMARKET_DATA_BOOK_REUSE_SORTED_SNAPSHOT=OFF
cmake -S . -B "$reuse_build" -DCMAKE_BUILD_TYPE=Release -DMARKET_DATA_BOOK_REUSE_SORTED_SNAPSHOT=ON
cmake --build "$baseline_build" --target trading_bench_bybit_l2_live_path -j"$(nproc)"
cmake --build "$reuse_build" --target trading_bench_bybit_l2_live_path -j"$(nproc)"

mkdir -p "$out"
printf 'run,variant,type,frames,decode_p50_ns,decode_p95_ns,decode_p99_ns,envelope_p50_ns,bids_p50_ns,asks_p50_ns,apply_p50_ns,apply_p95_ns,apply_p99_ns,total_p50_ns,total_p95_ns,total_p99_ns,vector_capacity_growths\n' > "$out/raw.csv"
for ((run=1; run<=2 * runs_per_variant; ++run)); do
  if ((run % 4 == 1 || run % 4 == 0)); then
    variant=baseline
    binary="$baseline_build/trading_bench_bybit_l2_live_path"
  else
    variant=reuse
    binary="$reuse_build/trading_bench_bybit_l2_live_path"
  fi
  taskset -c "$cpu" "$binary" "$corpus" "$repeats" "$warmup" "$expected_topic" 2>/dev/null |
    tail -n +2 | sed "s/^/$run,$variant,/" >> "$out/raw.csv"
done
printf 'corpus=%s\nruns_per_variant=%s\ncpu=%s\nrepeats=%s\nwarmup=%s\nexpected_topic=%s\nbaseline_build=%s\nreuse_build=%s\n' \
  "$corpus" "$runs_per_variant" "$cpu" "$repeats" "$warmup" "$expected_topic" "$baseline_build" "$reuse_build" > "$out/environment.txt"
printf 'variant,snapshot_apply_p50_median_ns,snapshot_apply_p99_median_ns\n' > "$out/summary.csv"
median_line=$(((runs_per_variant + 1) / 2))
for variant in baseline reuse; do
  p50=$(awk -F, -v variant="$variant" 'NR > 1 && $2 == variant && $3 == "snapshot" {print $11}' "$out/raw.csv" | sort -n | sed -n "${median_line}p")
  p99=$(awk -F, -v variant="$variant" 'NR > 1 && $2 == variant && $3 == "snapshot" {print $13}' "$out/raw.csv" | sort -n | sed -n "${median_line}p")
  printf '%s,%s,%s\n' "$variant" "$p50" "$p99" >> "$out/summary.csv"
done
printf '%s\n' "$out"
