#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "Configuring preset: $PRESET"
cmake --preset "$PRESET"
echo "Building preset: $PRESET"
cmake --build --preset "$PRESET"
echo "Done. Binaries under build/$PRESET/"
