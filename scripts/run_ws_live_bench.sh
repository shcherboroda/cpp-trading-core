#!/usr/bin/env bash
# Run Bybit WS live orderbook handler benchmark in Release build.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_RELEASE/trading_bybit_ws_orderbook_live"
SYMBOL="${1:-BTCUSDT}"
DEPTH="${2:-50}"
MAX_MESSAGES="${3:-1000}"

if [[ ! -x "$BIN" ]]; then
  echo "[run_ws_live_bench] Binary not found: $BIN"
  echo "  Did you run scripts/build_release.sh?"
  exit 1
fi

echo "[run_ws_live_bench] Running $BIN $SYMBOL $DEPTH $MAX_MESSAGES..."
"$BIN" "$SYMBOL" "$DEPTH" "$MAX_MESSAGES"
