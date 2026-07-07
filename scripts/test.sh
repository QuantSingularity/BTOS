#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "== Release build and tests (GCC) =="
cmake --preset release
cmake --build --preset release
./build/release/tests/btos_tests

echo "== ASan + UBSan build and tests =="
cmake --preset asan
cmake --build --preset asan
./build/asan/tests/btos_tests

if command -v clang++ >/dev/null 2>&1; then
  echo "== Clang release build and tests (second compiler) =="
  rm -rf build/clang
  CC=clang CXX=clang++ cmake -S . -G Ninja -B build/clang -DCMAKE_BUILD_TYPE=Release
  cmake --build build/clang
  ./build/clang/tests/btos_tests
else
  echo "clang++ not found; skipping second-compiler pass"
fi

echo "All test passes completed."
