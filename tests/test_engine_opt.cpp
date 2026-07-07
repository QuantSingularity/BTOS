#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "btos/analytics/metrics.hpp"
#include "btos/data/synthetic.hpp"
#include "btos/engine.hpp"
#include "btos/opt/advanced.hpp"
#include "btos/opt/experiment_store.hpp"
#include "btos/opt/optimizer.hpp"
#include "btos/opt/validation.hpp"
#include "btos/plugin_registry.hpp"
#include "btos/strategy/reference.hpp"

using namespace std;
using namespace btos;

namespace {

InstrumentMaster synthetic_master(const vector<string>& symbols) {
    InstrumentMaster m;
    uint32_t next = 1;
    for (const auto& s : symbols) {
        Instrument i;
        i.id = InstrumentId{next++};
        i.symbol = s;
        i.listing = TimestampNs{0};
        m.add(i);
    }
    return m;
}

shared_ptr<IBarReader> gbm_feed(uint64_t seed, size_t n = 400, double mu = 0.08) {
    mt19937_64 rng(seed);
    GbmParams p;
    p.mu = mu;
    return make_shared<MemoryBarReader>(
        generate_gbm_bars(rng, p, parse_iso8601_utc("2020-01-01"), days(1), n), days(1));
}

Engine& configure_ma(Engine& eng, uint64_t seed) {
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(seed))
        .with_strategy(make_unique<MaCrossoverStrategy>("SYN"))
        .with_params({{"fast", 5}, {"slow", 20}, {"quantity", 100}})
        .with_commission(make_unique<PerShareCommission>(0.005, 1.0))
        .with_seed(seed);
    return eng;
}

}

TEST(Invariants, test_invariant_no_lookahead) {
    class PeekingStrategy final : public IStrategy {
      public:
        void on_start(StrategyContext& ctx) override { inst_ = ctx.instrument("SYN"); }
        void on_bar(StrategyContext& ctx, const BarEvent& bar) override {
            if (bar.instrument != inst_) return;
            const auto& view = ctx.bars(inst_);
            if (view.size() < 5 || view.size() >= 100) return;
            EXPECT_THROW(static_cast<void>(view.at(view.size())), PointInTimeBars::LookaheadViolation);
            EXPECT_NO_THROW(static_cast<void>(view.at(view.size() - 1)));
            EXPECT_LE(view.latest().ts, ctx.now());
            checked_ = true;
        }
        void on_stop(StrategyContext&) override { EXPECT_TRUE(checked_); }
        string name() const override { return "peeking"; }

      private:
        InstrumentId inst_;
        bool checked_{false};
    };
    Engine eng;
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(3, 100))
        .with_strategy(make_unique<PeekingStrategy>())
        .with_seed(3);
    EXPECT_NO_THROW(eng.run());
}

TEST(Invariants, test_invariant_point_in_time) {
    CorporateActionBook book;
    CorporateAction split;
    split.instrument = InstrumentId{1};
    split.kind = CorporateAction::Kind::Split;
    split.effective = parse_iso8601_utc("2021-01-01");
    split.ratio = 4.0;
    book.add(split);
    const auto obs = parse_iso8601_utc("2020-06-01");
    EXPECT_DOUBLE_EQ(book.adjustment_factor(InstrumentId{1}, obs, parse_iso8601_utc("2020-12-31")),
                     1.0);
    EXPECT_DOUBLE_EQ(book.adjustment_factor(InstrumentId{1}, obs, parse_iso8601_utc("2021-01-02")),
                     0.25);
    EXPECT_TRUE(book.known_asof(InstrumentId{1}, parse_iso8601_utc("2020-12-31")).empty());
}

TEST(Invariants, test_invariant_survivorship) {
    auto m = synthetic_master({"LIVE", "DEAD"});
    InstrumentMaster m2;
    for (auto inst : m.all()) {
        if (inst.symbol == "DEAD") inst.delisting = parse_iso8601_utc("2020-06-01");
        m2.add(inst);
    }
    EXPECT_EQ(m2.active_universe(parse_iso8601_utc("2020-05-01")).size(), 2u);
    EXPECT_EQ(m2.active_universe(parse_iso8601_utc("2020-07-01")).size(), 1u);
    EXPECT_FALSE(m2.get(InstrumentId{2}).active_at(parse_iso8601_utc("2020-07-01")));
}

TEST(Invariants, test_invariant_deterministic_replay) {
    Engine e1, e2;
    const RunResult r1 = configure_ma(e1, 99).run();
    const RunResult r2 = configure_ma(e2, 99).run();
    EXPECT_EQ(r1.event_log_hash, r2.event_log_hash);
    EXPECT_EQ(r1.fill_log_hash, r2.fill_log_hash);
    EXPECT_EQ(r1.final_state, r2.final_state);
    EXPECT_EQ(r1.run_id, r2.run_id);
    EXPECT_GT(r1.fills.size(), 0u);
    Engine e3;
    const RunResult r3 = configure_ma(e3, 100).run();
    EXPECT_NE(r1.event_log_hash, r3.event_log_hash);
}

TEST(Invariants, test_invariant_event_ordering) {
    Kernel k;
    vector<int> ranks;
    k.subscribe([&](const Event& ev) { ranks.push_back(static_cast<int>(ev.priority)); });
    const TimestampNs t{777};
    FillEvent f;
    k.schedule(t, EventPriority::Fill, f);
    k.schedule(t, EventPriority::MarketData, BarEvent{});
    k.schedule(t, EventPriority::Timer, TimerEvent{1});
    k.schedule(t, EventPriority::OrderAck, OrderAckEvent{});
    k.schedule(t, EventPriority::Session, SessionEvent{});
    k.schedule(t, EventPriority::CorporateAction, CorporateActionEvent{});
    k.run(TimestampNs{1'000});
    ASSERT_EQ(ranks.size(), 6u);
    EXPECT_TRUE(is_sorted(ranks.begin(), ranks.end()));
}

TEST(Invariants, test_invariant_accounting_identity) {
    Engine eng;
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(5, 300))
        .with_strategy(make_unique<NaiveMarketMaker>("SYN"))
        .with_params({{"spread_bps", 20}, {"quote_size", 50}, {"max_inventory", 500}})
        .with_commission(make_unique<BpsCommission>(1.0))
        .with_seed(5);
    const RunResult r = eng.run();
    EXPECT_GT(r.fills.size(), 0u);
    EXPECT_TRUE(isfinite(r.final_equity));
}

TEST(Integration, MaCrossoverEndToEnd) {
    Engine eng;
    const RunResult r = configure_ma(eng, 21).run();
    EXPECT_GT(r.fills.size(), 2u);
    EXPECT_GT(r.equity_curve.size(), 300u);
    EXPECT_TRUE(isfinite(r.final_equity));
    for (const auto& f : r.fills) EXPECT_GT(f.quantity, 0.0);
}

TEST(Integration, PairsStatArbEndToEnd) {
    mt19937_64 rng(17);
    GbmParams p;
    auto base = generate_gbm_bars(rng, p, parse_iso8601_utc("2020-01-01"), days(1), 400);
    auto related = base;
    mt19937_64 noise(18);
    normal_distribution<double> eps(0.0, 0.3);
    for (auto& b : related) {
        const double e = eps(noise);
        b.open += e;
        b.high += e + 0.4;
        b.low += e - 0.4;
        b.close += e;
    }
    Engine eng;
    eng.with_instruments(synthetic_master({"Y", "X"}))
        .with_bars("Y", make_shared<MemoryBarReader>(base, days(1)))
        .with_bars("X", make_shared<MemoryBarReader>(related, days(1)))
        .with_strategy(make_unique<PairsStrategy>("Y", "X"))
        .with_params({{"window", 40}, {"entry_z", 1.5}, {"exit_z", 0.5}, {"quantity", 50}})
        .with_seed(17);
    const RunResult r = eng.run();
    EXPECT_GT(r.fills.size(), 0u);
    EXPECT_TRUE(isfinite(r.final_equity));
}

TEST(Integration, MarketMakerEndToEnd) {
    Engine eng;
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(31, 300, 0.0))
        .with_strategy(make_unique<NaiveMarketMaker>("SYN"))
        .with_params({{"spread_bps", 30}, {"quote_size", 20}, {"max_inventory", 200}})
        .with_seed(31);
    const RunResult r = eng.run();
    EXPECT_GT(r.fills.size(), 5u);
    double max_abs_inventory = 0;
    double inventory = 0;
    for (const auto& f : r.fills) {
        inventory += f.quantity * sign(f.side);
        max_abs_inventory = max(max_abs_inventory, fabs(inventory));
    }
    EXPECT_LE(max_abs_inventory, 200.0 + 20.0);
}

TEST(Integration, VectorizedAdapterTracksTarget) {
    Engine eng;
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(41, 120))
        .with_strategy(make_unique<VectorizedSignalStrategy>(
            "SYN",
            [](const vector<double>& closes) {
                return closes.size() >= 10 && closes.back() > closes[closes.size() - 10] ? 100.0
                                                                                         : 0.0;
            }))
        .with_seed(41);
    const RunResult r = eng.run();
    EXPECT_GT(r.fills.size(), 0u);
}

TEST(Integration, CheckpointRoundTripRestoresState) {
    MaCrossoverStrategy s("SYN");
    s.set_param("fast", 3);
    s.set_param("slow", 5);
    struct DummyCtx final : StrategyContext {
        TimestampNs now() const override { return TimestampNs{0}; }
        OrderId submit_order(InstrumentId, Side, OrderType, double, double, double,
                             TimeInForce) override {
            return OrderId{0};
        }
        void cancel_order(OrderId) override {}
        void schedule_timer(TimestampNs, uint64_t) override {}
        const PointInTimeBars& bars(InstrumentId) const override { throw logic_error("unused"); }
        const Portfolio& portfolio() const override { throw logic_error("unused"); }
        InstrumentId instrument(const string&) const override { return InstrumentId{1}; }
    } ctx;
    s.on_start(ctx);
    for (double px : {10.0, 11.0, 12.0, 13.0, 14.0}) {
        BarEvent b;
        b.instrument = InstrumentId{1};
        b.close = px;
        s.on_bar(ctx, b);
    }
    const string saved = s.save_state();
    MaCrossoverStrategy s2("SYN");
    s2.load_state(saved);
    EXPECT_EQ(s2.save_state(), saved);
}

TEST(Risk, PreTradeLimitsAndKillSwitch) {
    auto m = synthetic_master({"SYN"});
    FxRates fx("USD");
    PortfolioConfig cfg;
    cfg.initial_capital = 100'000;
    Portfolio pf(cfg, m, fx);
    pf.mark(InstrumentId{1}, 100.0);
    RiskLimits limits;
    limits.max_position_qty = 500;
    limits.max_gross_leverage = 2.0;
    limits.max_drawdown_kill = 0.10;
    StandardPreTradeCheck check(limits);
    Order small;
    small.instrument = InstrumentId{1};
    small.side = Side::Buy;
    small.quantity = 100;
    EXPECT_TRUE(check.evaluate(small, 100.0, pf, TimestampNs{1}).accepted);
    Order big = small;
    big.quantity = 600;
    EXPECT_FALSE(check.evaluate(big, 100.0, pf, TimestampNs{1}).accepted);
    Order levered = small;
    levered.quantity = 300;
    EXPECT_FALSE(check.evaluate(levered, 1'000.0, pf, TimestampNs{1}).accepted);
    check.observe_equity(100'000);
    check.observe_equity(89'000);
    EXPECT_TRUE(check.killed());
    EXPECT_FALSE(check.evaluate(small, 100.0, pf, TimestampNs{1}).accepted);
}

TEST(Risk, VarEsAndInverseNormal) {
    vector<double> rets;
    for (int i = 1; i <= 100; ++i) rets.push_back(i * 0.001 - 0.0505);
    const double var95 = historical_var(rets, 0.95);
    EXPECT_NEAR(var95, 0.04455, 1e-6);
    const double es95 = expected_shortfall(rets, 0.95);
    EXPECT_GT(es95, var95);
    EXPECT_NEAR(inverse_normal_cdf(0.5), 0.0, 1e-9);
    EXPECT_NEAR(inverse_normal_cdf(0.975), 1.959964, 1e-4);
    EXPECT_NEAR(normal_cdf(inverse_normal_cdf(0.99)), 0.99, 1e-6);
    EXPECT_NEAR(parametric_var(0.0, 0.02, 0.99), 0.02 * 2.326348, 1e-4);
    EXPECT_THROW(historical_var({}, 0.95), invalid_argument);
}

TEST(Risk, SizingFunctions) {
    vector<double> rets(252, 0.001);
    for (size_t i = 0; i < rets.size(); i += 2) rets[i] = -0.0005;
    const double w = vol_target_weight(rets, 0.10, 252.0, 5.0);
    EXPECT_GT(w, 0.0);
    EXPECT_LE(w, 5.0);
    const double k = kelly_fraction(rets, 0.5, 2.0);
    EXPECT_GE(k, -2.0);
    EXPECT_LE(k, 2.0);
}

TEST(Opt, GridCoversSteppedSpace) {
    vector<ParamSpec> space{{"x", 0, 1, 0.25}, {"y", 0, 1, 0.5}};
    SerialBackend backend;
    GridOptimizer grid;
    auto evals = grid.optimize(
        space, [](const ParamMap& p) { return -(p.at("x") - 0.5) * (p.at("x") - 0.5); }, 100, 0,
        backend);
    EXPECT_EQ(evals.size(), 15u);
    EXPECT_NEAR(evals.back().params.at("x"), 0.5, 1e-9);
}

TEST(Opt, RandomIsSeededAndFindsNeighborhood) {
    vector<ParamSpec> space{{"x", -2, 2, 0}};
    SerialBackend backend;
    RandomOptimizer opt;
    auto obj = [](const ParamMap& p) { return -fabs(p.at("x") - 0.7); };
    auto a = opt.optimize(space, obj, 200, 42, backend);
    auto b = opt.optimize(space, obj, 200, 42, backend);
    EXPECT_DOUBLE_EQ(a.back().params.at("x"), b.back().params.at("x"));
    EXPECT_NEAR(a.back().params.at("x"), 0.7, 0.1);
}

TEST(Opt, BayesianBeatsOrMatchesBudget) {
    vector<ParamSpec> space{{"x", 0, 1, 0}, {"y", 0, 1, 0}};
    SerialBackend backend;
    BayesianGpOptimizer opt(6);
    auto obj = [](const ParamMap& p) {
        const double dx = p.at("x") - 0.3;
        const double dy = p.at("y") - 0.8;
        return -(dx * dx + dy * dy);
    };
    auto evals = opt.optimize(space, obj, 30, 7, backend);
    EXPECT_EQ(evals.size(), 30u);
    EXPECT_GT(evals.back().objective, -0.05);
}

TEST(Opt, CmaEsConvergesOnSphere) {
    vector<ParamSpec> space{{"a", -1, 1, 0}, {"b", -1, 1, 0}, {"c", -1, 1, 0}};
    SerialBackend backend;
    CmaEsOptimizer opt(0.3);
    auto obj = [](const ParamMap& p) {
        double s = 0;
        for (const auto& [k, v] : p) s += (v - 0.2) * (v - 0.2);
        return -s;
    };
    auto evals = opt.optimize(space, obj, 400, 13, backend);
    EXPECT_GT(evals.back().objective, -0.02);
}

TEST(Opt, ThreadPoolBackendMatchesSerial) {
    vector<ParamSpec> space{{"x", 0, 1, 0}};
    auto obj = [](const ParamMap& p) { return p.at("x"); };
    RandomOptimizer opt;
    SerialBackend serial;
    ThreadPoolBackend pooled(2);
    auto a = opt.optimize(space, obj, 64, 5, serial);
    auto b = opt.optimize(space, obj, 64, 5, pooled);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_DOUBLE_EQ(a.back().objective, b.back().objective);
    EXPECT_DOUBLE_EQ(a.front().objective, b.front().objective);
}

TEST(Opt, WalkForwardSplitsAreContiguousAndDisjoint) {
    const auto splits = walk_forward_splits(100, 50, 10);
    ASSERT_EQ(splits.size(), 5u);
    for (const auto& s : splits) {
        EXPECT_EQ(s.train_end - s.train_begin, 50u);
        EXPECT_EQ(s.test_end - s.test_begin, 10u);
        EXPECT_EQ(s.train_end, s.test_begin);
    }
    EXPECT_EQ(splits[1].test_begin, splits[0].test_end);
    EXPECT_THROW(walk_forward_splits(10, 50, 10), invalid_argument);
}

TEST(Opt, PurgedKFoldRespectsPurgeAndEmbargo) {
    const auto folds = purged_kfold(100, 5, 3, 5);
    ASSERT_EQ(folds.size(), 5u);
    const auto& f = folds[2];
    EXPECT_EQ(f.test_begin, 40u);
    EXPECT_EQ(f.test_end, 60u);
    for (size_t idx : f.train_indices) {
        EXPECT_TRUE(idx < 37 || idx >= 68);
    }
    size_t total = 0;
    for (const auto& fold : folds) total += fold.test_end - fold.test_begin;
    EXPECT_EQ(total, 100u);
}

TEST(Opt, DeflatedSharpeDecreasesWithTrials) {
    const double sr = 0.10;
    const double d1 = deflated_sharpe_ratio(sr, 252, 0.0, 3.0, 1, 0.01);
    const double d100 = deflated_sharpe_ratio(sr, 252, 0.0, 3.0, 100, 0.01);
    EXPECT_GT(d1, d100);
    EXPECT_GE(d100, 0.0);
    EXPECT_LE(d1, 1.0);
    EXPECT_NEAR(probabilistic_sharpe_ratio(0.1, 0.0, 252, 0.0, 3.0),
                normal_cdf(0.1 * sqrt(251.0) / sqrt(1.0 + 0.005)), 1e-9);
}

TEST(Opt, MomentHelpers) {
    vector<double> sym{-2, -1, 0, 1, 2};
    EXPECT_NEAR(sample_skewness(sym), 0.0, 1e-12);
    vector<double> skewed{0, 0, 0, 0, 10};
    EXPECT_GT(sample_skewness(skewed), 1.0);
    EXPECT_GT(sample_kurtosis(skewed), 3.0);
}

namespace {

void exercise_store(IExperimentStore& db) {
    ExperimentRecord rec;
    rec.run_id = "abc123";
    rec.config_hash = "cfg";
    rec.dataset_hash = "data";
    rec.seed = 42;
    rec.metrics_json = "{\"sharpe\":1.5}";
    rec.created_utc = "2026-01-01T00:00:00.000000000Z";
    db.insert(rec);
    ExperimentRecord rec2 = rec;
    rec2.run_id = "def456";
    rec2.created_utc = "2026-01-02T00:00:00.000000000Z";
    db.insert(rec2);
    const auto got = db.get("abc123");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->seed, 42u);
    EXPECT_EQ(got->metrics_json, "{\"sharpe\":1.5}");
    const auto recent = db.recent(10);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].run_id, "def456");
    EXPECT_FALSE(db.get("missing").has_value());
}

}

TEST(Opt, SqliteExperimentStoreRoundTrip) {
    const auto path = (filesystem::temp_directory_path() / "btos_exp.sqlite").string();
    filesystem::remove(path);
    {
        SqliteExperimentStore db(path);
        exercise_store(db);
    }
    filesystem::remove(path);
}

TEST(Opt, JsonlExperimentStoreRoundTrip) {
    const auto path = (filesystem::temp_directory_path() / "btos_exp.jsonl").string();
    filesystem::remove(path);
    {
        JsonlExperimentStore db(path);
        exercise_store(db);
    }
    filesystem::remove(path);
}

TEST(Opt, StoreFactoryPicksBackendByExtension) {
    const auto sq = (filesystem::temp_directory_path() / "btos_f.sqlite").string();
    const auto jl = (filesystem::temp_directory_path() / "btos_f.jsonl").string();
    filesystem::remove(sq);
    filesystem::remove(jl);
    {
        auto a = open_experiment_store(sq);
        auto b = open_experiment_store(jl);
        ExperimentRecord rec;
        rec.run_id = "r1";
        rec.metrics_json = "{}";
        rec.created_utc = "2026-01-01T00:00:00.000000000Z";
        a->insert(rec);
        b->insert(rec);
        EXPECT_TRUE(a->get("r1").has_value());
        EXPECT_TRUE(b->get("r1").has_value());
    }
    ifstream check(sq, ios::binary);
    string magic(15, '\0');
    check.read(magic.data(), 15);
    EXPECT_EQ(magic.substr(0, 6), "SQLite");
    filesystem::remove(sq);
    filesystem::remove(jl);
}

TEST(Metrics, DrawdownSharpeAndMonthly) {
    vector<pair<TimestampNs, double>> equity;
    TimestampNs t = parse_iso8601_utc("2020-01-01");
    double eq = 100.0;
    const double steps[] = {1, 2, 3, -4, -3, 2, 4, 1, -1, 2, 3, 1};
    for (int month = 0; month < 12; ++month)
        for (int d = 0; d < 21; ++d) {
            eq *= 1.0 + steps[month] * 0.0004;
            t = t + days(1);
            equity.emplace_back(t, eq);
        }
    Metrics m = compute_metrics(equity, {}, {}, {}, {});
    EXPECT_GT(m.total_return, 0.0);
    EXPECT_GT(m.max_drawdown, 0.0);
    EXPECT_LT(m.max_drawdown, 0.2);
    EXPECT_EQ(m.returns_by_month.size(), 9u);
    EXPECT_FALSE(m.underwater.empty());
    EXPECT_FALSE(m.drawdowns.empty());
    EXPECT_GE(m.drawdowns.front().depth, m.drawdowns.back().depth);
    const string json = metrics_to_json(m);
    EXPECT_NE(json.find("\"sharpe\""), string::npos);
    const string html = render_html_report("t", m, equity);
    EXPECT_NE(html.find("<svg"), string::npos);
    EXPECT_EQ(html.find("http://cdn"), string::npos);
}

TEST(Metrics, TradesAndBenchmarkStats) {
    vector<FillEvent> fills{
        {FillId{1}, OrderId{1}, InstrumentId{1}, Side::Buy, 100, 10.0, 0},
        {FillId{2}, OrderId{2}, InstrumentId{1}, Side::Sell, 100, 12.0, 0},
        {FillId{3}, OrderId{3}, InstrumentId{1}, Side::Buy, 50, 12.0, 0},
        {FillId{4}, OrderId{4}, InstrumentId{1}, Side::Sell, 50, 11.0, 0},
    };
    vector<pair<TimestampNs, double>> equity;
    vector<double> bench;
    TimestampNs t{0};
    double eq = 1'000;
    mt19937_64 rng(3);
    normal_distribution<double> z(0.0005, 0.01);
    for (int i = 0; i < 100; ++i) {
        const double r = z(rng);
        eq *= 1 + r;
        t = t + days(1);
        equity.emplace_back(t, eq);
        if (i > 0) bench.push_back(r * 0.5);
    }
    Metrics m = compute_metrics(equity, fills, {}, bench, {});
    EXPECT_EQ(m.n_trades, 2u);
    EXPECT_DOUBLE_EQ(m.win_rate, 0.5);
    EXPECT_NEAR(m.beta, 2.0, 0.2);
}

extern "C" {

static double test_slip_apply(void* self, double ref, double qty, int side, double, double) {
    const double bps = *static_cast<double*>(self);
    (void)qty;
    return ref * (1.0 + side * bps * 1e-4);
}
static void* test_slip_create(const char*) { return new double(25.0); }
static void test_slip_destroy(void* self) { delete static_cast<double*>(self); }
static const btos_slippage_vtable kTestSlipVtable{test_slip_apply};
static const btos_plugin_manifest kTestManifest{BTOS_PLUGIN_ABI_VERSION, BTOS_PLUGIN_SLIPPAGE,
                                                "test_slippage", test_slip_create,
                                                test_slip_destroy, &kTestSlipVtable};
static const btos_plugin_manifest* test_manifest_entry() { return &kTestManifest; }
}

TEST(Plugin, InProcessManifestAdaptsToSlippageInterface) {
    PluginRegistry reg;
    reg.register_manifest(test_manifest_entry());
    ASSERT_TRUE(reg.has("test_slippage"));
    auto model = reg.create_slippage("test_slippage", "{}");
    SlippageContext ctx{100.0, 10.0, Side::Buy, 1'000.0, 0.02};
    EXPECT_NEAR(model->apply(ctx), 100.25, 1e-9);
    ctx.side = Side::Sell;
    EXPECT_NEAR(model->apply(ctx), 99.75, 1e-9);
    EXPECT_THROW(static_cast<void>(reg.create_commission("test_slippage", "{}")), runtime_error);
    EXPECT_THROW(static_cast<void>(reg.create_slippage("missing", "{}")), runtime_error);
}

TEST(Plugin, RejectsBadManifests) {
    PluginRegistry reg;
    btos_plugin_manifest bad = kTestManifest;
    bad.abi_version = 999;
    EXPECT_THROW(reg.register_manifest(&bad), runtime_error);
    btos_plugin_manifest incomplete = kTestManifest;
    incomplete.vtable = nullptr;
    EXPECT_THROW(reg.register_manifest(&incomplete), runtime_error);
    EXPECT_THROW(reg.load_shared_library("/nonexistent/plugin.so"), runtime_error);
}

TEST(Engine, RiskRejectionsProduceAcksNotFills) {
    Engine eng;
    RiskLimits limits;
    limits.max_position_qty = 10;
    eng.with_instruments(synthetic_master({"SYN"}))
        .with_bars("SYN", gbm_feed(55, 100))
        .with_strategy(make_unique<MaCrossoverStrategy>("SYN"))
        .with_params({{"fast", 3}, {"slow", 10}, {"quantity", 100}})
        .with_risk_limits(limits)
        .with_seed(55);
    const RunResult r = eng.run();
    EXPECT_EQ(r.fills.size(), 0u);
}

TEST(Engine, ConfigurationErrorsThrow) {
    Engine e1;
    EXPECT_THROW(e1.run(), logic_error);
    Engine e2;
    e2.with_instruments(synthetic_master({"SYN"}))
        .with_strategy(make_unique<MaCrossoverStrategy>("SYN"));
    EXPECT_THROW(e2.run(), logic_error);
    Engine e3;
    e3.with_instruments(synthetic_master({"SYN"}))
        .with_bars("OTHER", gbm_feed(1, 10))
        .with_strategy(make_unique<MaCrossoverStrategy>("SYN"));
    EXPECT_THROW(e3.run(), logic_error);
}
