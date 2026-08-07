#!/usr/bin/env bash
# Common helpers for cpp-trading-core scripts.

set -euo pipefail

# Resolve repo root (one level above scripts/)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR_RELEASE="${BUILD_DIR_RELEASE:-$ROOT_DIR/build}"
BUILD_DIR_PERF="$ROOT_DIR/build-perf"

NPROC="${NPROC:-$(nproc)}"
