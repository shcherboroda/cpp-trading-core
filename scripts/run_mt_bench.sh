#!/usr/bin/env bash
# Run multithreaded bench (producer/consumer) in Release build.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BIN="$BUILD_DIR_RELEASE/trading_mt_bench"
EVENTS="${1:-200000}"
SEED="${2:-42}"
shift $(( $# >= 2 ? 2 : $# ))

if [[ ! -x "$BIN" ]]; then
  echo "[run_mt_bench] Binary not found: $BIN"
  echo "  Did you run scripts/build_release.sh?"
  exit 1
fi

echo "[run_mt_bench] Running $BIN $EVENTS $SEED $*..."
"$BIN" "$EVENTS" "$SEED" "$@"
