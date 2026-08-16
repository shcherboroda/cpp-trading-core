#!/usr/bin/env bash
set -euo pipefail

levels=${1:-1000}
iterations=${2:-1000}
runs=${3:-15}
cpu=${CPU:-0}
out_dir="results/bybit-l2-handler-${levels}-$(date -u +%Y%m%dT%H%M%SZ)"

mkdir -p "$out_dir"
{
    echo "utc=$(date -u +%FT%TZ)"
    echo "levels=$levels"
    echo "iterations=$iterations"
    echo "runs=$runs"
    echo "cpu=$cpu"
    uname -a
    lscpu | sed '/^$/d'
    c++ --version
} > "$out_dir/environment.txt"

echo "run,levels,conversion,parser,input,parse_p50_ns,parse_p99_ns,convert_p50_ns,convert_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns" > "$out_dir/raw.csv"
for ((run = 1; run <= runs; ++run)); do
    for variant in "stod-copy dom direct" "fixed-ref sax direct" "stod-ref dom direct" "fixed-ref dom direct" "fixed-ref sax copy"; do
        read -r conversion parser input <<< "$variant"
        taskset -c "$cpu" ./build/trading_bench_bybit_l2_handler "$levels" "$iterations" "$conversion" "$parser" "$input" |
            tail -n 1 | sed "s/^/${run},/" >> "$out_dir/raw.csv"
    done
done

echo "$out_dir/raw.csv"
