#!/usr/bin/env bash
# Configure and build RelWithDebInfo for perf profiling.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "[build_perf] Configuring CMake (RelWithDebInfo)..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_PERF" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=OFF

echo "[build_perf] Building..."
cmake --build "$BUILD_DIR_PERF" -j"$NPROC"
