from __future__ import annotations

from typing import Any, Optional, Sequence

import numpy as np

from . import _btos

__all__ = [
    "load_bars",
    "synthetic_gbm",
    "synthetic_heston",
    "load_bars",
    "bars_to_arrays",
    "run_backtest",
    "BacktestResult",
]


def load_bars(path: str, period_seconds: float = 86400.0):
    return _btos.load_bars(path, period_seconds)


def bars_to_arrays(reader) -> dict:
    import numpy as np

    raw = _btos.bars_to_arrays(reader)
    return {
        "ts": np.asarray(raw["ts_ns"], dtype="datetime64[ns]"),
        "open": np.asarray(raw["open"], dtype=float),
        "high": np.asarray(raw["high"], dtype=float),
        "low": np.asarray(raw["low"], dtype=float),
        "close": np.asarray(raw["close"], dtype=float),
        "volume": np.asarray(raw["volume"], dtype=float),
    }


def synthetic_gbm(
    s0: float = 100.0,
    mu: float = 0.05,
    sigma: float = 0.2,
    start: str = "2020-01-01",
    period_seconds: float = 86400.0,
    n: int = 1000,
    seed: int = 42,
):
    return _btos.synthetic_gbm(s0, mu, sigma, start, period_seconds, n, seed)


def synthetic_heston(
    s0: float = 100.0,
    mu: float = 0.05,
    v0: float = 0.04,
    kappa: float = 1.5,
    theta: float = 0.04,
    xi: float = 0.5,
    rho: float = -0.7,
    start: str = "2020-01-01",
    period_seconds: float = 86400.0,
    n: int = 1000,
    seed: int = 42,
):
    return _btos.synthetic_heston(
        s0, mu, v0, kappa, theta, xi, rho, start, period_seconds, n, seed
    )


class BacktestResult:
    def __init__(self, raw: dict):
        self._raw = raw
        self.run_id: str = raw["run_id"]
        self.event_log_hash: int = raw["event_log_hash"]
        self.fill_log_hash: int = raw["fill_log_hash"]
        self.events_dispatched: int = raw["events_dispatched"]
        self.final_equity: float = raw["final_equity"]

    @property
    def equity_ts(self) -> np.ndarray:
        return np.asarray(self._raw["equity_ts_ns"], dtype="datetime64[ns]")

    @property
    def equity(self) -> np.ndarray:
        return np.asarray(self._raw["equity"], dtype=float)

    @property
    def fills(self):
        return {
            "instrument": np.asarray(self._raw["fill_instrument"], dtype=np.uint64),
            "side": np.asarray(self._raw["fill_side"], dtype=int),
            "quantity": np.asarray(self._raw["fill_quantity"], dtype=float),
            "price": np.asarray(self._raw["fill_price"], dtype=float),
            "commission": np.asarray(self._raw["fill_commission"], dtype=float),
        }

    def returns(self) -> np.ndarray:
        eq = self.equity
        if eq.size < 2:
            return np.empty(0, dtype=float)
        return np.diff(eq) / eq[:-1]

    def metrics(
        self,
        periods_per_year: float = 252.0,
        benchmark_returns: Optional[Sequence[float]] = None,
    ) -> dict:
        bench = list(benchmark_returns) if benchmark_returns is not None else []
        return _btos.compute_metrics(self._raw, periods_per_year, bench)

    def to_frame(self):
        import pandas as pd

        return pd.DataFrame({"equity": self.equity}, index=self.equity_ts)

    def __repr__(self) -> str:
        return (
            f"BacktestResult(run_id={self.run_id!r}, "
            f"final_equity={self.final_equity:.2f}, "
            f"fills={len(self._raw['fill_quantity'])}, "
            f"event_log_hash={self.event_log_hash:#018x})"
        )


def run_backtest(config: dict[str, Any]) -> BacktestResult:
    return BacktestResult(_btos.run_backtest(config))
