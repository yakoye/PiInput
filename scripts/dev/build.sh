#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DATA_DIR="${1:-}"
exec "$ROOT/scripts/build_linux.sh" "$TEST_DATA_DIR"
