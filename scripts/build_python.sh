#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "Building pybind11 module"
cmake -S . -G Ninja -B build/py -DCMAKE_BUILD_TYPE=Release \
  -DBTOS_BUILD_PYTHON=ON -DBTOS_BUILD_TESTS=OFF -DBTOS_BUILD_BENCH=OFF \
  -DBTOS_BUILD_EXAMPLE_PLUGINS=OFF
cmake --build build/py

SO="$(find build/py -name '_btos*.so' | head -1)"
if [[ -z "$SO" ]]; then
  echo "Build did not produce _btos module" >&2
  exit 1
fi
cp "$SO" python/btos/
echo "Module placed at python/btos/$(basename "$SO")"
echo "You can now: cd python && python -c 'import btos'"
