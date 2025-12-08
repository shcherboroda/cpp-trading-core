#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_PERF/trading_bybit_ws_orderbook_live"

SYMBOL="${1:-BTCUSDT}"
MAX_MSG="${2:-1000}"

echo "[perf_ws_live_bench] perf stat..."
perf stat -d "$BIN" "$SYMBOL" "$MAX_MSG"

echo
echo "[perf_ws_live_bench] perf record -> perf.ws.data..."
perf record -F 999 -g -o perf.ws.data -- "$BIN" "$SYMBOL" "$MAX_MSG"

echo
echo "[perf_ws_live_bench] perf report (head)..."
perf report -i perf.ws.data --stdio | head -n 60
