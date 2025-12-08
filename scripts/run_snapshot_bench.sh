#!/usr/bin/env bash
# Run Bybit REST snapshot → OrderBook benchmark in Release build.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_RELEASE/trading_bybit_orderbook_snapshot"
SYMBOL="${1:-BTCUSDT}"
DEPTH="${2:-50}"
RUNS="${3:-5000}"

if [[ ! -x "$BIN" ]]; then
  echo "[run_snapshot_bench] Binary not found: $BIN"
  echo "  Did you run scripts/build_release.sh?"
  exit 1
fi

echo "[run_snapshot_bench] Running $BIN $SYMBOL $DEPTH $RUNS..."
"$BIN" "$SYMBOL" "$DEPTH" "$RUNS"
