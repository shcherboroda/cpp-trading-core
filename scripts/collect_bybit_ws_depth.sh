#!/usr/bin/env bash
set -euo pipefail

runs=${1:-3}
messages=${2:-30}
symbol=${SYMBOL:-BTCUSDT}
depths=${DEPTHS:-"1 50 1000"}
timeout_seconds=${TIMEOUT_SECONDS:-60}
cpu=${CPU:-}
out="results/bybit-ws-depth-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out"

{
    printf 'symbol=%s\n' "$symbol"
    printf 'runs=%s\n' "$runs"
    printf 'messages=%s\n' "$messages"
    printf 'depths=%s\n' "$depths"
    printf 'timeout_seconds=%s\n' "$timeout_seconds"
    printf 'cpu=%s\n' "${cpu:-unrestricted}"
    uname -a
    git rev-parse HEAD
    git status --short
} > "$out/environment.txt"

for depth in $depths; do
    for ((run = 1; run <= runs; ++run)); do
        if [[ -n "$cpu" ]]; then
            timeout "$timeout_seconds" taskset -c "$cpu" \
                ./build/trading_bybit_ws_orderbook_live "$symbol" "$messages" /dev/null "$depth" \
                > "$out/depth-${depth}-run-${run}.txt" 2>&1 || true
        else
            timeout "$timeout_seconds" ./build/trading_bybit_ws_orderbook_live \
                "$symbol" "$messages" /dev/null "$depth" \
                > "$out/depth-${depth}-run-${run}.txt" 2>&1 || true
        fi
    done
done

printf '%s\n' "$out"
