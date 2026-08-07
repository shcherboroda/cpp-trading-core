#!/usr/bin/env bash
# Collect comparable fixed-affinity runs of the current SPSC pipeline benchmark.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

RUNS="${1:-7}"
EVENTS="${2:-2000000}"
SEED="${3:-42}"
CPUS="${CPUS:-0,2}"
LATENCY_MODE="${LATENCY_MODE:-pre-push}"
BACKOFF_MODE="${BACKOFF_MODE:-yield}"
RESULT_LABEL="${RESULT_LABEL:-baseline}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/results/mt-${RESULT_LABEL}-$(date -u +%Y%m%dT%H%M%SZ)}"
BIN="$BUILD_DIR_RELEASE/trading_mt_bench"

if [[ ! -x "$BIN" ]]; then
  echo "[collect_mt_baseline] Binary not found: $BIN" >&2
  echo "  Did you run scripts/build_release.sh?" >&2
  exit 1
fi

case "$LATENCY_MODE" in
  off|pre-push|enqueued) ;;
  *) echo "LATENCY_MODE must be off, pre-push, or enqueued" >&2; exit 1 ;;
esac

case "$BACKOFF_MODE" in
  yield|pause) ;;
  *) echo "BACKOFF_MODE must be yield or pause" >&2; exit 1 ;;
esac

if [[ ! "$RESULT_LABEL" =~ ^[a-z0-9][a-z0-9._-]*$ ]]; then
  echo "RESULT_LABEL must contain lowercase letters, digits, dots, underscores, or hyphens" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/raw"

{
  echo "commit=$(git rev-parse HEAD)"
  echo "result_label=$RESULT_LABEL"
  echo "backoff_mode=$BACKOFF_MODE"
  echo "binary_sha256=$(sha256sum "$BIN")"
  echo "source_sha256=$(sha256sum "$ROOT_DIR/app/mt_bench_main.cpp" "$ROOT_DIR/include/utils/spsc_queue.hpp" "$ROOT_DIR/scripts/collect_mt_baseline.sh")"
  echo "git_status:"
  git status --short
  echo "command=taskset -c $CPUS $BIN $EVENTS $SEED --latency=$LATENCY_MODE --backoff=$BACKOFF_MODE --format=csv"
  echo "cpus=$CPUS"
  uname -a
  echo
  cmake --version | head -n 1
  uptime
  echo
  g++ --version | head -n 1
  echo
  if [[ -f "$BUILD_DIR_RELEASE/CMakeCache.txt" ]]; then
    grep -E '^(CMAKE_(CXX_COMPILER:FILEPATH|CXX_FLAGS(_RELEASE)?:STRING|BUILD_TYPE:STRING)|SPSC_QUEUE_PAD_INDICES:BOOL)' \
      "$BUILD_DIR_RELEASE/CMakeCache.txt" || true
    echo
  fi
  taskset -c "$CPUS" bash -c 'taskset -pc $$'
  echo
  lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE
} > "$OUTPUT_DIR/environment.txt"

echo "run,processed,seed,queue_capacity,warmup_events,latency_mode,backoff_mode,elapsed_ns,throughput_events_per_s,mean_ns_per_event,latency_samples,p50_ns,p95_ns,p99_ns" > "$OUTPUT_DIR/runs.csv"

for run in $(seq 1 "$RUNS"); do
  result="$(taskset -c "$CPUS" "$BIN" "$EVENTS" "$SEED" "--latency=$LATENCY_MODE" "--backoff=$BACKOFF_MODE" --format=csv)"
  printf '%s\n' "$result" > "$OUTPUT_DIR/raw/run-${run}.csv"
  printf '%s,%s\n' "$run" "$result" >> "$OUTPUT_DIR/runs.csv"
done

echo "Collected $RUNS runs in $OUTPUT_DIR"
