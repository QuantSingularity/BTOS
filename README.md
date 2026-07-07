# BTOS: Backtesting Operating System

A modular, event-driven C++20 platform for researching systematic trading strategies:
deterministic event kernel, point-in-time data layer, exchange simulation with latency
and impact, multi-currency portfolio accounting, risk engine, parameter optimizers,
walk-forward and purged K-fold validation, deflated Sharpe ratio, experiment tracking,
and HTML analytics reports.

## Requirements

- CMake 3.25+ and Ninja
- GCC 13+ or Clang 18+ (any C++20 compiler); the suite builds warnings-as-errors
  clean under both
- Network access for FetchContent on first configure (nlohmann/json, GoogleTest,
  rapidcheck, Google Benchmark, all pinned)

## Build

```
cmake --preset release
cmake --build --preset release
```

Other presets: `debug`, `relwithdebinfo`, `asan` (address + undefined), `tsan`.

## Quickstart on synthetic data (under one minute)

```
cd build/release
B=apps/btos_cli/btos

$B synth --out syn.btosd --bars 1500 --seed 42
$B validate --data syn.btosd

cat > run.json << 'EOF'
{
  "seed": 42,
  "data": [{"symbol": "SYN", "path": "syn.btosd", "period_sec": 86400}],
  "strategy": {"name": "ma_crossover", "symbol": "SYN",
               "params": {"fast": 8, "slow": 30, "quantity": 100}},
  "execution": {"latency_ms": 1, "slippage_bps": 2, "commission_bps": 1},
  "portfolio": {"initial_capital": 1000000}
}
EOF

$B run --config run.json --report report.html --metrics metrics.json \
       --db experiments.jsonl --event-log events.log
$B replay --event-log events.log
$B report --db experiments.jsonl
```

Open `report.html` for the equity curve, drawdowns, monthly returns, and trade
statistics. Running the same config twice produces a byte-identical event log hash.

The `--db` flag accepts either a SQLite file (default for any extension other than
`.jsonl`, for example `experiments.sqlite`) or a `.jsonl` file for a lightweight,
greppable append-only log. Both implement the same query surface.

## Optimization

```
cat > opt.json << 'EOF'
{
  "seed": 42,
  "data": [{"symbol": "SYN", "path": "syn.btosd", "period_sec": 86400}],
  "strategy": {"name": "ma_crossover", "symbol": "SYN", "params": {"quantity": 100}},
  "optimize": {"method": "random", "budget": 24, "metric": "sharpe",
    "space": [{"name": "fast", "lo": 3, "hi": 15, "step": 1},
              {"name": "slow", "lo": 20, "hi": 60, "step": 5}]}
}
EOF
$B optimize --config opt.json --db experiments.jsonl
```

Methods: `grid`, `random`, `bayes` (GP with expected improvement), `cmaes`. The
optimizer prints the deflated Sharpe ratio of the best candidate given the number of
trials, penalizing selection bias.

## Ingesting real data

CSV bars with header `ts,open,high,low,close,volume` (ISO-8601 UTC timestamps):

```
$B ingest --csv bars.csv --out bars.btosd --period-sec 86400
```

## Tests and benchmarks

```
ctest --test-dir build/release
./build/release/bench/btos_bench
```

On the reference environment (Ubuntu 24, single core) the suite is 91 of 91 passing
under the release preset with both GCC 13.3 and Clang 18, and again under `asan` with
the per-fill accounting-identity check enabled; the concurrency tests also pass under
`tsan`. Coverage includes a regression suite over a checked-in messy-data fixture and
an end-to-end test that dlopens a real out-of-tree plugin `.so` (see
`examples/plugins`). Measured throughput: kernel dispatch 6.71M events/sec,
interleaved schedule-and-dispatch 57.3M events/sec, order book matching 11.49M
orders/sec and 8.76M fills/sec.

## Python bindings

The engine can be driven from Python through a pybind11 module. The full simulation
runs in C++ at whole-run granularity, so determinism is preserved: a given (data,
params, seed) triple always produces the same run_id and event log hash. The boundary
is deliberately at run granularity, not per-bar, which keeps the hot path in C++ and
avoids putting non-deterministic Python on the simulation path.

```
cmake -S . -B build/py -G Ninja -DBTOS_BUILD_PYTHON=ON
cmake --build build/py
cd build/py/python
```

```python
import btos, numpy as np, pandas as pd

btos.write_synthetic(out="syn.btosd", model="gbm", seed=7, bars=2000)
r = btos.run_backtest(
    data_paths={"SYN": "syn.btosd"},
    strategy="ma_crossover", symbols={"symbol": "SYN"},
    params={"fast": 5, "slow": 20, "quantity": 100}, seed=7,
)
equity = pd.Series(
    r.equity_values,
    index=pd.to_datetime(np.array(r.equity_times_ns, dtype="int64"), unit="ns"),
)
print(r.run_id, r.final_equity, len(r.fills))
```

Simulation stays in C++; data wrangling, metrics, and plotting happen in Python.
`python/example_research.py` runs a parameter sweep this way, and
`python/test_bindings.py` checks import, determinism, seed sensitivity, and pandas
conversion.

## Python bindings

BTOS ships an optional pybind11 layer so you can drive the C++ engine from Python and
do research, plotting, and analysis in the Python ecosystem while the simulation stays
in the fast, deterministic C++ core:

```
cmake -S . -G Ninja -B build/py -DCMAKE_BUILD_TYPE=Release -DBTOS_BUILD_PYTHON=ON
cmake --build build/py
```

```python
import btos
bars = btos.synthetic_gbm(n=2000, seed=42)
r = btos.run_backtest({"strategy": "ma_crossover", "symbol": "SYN",
                       "bars": {"SYN": bars}, "params": {"fast": 5, "slow": 20,
                       "quantity": 100}, "seed": 42})
print(r.final_equity, r.metrics()["sharpe"])
```

The language boundary is at run granularity, not per bar, so the determinism guarantee
holds across it: the same config yields the same event log hash from Python or C++.
See `python/README.md` for the full API. A Streamlit app (`scripts/run_app.sh`) gives a
browser view over the same API, and the `scripts/` directory holds convenience wrappers
for building, testing, the CLI demo, and launching the app.

## Library use

`find_package(btos)` after `cmake --install`, then link `btos::lib` and drive the
`btos::Engine` facade directly. See ARCHITECTURE.md for the module map and extension
points, and NOTES.md for design decisions and the deferral register.

## Layout

```
include/btos/   public headers (core, data, exec, portfolio, strategy, risk, opt, analytics)
src/            implementations
apps/btos_cli/  command line interface
python/         pybind11 bindings, wrapper package, Streamlit app, tests, cross-validation
examples/       out-of-tree example plugins built against the public C ABI
scripts/        build, test, demo, and app-launch convenience scripts
tests/          unit, invariant, integration, property, and regression tests
bench/          Google Benchmark suite
docs/           ARCHITECTURE.md lives at the repository root
```
