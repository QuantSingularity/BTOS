#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! ls python/btos/_btos*.so >/dev/null 2>&1; then
  echo "Python module not found; building it first"
  scripts/build_python.sh
fi

if ! python3 -c "import streamlit" >/dev/null 2>&1; then
  echo "Streamlit is not installed. Install it with: pip install streamlit"
  exit 1
fi

cd python
exec streamlit run app/streamlit_app.py
