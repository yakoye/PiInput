#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/linux"
INSTALL="$ROOT/dist/linux-x64"
TEST_DATA_DIR="${1:-}"
ARGS=(-S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPIINPUT_BUILD_TESTS=ON)
if [[ -n "$TEST_DATA_DIR" ]]; then
  ARGS+=("-DPIINPUT_TESTDATA_DIR=$TEST_DATA_DIR")
fi
cmake "${ARGS[@]}"
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
cmake --install "$BUILD" --prefix "$INSTALL"
