import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import btos


def base_config(seed=42, n=1000):
    return {
        "strategy": "ma_crossover",
        "symbol": "SYN",
        "bars": {"SYN": btos.synthetic_gbm(n=n, seed=seed)},
        "params": {"fast": 5, "slow": 20, "quantity": 100},
        "initial_capital": 1_000_000.0,
        "seed": seed,
    }


def test_synthetic_gbm_is_seeded():
    a = btos.run_backtest(base_config(seed=1))
    b = btos.run_backtest(base_config(seed=1))
    assert a.event_log_hash == b.event_log_hash


def test_distinct_seed_distinct_hash():
    a = btos.run_backtest(base_config(seed=1))
    b = btos.run_backtest(base_config(seed=2))
    assert a.event_log_hash != b.event_log_hash


def test_result_arrays_have_expected_types():
    r = btos.run_backtest(base_config())
    assert r.equity.dtype == np.float64
    assert r.equity_ts.dtype == np.dtype("datetime64[ns]")
    assert r.equity.shape[0] == r.equity_ts.shape[0]
    assert np.isfinite(r.final_equity)


def test_returns_length_matches_equity():
    r = btos.run_backtest(base_config())
    assert r.returns().shape[0] == r.equity.shape[0] - 1


def test_metrics_are_finite_and_keyed():
    r = btos.run_backtest(base_config())
    m = r.metrics(periods_per_year=252)
    for key in (
        "total_return",
        "cagr",
        "sharpe",
        "max_drawdown",
        "n_trades",
        "win_rate",
    ):
        assert key in m
    assert np.isfinite(m["sharpe"])
    assert 0.0 <= m["win_rate"] <= 1.0


def test_market_maker_bounded_inventory():
    r = btos.run_backtest(
        {
            "strategy": "market_maker",
            "symbol": "SYN",
            "bars": {"SYN": btos.synthetic_gbm(n=400, seed=9)},
            "params": {"spread_bps": 20, "quote_size": 50, "max_inventory": 500},
            "seed": 9,
        }
    )
    f = r.fills
    inventory = np.cumsum(f["quantity"] * f["side"])
    assert np.max(np.abs(inventory)) <= 500 + 50


def test_unknown_strategy_raises():
    with pytest.raises(Exception):
        btos.run_backtest({"strategy": "does_not_exist", "symbol": "S", "bars": {}})


def test_commission_reduces_equity():
    free = btos.run_backtest(base_config(seed=3))
    with_comm_cfg = base_config(seed=3)
    with_comm_cfg["commission"] = {"model": "per_share", "rate": 0.01, "min": 1.0}
    charged = btos.run_backtest(with_comm_cfg)
    assert charged.final_equity <= free.final_equity


def test_bars_to_arrays_shapes_match():
    reader = btos.synthetic_gbm(n=500, seed=4)
    a = btos.bars_to_arrays(reader)
    assert a["close"].shape == (500,)
    assert a["ts"].dtype == np.dtype("datetime64[ns]")
    assert np.all(a["high"] >= a["low"])


def test_cross_validation_matches_independent_ledger():
    import pandas as pd

    fast, slow, qty, cap, seed, n = 5, 20, 100.0, 1_000_000.0, 42, 1000
    reader = btos.synthetic_gbm(s0=100.0, mu=0.08, sigma=0.2, n=n, seed=seed)
    bars = btos.bars_to_arrays(reader)
    result = btos.run_backtest(
        {
            "strategy": "ma_crossover",
            "symbol": "SYN",
            "bars": {"SYN": reader},
            "params": {"fast": fast, "slow": slow, "quantity": qty},
            "initial_capital": cap,
            "seed": seed,
        }
    )
    c, o = bars["close"], bars["open"]
    fast_ma = pd.Series(c).rolling(fast).mean().to_numpy()
    slow_ma = pd.Series(c).rolling(slow).mean().to_numpy()
    state = np.zeros(n, dtype=int)
    pos = 0
    for t in range(n):
        if np.isnan(slow_ma[t]):
            state[t] = pos
            continue
        if fast_ma[t] > slow_ma[t] and pos <= 0:
            pos = 1
        elif fast_ma[t] < slow_ma[t] and pos > 0:
            pos = 0
        state[t] = pos
    exec_state = np.concatenate([[0], state[:-1]])
    cash, shares = cap, 0.0
    for t in range(1, n):
        target = exec_state[t] * qty
        delta = target - shares
        if delta != 0.0:
            cash -= delta * o[t]
            shares = target
    ref_final = cash + shares * c[-1]
    rel = abs(result.final_equity - ref_final) / abs(ref_final)
    assert rel < 1e-6
