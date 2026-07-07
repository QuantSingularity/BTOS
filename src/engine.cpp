#include "btos/engine.hpp"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <unordered_map>

#include "btos/core/event_log.hpp"
#include "btos/core/hash.hpp"

using namespace std;

namespace btos {

struct Engine::Impl {
    InstrumentMaster instruments;
    bool have_instruments{false};
    std::vector<std::pair<std::string, std::shared_ptr<IBarReader>>> feeds;
    Calendar calendar{Calendar::always_open()};
    CorporateActionBook corp_actions;
    std::unique_ptr<IStrategy> strategy;
    std::map<std::string, double> params;
    std::unique_ptr<ILatencyModel> latency;
    std::unique_ptr<ISlippageModel> slippage;
    std::unique_ptr<ICommissionModel> commission;
    RiskLimits limits;
    PortfolioConfig pf_cfg;
    std::uint64_t seed{0};
    TimestampNs win_start{kNoTimestamp};
    TimestampNs win_end{kMaxTimestamp};
    std::string event_log_path;
};

namespace {

class EngineContext final : public StrategyContext {
  public:
    EngineContext(Kernel& kernel, IExchangeSim& exchange, Portfolio& portfolio,
                  const InstrumentMaster& instruments, StandardPreTradeCheck& risk,
                  std::unordered_map<std::uint32_t, PointInTimeBars>& bars)
        : kernel_(kernel),
          exchange_(exchange),
          portfolio_(portfolio),
          instruments_(instruments),
          risk_(risk),
          bars_(bars) {}

    [[nodiscard]] TimestampNs now() const override { return kernel_.now(); }

    OrderId submit_order(InstrumentId instrument, Side side, OrderType type, double qty,
                         double limit_price, double stop_price, TimeInForce tif) override {
        Order o;
        o.id = OrderId{next_order_++};
        o.instrument = instrument;
        o.side = side;
        o.type = type;
        o.tif = tif;
        o.quantity = qty;
        o.limit_price = limit_price;
        o.stop_price = stop_price;
        double ref = limit_price;
        auto bit = bars_.find(instrument.value);
        if (bit != bars_.end() && bit->second.size() > 0) ref = bit->second.latest().close;
        const RiskDecision d = risk_.evaluate(o, ref, portfolio_, kernel_.now());
        if (!d.accepted) {
            kernel_.schedule(kernel_.now(), EventPriority::OrderAck,
                             OrderAckEvent{o.id, false, "risk: " + d.reason});
            return o.id;
        }
        exchange_.submit(o);
        return o.id;
    }

    void cancel_order(OrderId id) override { exchange_.cancel(id); }

    void schedule_timer(TimestampNs when, std::uint64_t token) override {
        kernel_.schedule(when, EventPriority::Timer, TimerEvent{token});
    }

    [[nodiscard]] const PointInTimeBars& bars(InstrumentId id) const override {
        auto it = bars_.find(id.value);
        if (it == bars_.end()) throw std::out_of_range("engine: no bar feed for instrument");
        return it->second;
    }

    [[nodiscard]] const Portfolio& portfolio() const override { return portfolio_; }

    [[nodiscard]] InstrumentId instrument(const std::string& symbol) const override {
        auto id = instruments_.find(symbol);
        if (!id) throw std::out_of_range("engine: unknown symbol " + symbol);
        return *id;
    }

  private:
    Kernel& kernel_;
    IExchangeSim& exchange_;
    Portfolio& portfolio_;
    const InstrumentMaster& instruments_;
    StandardPreTradeCheck& risk_;
    std::unordered_map<std::uint32_t, PointInTimeBars>& bars_;
    std::uint64_t next_order_{1};
};

}

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

Engine& Engine::with_instruments(InstrumentMaster master) {
    impl_->instruments = std::move(master);
    impl_->have_instruments = true;
    return *this;
}
Engine& Engine::with_bars(const std::string& symbol, std::shared_ptr<IBarReader> reader) {
    impl_->feeds.emplace_back(symbol, std::move(reader));
    return *this;
}
Engine& Engine::with_calendar(Calendar cal) {
    impl_->calendar = std::move(cal);
    return *this;
}
Engine& Engine::with_corporate_actions(CorporateActionBook book) {
    impl_->corp_actions = std::move(book);
    return *this;
}
Engine& Engine::with_strategy(std::unique_ptr<IStrategy> strategy) {
    impl_->strategy = std::move(strategy);
    return *this;
}
Engine& Engine::with_params(std::map<std::string, double> params) {
    impl_->params = std::move(params);
    return *this;
}
Engine& Engine::with_latency(std::unique_ptr<ILatencyModel> latency) {
    impl_->latency = std::move(latency);
    return *this;
}
Engine& Engine::with_slippage(std::unique_ptr<ISlippageModel> slippage) {
    impl_->slippage = std::move(slippage);
    return *this;
}
Engine& Engine::with_commission(std::unique_ptr<ICommissionModel> commission) {
    impl_->commission = std::move(commission);
    return *this;
}
Engine& Engine::with_risk_limits(RiskLimits limits) {
    impl_->limits = limits;
    return *this;
}
Engine& Engine::with_portfolio(PortfolioConfig cfg) {
    impl_->pf_cfg = std::move(cfg);
    return *this;
}
Engine& Engine::with_seed(std::uint64_t seed) {
    impl_->seed = seed;
    return *this;
}
Engine& Engine::with_window(TimestampNs start, TimestampNs end) {
    impl_->win_start = start;
    impl_->win_end = end;
    return *this;
}
Engine& Engine::with_event_log(const std::string& path) {
    impl_->event_log_path = path;
    return *this;
}

RunResult Engine::run() {
    Impl& s = *impl_;
    if (!s.have_instruments) throw std::logic_error("engine: instruments are required");
    if (!s.strategy) throw std::logic_error("engine: a strategy is required");
    if (s.feeds.empty()) throw std::logic_error("engine: at least one bar feed is required");
    if (!s.latency) s.latency = std::make_unique<FixedLatency>(milliseconds(1));
    if (!s.slippage) s.slippage = std::make_unique<FixedBpsSlippage>(0.0);
    if (!s.commission) s.commission = std::make_unique<NoCommission>();

    Kernel kernel;
    std::string log_path = s.event_log_path;
    if (log_path.empty())
        log_path = (std::filesystem::temp_directory_path() /
                    ("btos_events_" + std::to_string(s.seed) + ".log"))
                       .string();
    EventLogWriter log(log_path);
    kernel.set_sink(&log);

    FxRates fx(s.pf_cfg.base_currency);
    Portfolio portfolio(s.pf_cfg, s.instruments, fx);
    StandardPreTradeCheck risk(s.limits);
    BarExchangeSim exchange(kernel, s.instruments, std::move(s.latency), std::move(s.slippage));

    std::unordered_map<std::uint32_t, PointInTimeBars> bars;
    std::unordered_map<std::uint32_t, const IBarReader*> readers;
    for (const auto& [symbol, reader] : s.feeds) {
        const auto id = s.instruments.find(symbol);
        if (!id) throw std::logic_error("engine: feed symbol not in instrument master: " + symbol);
        bars.emplace(id->value, PointInTimeBars{*reader});
        readers[id->value] = reader.get();
    }

    Fnv1a64 dataset_hash;
    TimestampNs first_ts = kMaxTimestamp;
    for (const auto& [iid, reader] : readers) {
        for (const auto& b : reader->bars()) {
            if (b.ts < s.win_start || b.ts > s.win_end) continue;
            if (b.ts < first_ts) first_ts = b.ts;
            dataset_hash.update_value(iid);
            dataset_hash.update_value(b.ts.ns);
            dataset_hash.update_value(b.close);
            BarEvent ev;
            ev.instrument = InstrumentId{iid};
            ev.open = b.open;
            ev.high = b.high;
            ev.low = b.low;
            ev.close = b.close;
            ev.volume = b.volume;
            ev.period = reader->period();
            kernel.schedule(b.ts, EventPriority::MarketData, ev);
        }
    }
    if (first_ts == kMaxTimestamp) throw std::logic_error("engine: no bars inside the window");
    for (const auto& a : s.corp_actions.all()) {
        if (a.effective < first_ts || a.effective > s.win_end) continue;
        CorporateActionEvent ev;
        ev.instrument = a.instrument;
        ev.ratio = a.ratio;
        ev.amount = a.amount;
        switch (a.kind) {
            case CorporateAction::Kind::Split:
                ev.kind = CorporateActionEvent::Kind::Split;
                break;
            case CorporateAction::Kind::CashDividend:
                ev.kind = CorporateActionEvent::Kind::CashDividend;
                break;
            case CorporateAction::Kind::Delisting:
                ev.kind = CorporateActionEvent::Kind::Delisting;
                break;
        }
        kernel.schedule(a.effective, EventPriority::CorporateAction, ev);
    }

    EngineContext ctx(kernel, exchange, portfolio, s.instruments, risk, bars);
    for (const auto& [k, v] : s.params) s.strategy->set_param(k, v);

    RunResult result;
    Fnv1a64 fill_hash;
    bool debug_accounting = false;
#ifndef NDEBUG
    debug_accounting = true;
#endif
#ifdef BTOS_FORCE_ACCOUNTING_CHECKS
    debug_accounting = true;
#endif

    kernel.subscribe([&](const Event& ev) {
        exchange.on_event(ev);
        if (const auto* bar = std::get_if<BarEvent>(&ev.payload)) {
            auto it = bars.find(bar->instrument.value);
            if (it != bars.end()) it->second.advance_to(ev.ts);
            portfolio.mark(bar->instrument, bar->close);
            s.strategy->on_bar(ctx, *bar);
            const double eq = portfolio.equity(ev.ts);
            risk.observe_equity(eq);
            result.equity_curve.emplace_back(ev.ts, eq);
            result.exposure_curve.emplace_back(
                ev.ts, eq > 0 ? portfolio.gross_exposure(ev.ts) / eq : 0.0);
        } else if (const auto* fill = std::get_if<FillEvent>(&ev.payload)) {
            FillEvent f = *fill;
            const Instrument& inst = s.instruments.get(f.instrument);
            f.commission = s.commission->commission(f.quantity, f.price, inst.multiplier);
            portfolio.apply_fill(f, ev.ts);
            if (debug_accounting) portfolio.check_identity(ev.ts);
            result.fills.push_back(f);
            fill_hash.update_value(f.fill.value);
            fill_hash.update_value(f.quantity);
            fill_hash.update_value(f.price);
            fill_hash.update_value(f.commission);
            s.strategy->on_fill(ctx, f);
        } else if (const auto* ca = std::get_if<CorporateActionEvent>(&ev.payload)) {
            portfolio.apply_corporate_action(*ca, ev.ts);
            if (debug_accounting && ca->kind == CorporateActionEvent::Kind::Split)
                portfolio.check_identity(ev.ts);
        } else if (const auto* timer = std::get_if<TimerEvent>(&ev.payload)) {
            s.strategy->on_timer(ctx, timer->token);
        }
    });

    s.strategy->on_start(ctx);
    result.events_dispatched = kernel.run(s.win_end);
    s.strategy->on_stop(ctx);
    log.close();

    result.event_log_hash = log.digest();
    result.fill_log_hash = fill_hash.digest();
    result.final_state = portfolio.serialize_state();
    result.final_equity =
        result.equity_curve.empty() ? s.pf_cfg.initial_capital : result.equity_curve.back().second;

    Fnv1a64 run_id;
    run_id.update_value(s.seed);
    run_id.update_value(dataset_hash.digest());
    run_id.update_value(result.event_log_hash);
    result.run_id = to_hex(run_id.digest());
    return result;
}

}
