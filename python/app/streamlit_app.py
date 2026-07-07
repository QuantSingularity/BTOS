import os
import sys

import pandas as pd
import streamlit as st

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import btos

st.set_page_config(page_title="BTOS", layout="wide")
st.title("BTOS: Backtesting Operating System")
st.caption(
    "A thin view over the deterministic C++ engine. All simulation runs in C++; "
    "this page only configures a run and displays results."
)

with st.sidebar:
    st.header("Data")
    data_source = st.selectbox("Source", ["synthetic_gbm", "synthetic_heston"])
    n_bars = st.number_input(
        "Bars", min_value=50, max_value=20000, value=2000, step=100
    )
    seed = st.number_input("Seed", min_value=0, max_value=2**31 - 1, value=42, step=1)
    s0 = st.number_input("Start price", min_value=1.0, value=100.0)
    mu = st.number_input("Drift (annual)", value=0.08, format="%.3f")
    sigma = st.number_input(
        "Volatility (annual)", min_value=0.0, value=0.20, format="%.3f"
    )

    st.header("Strategy")
    strategy = st.selectbox("Strategy", ["ma_crossover", "market_maker"])
    if strategy == "ma_crossover":
        fast = st.number_input("Fast window", min_value=2, value=5, step=1)
        slow = st.number_input("Slow window", min_value=3, value=20, step=1)
        quantity = st.number_input("Order quantity", min_value=1.0, value=100.0)
    else:
        spread_bps = st.number_input("Spread (bps)", min_value=1.0, value=20.0)
        quote_size = st.number_input("Quote size", min_value=1.0, value=50.0)
        max_inventory = st.number_input("Max inventory", min_value=1.0, value=500.0)

    st.header("Portfolio and frictions")
    initial_capital = st.number_input(
        "Initial capital", min_value=1000.0, value=1_000_000.0
    )
    commission_per_share = st.number_input(
        "Commission per share", min_value=0.0, value=0.005, format="%.4f"
    )
    run_clicked = st.button("Run backtest", type="primary")


def build_bars():
    if data_source == "synthetic_gbm":
        return btos.synthetic_gbm(
            s0=s0, mu=mu, sigma=sigma, n=int(n_bars), seed=int(seed)
        )
    return btos.synthetic_heston(s0=s0, mu=mu, n=int(n_bars), seed=int(seed))


def build_config(reader):
    cfg = {
        "strategy": strategy,
        "symbol": "SYN",
        "bars": {"SYN": reader},
        "initial_capital": float(initial_capital),
        "seed": int(seed),
    }
    if strategy == "ma_crossover":
        cfg["params"] = {
            "fast": int(fast),
            "slow": int(slow),
            "quantity": float(quantity),
        }
    else:
        cfg["params"] = {
            "spread_bps": float(spread_bps),
            "quote_size": float(quote_size),
            "max_inventory": float(max_inventory),
        }
    if commission_per_share > 0:
        cfg["commission"] = {
            "model": "per_share",
            "rate": float(commission_per_share),
            "min": 1.0,
        }
    return cfg


if run_clicked:
    reader = build_bars()
    result = btos.run_backtest(build_config(reader))
    metrics = result.metrics(periods_per_year=252)

    top = st.columns(4)
    top[0].metric("Final equity", f"{result.final_equity:,.0f}")
    top[1].metric("Total return", f"{metrics['total_return'] * 100:.2f}%")
    top[2].metric("Sharpe", f"{metrics['sharpe']:.2f}")
    top[3].metric("Max drawdown", f"{metrics['max_drawdown'] * 100:.2f}%")

    st.subheader("Equity curve")
    curve = pd.DataFrame({"equity": result.equity}, index=result.equity_ts)
    st.line_chart(curve)

    left, right = st.columns(2)
    with left:
        st.subheader("Metrics")
        keys = [
            "cagr",
            "volatility_annual",
            "sortino",
            "turnover_annual",
            "n_trades",
            "win_rate",
        ]
        st.dataframe(pd.DataFrame({"value": {k: metrics[k] for k in keys}}))
    with right:
        st.subheader("Run")
        st.write(
            {
                "run_id": result.run_id,
                "event_log_hash": f"{result.event_log_hash:#018x}",
                "fills": int(result.fills["quantity"].size),
                "events_dispatched": int(result.events_dispatched),
            }
        )
        st.caption(
            "Re-running with the same inputs reproduces this event log hash exactly."
        )
else:
    st.info("Configure a run in the sidebar and click Run backtest.")
