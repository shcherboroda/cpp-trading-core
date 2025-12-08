#!/usr/bin/env bash
# Configure and build Release + run tests.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "[build_release] Configuring CMake (Release)..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_RELEASE" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

echo "[build_release] Building..."
cmake --build "$BUILD_DIR_RELEASE" -j"$NPROC"

echo "[build_release] Running tests..."
ctest --test-dir "$BUILD_DIR_RELEASE" --output-on-failure
