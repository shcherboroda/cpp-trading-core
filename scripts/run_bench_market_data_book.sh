#!/usr/bin/env bash
# Run MarketDataOrderBook benchmark in Release build.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_RELEASE/trading_bench_market_data_book"

if [[ ! -x "$BIN" ]]; then
  echo "[run_bench_market_data_book] Binary not found: $BIN"
  echo "  Did you run scripts/build_release.sh?"
  exit 1
fi

echo "[run_bench_market_data_book] Running $BIN $* ..."
"$BIN" "$@"
