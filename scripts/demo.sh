#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BIN="build/release/apps/btos_cli/btos"
if [[ ! -x "$BIN" ]]; then
  echo "CLI not built; running scripts/build.sh release first"
  scripts/build.sh release
fi

WORK="$(mktemp -d)"
echo "Working directory: $WORK"
mkdir -p "$WORK/out"

echo "== 1. Generate synthetic data =="
"$BIN" synth --out "$WORK/syn.btosd" --bars 2000 --seed 42

echo "== 2. Validate =="
"$BIN" validate --data "$WORK/syn.btosd"

echo "== 3. Run backtest =="
cat > "$WORK/run.json" << JSON
{
  "seed": 42,
  "data": [{"symbol": "SYN", "path": "$WORK/syn.btosd", "period_sec": 86400}],
  "strategy": {"name": "ma_crossover", "symbol": "SYN",
               "params": {"fast": 5, "slow": 20, "quantity": 100}},
  "execution": {"latency_ms": 1, "slippage_bps": 1.0, "commission_per_share": 0.005},
  "portfolio": {"initial_capital": 1000000.0, "base_currency": "USD"}
}
JSON
"$BIN" run --config "$WORK/run.json" --report "$WORK/out/report.html" \
       --metrics "$WORK/out/metrics.json" --db "$WORK/out/experiments.sqlite" \
       --event-log "$WORK/out/events.log"

echo "== 4. Replay (determinism) =="
"$BIN" replay --event-log "$WORK/out/events.log"

echo "== 5. Query experiment store =="
"$BIN" report --db "$WORK/out/experiments.sqlite" --limit 5

echo "Demo complete. Report at $WORK/out/report.html"
