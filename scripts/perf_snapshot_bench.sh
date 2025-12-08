#!/usr/bin/env bash
# Run perf stat + perf record for Bybit snapshot benchmark (perf build).

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_PERF/trading_bybit_orderbook_snapshot"
DATA_FILE="${1:-perf.snapshot.data}"
SYMBOL="${2:-BTCUSDT}"
DEPTH="${3:-50}"
RUNS="${4:-5000}"

if [[ ! -x "$BIN" ]]; then
  echo "[perf_snapshot_bench] Binary not found: $BIN"
  echo "  Did you run scripts/build_perf.sh?"
  exit 1
fi

echo "[perf_snapshot_bench] perf stat..."
perf stat -d "$BIN" "$SYMBOL" "$DEPTH" "$RUNS"

echo
echo "[perf_snapshot_bench] perf record -> $DATA_FILE..."
perf record -F 999 -g -o "$DATA_FILE" -- \
  "$BIN" "$SYMBOL" "$DEPTH" "$RUNS"

echo
echo "[perf_snapshot_bench] perf report (head)..."
perf report -i "$DATA_FILE" --stdio | head -n 60
