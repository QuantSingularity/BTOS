import btos


def main():
    config = {
        "strategy": "ma_crossover",
        "symbol": "SYN",
        "bars": {
            "SYN": btos.synthetic_gbm(s0=100.0, mu=0.08, sigma=0.2, n=2000, seed=42)
        },
        "params": {"fast": 5, "slow": 20, "quantity": 100},
        "commission": {"model": "per_share", "rate": 0.005, "min": 1.0},
        "initial_capital": 1_000_000.0,
        "seed": 42,
    }

    result = btos.run_backtest(config)

    print(result)
    print(f"run_id           {result.run_id}")
    print(f"event log hash   {result.event_log_hash:#018x}")
    print(f"final equity     {result.final_equity:,.2f}")
    print(f"fills            {result.fills['quantity'].size}")

    metrics = result.metrics(periods_per_year=252)
    print("\nmetrics")
    for key in (
        "total_return",
        "cagr",
        "sharpe",
        "sortino",
        "max_drawdown",
        "n_trades",
        "win_rate",
    ):
        print(f"  {key:<16} {metrics[key]}")

    config["bars"] = {
        "SYN": btos.synthetic_gbm(s0=100.0, mu=0.08, sigma=0.2, n=2000, seed=42)
    }
    replay = btos.run_backtest(config)
    print(
        f"\ndeterministic replay hash matches: "
        f"{result.event_log_hash == replay.event_log_hash}"
    )


if __name__ == "__main__":
    main()
