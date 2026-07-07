#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "btos/analytics/metrics.hpp"
#include "btos/data/btosd.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/data/readers.hpp"
#include "btos/data/synthetic.hpp"
#include "btos/engine.hpp"
#include "btos/portfolio/commission.hpp"
#include "btos/exec/latency.hpp"
#include "btos/exec/slippage.hpp"
#include "btos/strategy/reference.hpp"

using namespace std;
namespace py = pybind11;
using namespace btos;

namespace {

DurationNs period_from_seconds(double seconds_value) {
    return DurationNs{static_cast<int64_t>(seconds_value * 1e9)};
}

shared_ptr<IBarReader> load_bars(const string& path, double period_seconds) {
    if (path.size() > 6 && path.substr(path.size() - 6) == ".btosd")
        return make_shared<BtosdBarReader>(path);
    if (path.size() > 4 && path.substr(path.size() - 4) == ".csv")
        return make_shared<CsvBarReader>(path, period_from_seconds(period_seconds));
    throw runtime_error("load_bars: unsupported extension for " + path);
}

shared_ptr<IBarReader> synth_gbm(double s0, double mu, double sigma, const string& start,
                                 double period_seconds, size_t n, uint64_t seed) {
    mt19937_64 rng(seed);
    GbmParams p;
    p.s0 = s0;
    p.mu = mu;
    p.sigma = sigma;
    auto bars = generate_gbm_bars(rng, p, parse_iso8601_utc(start),
                                  period_from_seconds(period_seconds), n);
    return make_shared<MemoryBarReader>(move(bars), period_from_seconds(period_seconds));
}

shared_ptr<IBarReader> synth_heston(double s0, double mu, double v0, double kappa, double theta,
                                    double xi, double rho, const string& start,
                                    double period_seconds, size_t n, uint64_t seed) {
    mt19937_64 rng(seed);
    HestonParams p;
    p.s0 = s0;
    p.mu = mu;
    p.v0 = v0;
    p.kappa = kappa;
    p.theta = theta;
    p.xi = xi;
    p.rho = rho;
    auto bars = generate_heston_bars(rng, p, parse_iso8601_utc(start),
                                     period_from_seconds(period_seconds), n);
    return make_shared<MemoryBarReader>(move(bars), period_from_seconds(period_seconds));
}

InstrumentMaster single_instrument(const string& symbol, const string& currency) {
    InstrumentMaster m;
    Instrument inst;
    inst.id = InstrumentId{1};
    inst.symbol = symbol;
    inst.currency = currency;
    inst.listing = TimestampNs{0};
    m.add(inst);
    return m;
}

unique_ptr<IStrategy> make_strategy(const string& name, const vector<string>& symbols) {
    if (name == "ma_crossover") {
        if (symbols.empty()) throw runtime_error("ma_crossover needs one symbol");
        return make_unique<MaCrossoverStrategy>(symbols[0]);
    }
    if (name == "pairs") {
        if (symbols.size() < 2) throw runtime_error("pairs needs two symbols");
        return make_unique<PairsStrategy>(symbols[0], symbols[1]);
    }
    if (name == "market_maker") {
        if (symbols.empty()) throw runtime_error("market_maker needs one symbol");
        return make_unique<NaiveMarketMaker>(symbols[0]);
    }
    throw runtime_error("unknown strategy: " + name);
}

unique_ptr<ICommissionModel> make_commission(const py::dict& spec) {
    if (spec.empty()) return nullptr;
    const string model = spec.contains("model") ? spec["model"].cast<string>() : "";
    if (model == "per_share")
        return make_unique<PerShareCommission>(spec["rate"].cast<double>(),
                                               spec.contains("min") ? spec["min"].cast<double>()
                                                                    : 0.0);
    if (model == "bps") return make_unique<BpsCommission>(spec["bps"].cast<double>());
    if (model == "none" || model.empty()) return make_unique<NoCommission>();
    throw runtime_error("unknown commission model: " + model);
}

unique_ptr<ISlippageModel> make_slippage(const py::dict& spec) {
    if (spec.empty()) return nullptr;
    const string model = spec.contains("model") ? spec["model"].cast<string>() : "";
    if (model == "fixed_bps") return make_unique<FixedBpsSlippage>(spec["bps"].cast<double>());
    if (model == "sqrt_impact") return make_unique<SquareRootImpact>(spec["k"].cast<double>());
    throw runtime_error("unknown slippage model: " + model);
}

py::dict bars_to_arrays(shared_ptr<IBarReader> reader) {
    const auto& bars = reader->bars();
    vector<int64_t> ts;
    vector<double> open, high, low, close, volume;
    const size_t n = bars.size();
    ts.reserve(n);
    open.reserve(n);
    high.reserve(n);
    low.reserve(n);
    close.reserve(n);
    volume.reserve(n);
    for (const auto& b : bars) {
        ts.push_back(b.ts.ns);
        open.push_back(b.open);
        high.push_back(b.high);
        low.push_back(b.low);
        close.push_back(b.close);
        volume.push_back(b.volume);
    }
    py::dict out;
    out["ts_ns"] = ts;
    out["open"] = open;
    out["high"] = high;
    out["low"] = low;
    out["close"] = close;
    out["volume"] = volume;
    return out;
}

py::dict run_result_to_dict(const RunResult& r) {
    py::dict out;
    out["run_id"] = r.run_id;
    out["event_log_hash"] = r.event_log_hash;
    out["fill_log_hash"] = r.fill_log_hash;
    out["events_dispatched"] = r.events_dispatched;
    out["final_equity"] = r.final_equity;

    vector<int64_t> eq_ts;
    vector<double> eq_val;
    eq_ts.reserve(r.equity_curve.size());
    eq_val.reserve(r.equity_curve.size());
    for (const auto& [t, v] : r.equity_curve) {
        eq_ts.push_back(t.ns);
        eq_val.push_back(v);
    }
    out["equity_ts_ns"] = eq_ts;
    out["equity"] = eq_val;

    vector<int64_t> fill_ts;
    vector<uint64_t> fill_inst;
    vector<int> fill_side;
    vector<double> fill_qty;
    vector<double> fill_px;
    vector<double> fill_comm;
    const size_t nf = r.fills.size();
    fill_ts.reserve(nf);
    fill_inst.reserve(nf);
    fill_side.reserve(nf);
    fill_qty.reserve(nf);
    fill_px.reserve(nf);
    fill_comm.reserve(nf);
    for (const auto& f : r.fills) {
        fill_inst.push_back(f.instrument.value);
        fill_side.push_back(f.side == Side::Buy ? 1 : -1);
        fill_qty.push_back(f.quantity);
        fill_px.push_back(f.price);
        fill_comm.push_back(f.commission);
    }
    out["fill_instrument"] = fill_inst;
    out["fill_side"] = fill_side;
    out["fill_quantity"] = fill_qty;
    out["fill_price"] = fill_px;
    out["fill_commission"] = fill_comm;
    return out;
}

py::dict compute_metrics_py(const py::dict& result, double periods_per_year,
                            const vector<double>& benchmark_returns) {
    const auto eq_ts = result["equity_ts_ns"].cast<vector<int64_t>>();
    const auto eq_val = result["equity"].cast<vector<double>>();
    vector<pair<TimestampNs, double>> equity;
    equity.reserve(eq_ts.size());
    for (size_t i = 0; i < eq_ts.size(); ++i)
        equity.emplace_back(TimestampNs{eq_ts[i]}, eq_val[i]);

    const auto qty = result["fill_quantity"].cast<vector<double>>();
    const auto px = result["fill_price"].cast<vector<double>>();
    const auto side = result["fill_side"].cast<vector<int>>();
    const auto inst = result["fill_instrument"].cast<vector<uint64_t>>();
    const auto comm = result["fill_commission"].cast<vector<double>>();
    vector<FillEvent> fills;
    fills.reserve(qty.size());
    for (size_t i = 0; i < qty.size(); ++i) {
        FillEvent f;
        f.instrument = InstrumentId{static_cast<uint32_t>(inst[i])};
        f.side = side[i] > 0 ? Side::Buy : Side::Sell;
        f.quantity = qty[i];
        f.price = px[i];
        f.commission = comm[i];
        fills.push_back(f);
    }

    MetricsOptions opts;
    opts.periods_per_year = periods_per_year;
    const Metrics m = compute_metrics(equity, fills, {}, benchmark_returns, opts);

    py::dict out;
    out["total_return"] = m.total_return;
    out["cagr"] = m.cagr;
    out["volatility_annual"] = m.volatility_annual;
    out["sharpe"] = m.sharpe;
    out["sortino"] = m.sortino;
    out["max_drawdown"] = m.max_drawdown;
    out["turnover_annual"] = m.turnover_annual;
    out["exposure_mean"] = m.exposure_mean;
    out["n_trades"] = m.n_trades;
    out["win_rate"] = m.win_rate;
    out["alpha_annual"] = m.alpha_annual;
    out["beta"] = m.beta;
    out["information_ratio"] = m.information_ratio;
    return out;
}

py::dict run_backtest(const py::dict& config) {
    Engine eng;

    const string strat_name = config["strategy"].cast<string>();
    vector<string> symbols;
    if (config.contains("symbols"))
        symbols = config["symbols"].cast<vector<string>>();
    else if (config.contains("symbol"))
        symbols.push_back(config["symbol"].cast<string>());

    string currency = config.contains("currency") ? config["currency"].cast<string>() : "USD";
    if (symbols.size() == 1) {
        eng.with_instruments(single_instrument(symbols[0], currency));
    } else {
        InstrumentMaster m;
        uint32_t next = 1;
        for (const auto& s : symbols) {
            Instrument inst;
            inst.id = InstrumentId{next++};
            inst.symbol = s;
            inst.currency = currency;
            inst.listing = TimestampNs{0};
            m.add(inst);
        }
        eng.with_instruments(move(m));
    }

    const auto feeds = config["bars"].cast<py::dict>();
    for (auto item : feeds) {
        const string symbol = item.first.cast<string>();
        auto reader = item.second.cast<shared_ptr<IBarReader>>();
        eng.with_bars(symbol, reader);
    }

    eng.with_strategy(make_strategy(strat_name, symbols));

    if (config.contains("params"))
        eng.with_params(config["params"].cast<map<string, double>>());
    if (config.contains("commission"))
        if (auto c = make_commission(config["commission"].cast<py::dict>())) eng.with_commission(move(c));
    if (config.contains("slippage"))
        if (auto s = make_slippage(config["slippage"].cast<py::dict>())) eng.with_slippage(move(s));
    if (config.contains("latency_ms"))
        eng.with_latency(make_unique<FixedLatency>(
            DurationNs{static_cast<int64_t>(config["latency_ms"].cast<double>() * 1e6)}));
    if (config.contains("initial_capital")) {
        PortfolioConfig pc;
        pc.initial_capital = config["initial_capital"].cast<double>();
        pc.base_currency = currency;
        eng.with_portfolio(pc);
    }
    if (config.contains("risk")) {
        const auto rd = config["risk"].cast<py::dict>();
        RiskLimits limits;
        if (rd.contains("max_gross_leverage"))
            limits.max_gross_leverage = rd["max_gross_leverage"].cast<double>();
        if (rd.contains("max_position_qty"))
            limits.max_position_qty = rd["max_position_qty"].cast<double>();
        if (rd.contains("max_drawdown_kill"))
            limits.max_drawdown_kill = rd["max_drawdown_kill"].cast<double>();
        eng.with_risk_limits(limits);
    }

    const uint64_t seed = config.contains("seed") ? config["seed"].cast<uint64_t>() : 0;
    eng.with_seed(seed);

    return run_result_to_dict(eng.run());
}

}

PYBIND11_MODULE(_btos, m) {
    m.doc() = "BTOS: Backtesting Operating System. C++ engine driven from Python at run granularity.";

    py::class_<IBarReader, shared_ptr<IBarReader>>(m, "BarReader");

    m.def("load_bars", &load_bars, py::arg("path"), py::arg("period_seconds") = 86400.0,
          "Load bars from a .btosd or .csv file into a bar reader.");
    m.def("bars_to_arrays", &bars_to_arrays, py::arg("reader"),
          "Return a reader's OHLCV as a dict of arrays (ts_ns, open, high, low, close, volume).");
    m.def("synthetic_gbm", &synth_gbm, py::arg("s0") = 100.0, py::arg("mu") = 0.05,
          py::arg("sigma") = 0.2, py::arg("start") = "2020-01-01",
          py::arg("period_seconds") = 86400.0, py::arg("n") = 1000, py::arg("seed") = 42,
          "Generate a seeded geometric Brownian motion bar series.");
    m.def("synthetic_heston", &synth_heston, py::arg("s0") = 100.0, py::arg("mu") = 0.05,
          py::arg("v0") = 0.04, py::arg("kappa") = 1.5, py::arg("theta") = 0.04,
          py::arg("xi") = 0.5, py::arg("rho") = -0.7, py::arg("start") = "2020-01-01",
          py::arg("period_seconds") = 86400.0, py::arg("n") = 1000, py::arg("seed") = 42,
          "Generate a seeded Heston stochastic-volatility bar series.");
    m.def("run_backtest", &run_backtest, py::arg("config"),
          "Run a full backtest from a config dict. Returns a dict of result arrays.");
    m.def("compute_metrics", &compute_metrics_py, py::arg("result"),
          py::arg("periods_per_year") = 252.0,
          py::arg("benchmark_returns") = vector<double>{},
          "Compute performance metrics from a backtest result dict.");
}
