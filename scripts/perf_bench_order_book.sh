#!/usr/bin/env bash
# Run perf stat + perf record for OrderBook microbenchmark (perf build).

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_PERF/trading_bench_order_book"
DATA_FILE="${1:-perf.bench.data}"

if [[ ! -x "$BIN" ]]; then
  echo "[perf_bench_order_book] Binary not found: $BIN"
  echo "  Did you run scripts/build_perf.sh?"
  exit 1
fi

echo "[perf_bench_order_book] perf stat..."
perf stat -d "$BIN"

echo
echo "[perf_bench_order_book] perf record -> $DATA_FILE..."
perf record -F 999 -g -o "$DATA_FILE" -- "$BIN"

echo
echo "[perf_bench_order_book] perf report (head)..."
perf report -i "$DATA_FILE" --stdio | head -n 60
