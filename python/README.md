# BTOS Python bindings

A thin pybind11 layer over the C++ `btos::Engine` facade. The design boundary is
deliberate: Python configures and launches a whole backtest and receives results back
as NumPy arrays, while the simulation itself runs entirely in the C++ core. Python is
never called back into on the hot path, so the engine's determinism guarantee is
preserved (the same config produces the same event log hash whether launched from C++
or Python) and the fast path stays in C++. Research, plotting, and statistical work
happen in Python where the ecosystem is strongest.

## Build

```
cmake -S . -G Ninja -B build/py -DCMAKE_BUILD_TYPE=Release \
  -DBTOS_BUILD_PYTHON=ON -DBTOS_BUILD_TESTS=OFF -DBTOS_BUILD_BENCH=OFF
cmake --build build/py
```

The extension module is written to `build/py/pybtos/btos/`. Copy it next to the
`python/btos/` package, or add both to `PYTHONPATH`, so `import btos` resolves the
package and its compiled `_btos` extension.

## Usage

```python
import btos

bars = btos.synthetic_gbm(s0=100, mu=0.08, sigma=0.2, n=2000, seed=42)

result = btos.run_backtest({
    "strategy": "ma_crossover",
    "symbol": "SYN",
    "bars": {"SYN": bars},
    "params": {"fast": 5, "slow": 20, "quantity": 100},
    "commission": {"model": "per_share", "rate": 0.005, "min": 1.0},
    "initial_capital": 1_000_000.0,
    "seed": 42,
})

print(result.final_equity, hex(result.event_log_hash))
metrics = result.metrics(periods_per_year=252)
df = result.to_frame()          # pandas DataFrame indexed by datetime64[ns]
returns = result.returns()      # numpy array of per-bar returns
```

## API

Data:

- `synthetic_gbm(...)`, `synthetic_heston(...)`: seeded synthetic bar series.
- `load_bars(path, period_seconds)`: load a `.btosd` or `.csv` bar file.

Running:

- `run_backtest(config) -> BacktestResult`. Config keys: `strategy`
  (`ma_crossover`, `pairs`, `market_maker`), `symbol` or `symbols`, `bars`
  (symbol to reader), `params`, `commission`, `slippage`, `latency_ms`,
  `initial_capital`, `risk`, `currency`, `seed`.

Results (`BacktestResult`):

- `.equity` (float64), `.equity_ts` (datetime64[ns]), `.fills` (dict of arrays),
  `.final_equity`, `.event_log_hash`, `.run_id`.
- `.returns()`, `.metrics(periods_per_year, benchmark_returns)`, `.to_frame()`.

## Interactive app

A thin Streamlit view over the Python API lets you configure and run a backtest in the
browser and see the equity curve and metrics live. It contains no simulation logic: it
only calls `run_backtest` and displays the arrays that come back, so the deterministic
C++ core remains the single source of truth.

```
pip install -r python/requirements.txt
scripts/run_app.sh
```

Or directly:

```
cd python && streamlit run app/streamlit_app.py
```

## Cross-validation

`python/validation/cross_validate.py` checks the engine's accounting against two
independent references on identical data: a from-scratch NumPy ledger (matches exactly,
relative difference 0.0) and vectorbt 1.1.0 (agrees to about 8e-5, the small residual
being a known convention difference). This is external correctness validation the
internal C++ tests cannot provide on their own.

```
python python/validation/cross_validate.py
```

## Tests

```
python -m pytest python/tests
```
