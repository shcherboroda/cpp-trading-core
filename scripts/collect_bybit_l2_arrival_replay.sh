#!/usr/bin/env bash
set -euo pipefail

corpus=${1:?corpus path required}
runs=${2:-7}
cpu=${CPU:-0}
repeats=${REPEATS:-20}
burst_sizes=${BURST_SIZES:-"1 8 16"}
burst_gaps_ns=${BURST_GAPS_NS:-"1000000 100000 10000"}
out="results/bybit-l2-arrival-replay-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out"

printf 'run,model,repeats,burst_size,burst_gap_ns,frames,service_p50_ns,service_p95_ns,service_p99_ns,queue_p50_ns,queue_p95_ns,queue_p99_ns,queue_max_ns,burst_start_queue_p50_ns,burst_start_queue_p95_ns,burst_start_queue_p99_ns,burst_start_queue_max_ns,e2e_p50_ns,e2e_p95_ns,e2e_p99_ns,e2e_max_ns,checksum\n' > "$out/raw.csv"
for ((run = 1; run <= runs; ++run)); do
    for burst_size in $burst_sizes; do
        for burst_gap_ns in $burst_gaps_ns; do
            taskset -c "$cpu" ./build/trading_bench_bybit_l2_arrival_replay "$corpus" \
                "--repeats=$repeats" "--burst-size=$burst_size" "--burst-gap-ns=$burst_gap_ns" --format=csv \
                | tail -n 1 | sed "s/^/$run,/" >> "$out/raw.csv"
        done
    done
done

{
    printf 'corpus=%s\n' "$corpus"
    printf 'runs=%s\n' "$runs"
    printf 'cpu=%s\n' "$cpu"
    printf 'repeats=%s\n' "$repeats"
    printf 'burst_sizes=%s\n' "$burst_sizes"
    printf 'burst_gaps_ns=%s\n' "$burst_gaps_ns"
    printf 'command=taskset -c %s ./build/trading_bench_bybit_l2_arrival_replay %s --repeats=%s --burst-size=N --burst-gap-ns=N --format=csv\n' "$cpu" "$corpus" "$repeats"
    uname -a
    git rev-parse HEAD
    git status --short
} > "$out/environment.txt"

printf '%s\n' "$out/raw.csv"
