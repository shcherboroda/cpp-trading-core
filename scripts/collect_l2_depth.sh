#!/usr/bin/env bash
# Collect fixed-CPU L2 snapshot or delta measurements at one book depth.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

RUNS="${1:-7}"
LEVELS_PER_SIDE="${2:-50}"
ITERATIONS="${3:-10000}"
MODE="${MODE:-snapshot}"
IMPLEMENTATION="${IMPLEMENTATION:-map}"
WARMUP="${WARMUP:-1000}"
CPU="${CPU:-0}"
RESULT_LABEL="${RESULT_LABEL:-l2-${MODE}-${LEVELS_PER_SIDE}}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/results/l2-${RESULT_LABEL}-$(date -u +%Y%m%dT%H%M%SZ)}"
BIN="$BUILD_DIR_RELEASE/trading_bench_market_data_book"

if [[ ! -x "$BIN" ]]; then
  echo "[collect_l2_depth] Binary not found: $BIN" >&2
  exit 1
fi

if [[ ! "$RUNS" =~ ^[1-9][0-9]*$ || ! "$LEVELS_PER_SIDE" =~ ^[1-9][0-9]*$ || ! "$ITERATIONS" =~ ^[1-9][0-9]*$ || ! "$WARMUP" =~ ^[0-9]+$ || ! "$CPU" =~ ^[0-9]+$ ]]; then
  echo "RUNS, LEVELS_PER_SIDE, ITERATIONS, WARMUP, and CPU must be valid integers" >&2
  exit 1
fi

case "$MODE" in snapshot|delta-update|delta-mixed) ;; *) echo "Unsupported MODE: $MODE" >&2; exit 1 ;; esac
case "$IMPLEMENTATION" in map|flat) ;; *) echo "Unsupported IMPLEMENTATION: $IMPLEMENTATION" >&2; exit 1 ;; esac

mkdir -p "$OUTPUT_DIR/raw"
{
  echo "commit=$(git rev-parse HEAD)"
  echo "mode=$MODE"
  echo "implementation=$IMPLEMENTATION"
  echo "levels_per_side=$LEVELS_PER_SIDE"
  echo "iterations=$ITERATIONS"
  echo "warmup=$WARMUP"
  echo "cpu=$CPU"
  echo "binary_sha256=$(sha256sum "$BIN")"
  echo "source_sha256=$(sha256sum "$ROOT_DIR/app/bench_market_data_book_main.cpp" "$ROOT_DIR/src/market_data_order_book.cpp" "$ROOT_DIR/src/flat_market_data_order_book.cpp" "$ROOT_DIR/scripts/collect_l2_depth.sh")"
  echo "git_status:"
  git status --short
  echo "command=taskset -c $CPU $BIN --levels-per-side=$LEVELS_PER_SIDE --iterations=$ITERATIONS --warmup=$WARMUP --mode=$MODE --implementation=$IMPLEMENTATION --format=csv"
  uname -a
  g++ --version | head -n 1
  cmake --version | head -n 1
  uptime
} > "$OUTPUT_DIR/environment.txt"

echo "run,implementation,mode,levels_per_side,iterations,warmup,updates_per_batch,cpu_start,cpu_end,elapsed_ns,batches_per_second,p50_ns,p95_ns,p99_ns,checksum" > "$OUTPUT_DIR/runs.csv"
for run in $(seq 1 "$RUNS"); do
  result="$(taskset -c "$CPU" "$BIN" "--levels-per-side=$LEVELS_PER_SIDE" "--iterations=$ITERATIONS" "--warmup=$WARMUP" "--mode=$MODE" "--implementation=$IMPLEMENTATION" --format=csv)"
  printf '%s\n' "$result" > "$OUTPUT_DIR/raw/run-${run}.csv"
  printf '%s,%s\n' "$run" "$result" >> "$OUTPUT_DIR/runs.csv"
done

echo "Collected $RUNS L2 $MODE runs in $OUTPUT_DIR"
