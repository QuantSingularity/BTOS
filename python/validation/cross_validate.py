import btos
import numpy as np
import pandas as pd

FAST, SLOW, QTY, CAP, SEED, N = 5, 20, 100.0, 1_000_000.0, 42, 2000


def btos_run():
    reader = btos.synthetic_gbm(s0=100.0, mu=0.08, sigma=0.2, n=N, seed=SEED)
    bars = btos.bars_to_arrays(reader)
    result = btos.run_backtest(
        {
            "strategy": "ma_crossover",
            "symbol": "SYN",
            "bars": {"SYN": reader},
            "params": {"fast": FAST, "slow": SLOW, "quantity": QTY},
            "initial_capital": CAP,
            "seed": SEED,
        }
    )
    return bars, result


def independent_ledger(bars):
    c = bars["close"]
    o = bars["open"]
    fast_ma = pd.Series(c).rolling(FAST).mean().to_numpy()
    slow_ma = pd.Series(c).rolling(SLOW).mean().to_numpy()

    state = np.zeros(N, dtype=int)
    pos = 0
    for t in range(N):
        if np.isnan(slow_ma[t]):
            state[t] = pos
            continue
        if fast_ma[t] > slow_ma[t] and pos <= 0:
            pos = 1
        elif fast_ma[t] < slow_ma[t] and pos > 0:
            pos = 0
        state[t] = pos

    exec_state = np.concatenate([[0], state[:-1]])
    cash, shares, n_fills = CAP, 0.0, 0
    for t in range(1, N):
        target = exec_state[t] * QTY
        delta = target - shares
        if delta != 0.0:
            cash -= delta * o[t]
            shares = target
            n_fills += 1
    return cash + shares * c[-1], n_fills


def vectorbt_run(bars):
    try:
        import vectorbt as vbt
    except ImportError:
        return None
    idx = pd.DatetimeIndex(bars["ts"])
    close = pd.Series(bars["close"], index=idx)
    open_ = pd.Series(bars["open"], index=idx)
    fast_ma = vbt.MA.run(close, FAST).ma
    slow_ma = vbt.MA.run(close, SLOW).ma
    entries = fast_ma.vbt.crossed_above(slow_ma)
    exits = fast_ma.vbt.crossed_below(slow_ma)
    pf = vbt.Portfolio.from_signals(
        close=close,
        price=open_.shift(-1),
        entries=entries,
        exits=exits,
        size=QTY,
        size_type="amount",
        direction="longonly",
        init_cash=CAP,
        fees=0.0,
        slippage=0.0,
        freq="1D",
    )
    return float(pf.value().iloc[-1])


def main():
    bars, result = btos_run()
    btos_final = result.final_equity
    ref_final, ref_fills = independent_ledger(bars)

    print(f"BTOS engine            final equity: {btos_final:,.4f}")
    print(f"Independent ledger     final equity: {ref_final:,.4f}")
    rel_ref = abs(btos_final - ref_final) / abs(ref_final)
    print(f"  relative difference vs independent ledger: {rel_ref:.2e}")
    assert (
        rel_ref < 1e-6
    ), "BTOS disagrees with an exact reimplementation of its own semantics"

    vbt_final = vectorbt_run(bars)
    if vbt_final is not None:
        rel_vbt = abs(btos_final - vbt_final) / abs(vbt_final)
        print(f"vectorbt (third party) final value:  {vbt_final:,.4f}")
        print(f"  relative difference vs vectorbt: {rel_vbt:.2e}")
        assert rel_vbt < 5e-3, "BTOS diverges materially from vectorbt"
    else:
        print("vectorbt not installed; skipped third-party cross-check")

    print("\nCROSS-VALIDATION PASSED")


if __name__ == "__main__":
    main()
