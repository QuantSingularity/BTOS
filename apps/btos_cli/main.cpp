#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "btos/analytics/metrics.hpp"
#include "btos/core/event_log.hpp"
#include "btos/data/btosd.hpp"
#include "btos/data/synthetic.hpp"
#include "btos/data/validate.hpp"
#include "btos/engine.hpp"
#include "btos/strategy/reference.hpp"
#include "btos/opt/advanced.hpp"
#include "btos/opt/experiment_store.hpp"
#include "btos/opt/optimizer.hpp"
#include "btos/opt/validation.hpp"

using namespace std;
using json = nlohmann::json;

namespace {

using namespace btos;

map<string, string> parse_flags(int argc, char** argv, int start) {
    map<string, string> flags;
    for (int i = start; i < argc; ++i) {
        string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            const auto eq = a.find('=');
            if (eq != string::npos)
                flags[a.substr(2, eq - 2)] = a.substr(eq + 1);
            else if (i + 1 < argc && string(argv[i + 1]).rfind("--", 0) != 0)
                flags[a.substr(2)] = argv[++i];
            else
                flags[a.substr(2)] = "true";
        }
    }
    return flags;
}

string flag_or(const map<string, string>& f, const string& k, const string& d) {
    auto it = f.find(k);
    return it == f.end() ? d : it->second;
}

int usage() {
    cout << "btos <subcommand> [flags]\n\n"
            "Subcommands:\n"
            "  ingest    --csv <file> --out <file.btosd> [--period-sec 86400]\n"
            "  synth     --out <file.btosd> [--model gbm|heston] [--bars 2000] [--seed 42]\n"
            "            [--s0 100] [--mu 0.05] [--sigma 0.2] [--period-sec 86400]\n"
            "  validate  --data <file.btosd|file.csv> [--period-sec 86400]\n"
            "  run       --config <run.json> [--report <out.html>] [--metrics <out.json>]\n"
            "            [--db <experiments.jsonl>] [--event-log <out.log>]\n"
            "  optimize  --config <opt.json> [--db <experiments.jsonl>]\n"
            "  report    --db <experiments.jsonl> [--run-id <id>] [--limit 10]\n"
            "  replay    --event-log <file.log>\n";
    return 2;
}

shared_ptr<IBarReader> open_bars(const string& path, DurationNs period) {
    if (path.size() > 6 && path.substr(path.size() - 6) == ".btosd")
        return make_shared<BtosdBarReader>(path);
    return make_shared<CsvBarReader>(path, period);
}

unique_ptr<IStrategy> make_strategy(const string& name, const json& cfg) {
    if (name == "ma_crossover")
        return make_unique<MaCrossoverStrategy>(cfg.value("symbol", "SYN"));
    if (name == "pairs_stat_arb")
        return make_unique<PairsStrategy>(cfg.value("symbol_y", "Y"), cfg.value("symbol_x", "X"));
    if (name == "naive_market_maker")
        return make_unique<NaiveMarketMaker>(cfg.value("symbol", "SYN"));
    throw runtime_error("unknown strategy: " + name);
}

void build_engine_from_config(Engine& eng, const json& cfg, uint64_t seed_override,
                              bool has_override) {
    const json& data = cfg.at("data");
    InstrumentMaster master;
    if (cfg.contains("instruments")) {
        master = InstrumentMaster::from_csv(cfg["instruments"].get<string>());
    } else {
        for (const auto& feed : data) {
            const string symbol = feed.at("symbol").get<string>();
            if (!master.find(symbol)) {
                Instrument inst;
                inst.id = InstrumentId{static_cast<uint32_t>(master.all().size() + 1)};
                inst.symbol = symbol;
                inst.asset_class = AssetClass::Equity;
                inst.currency = "USD";
                inst.tick_size = 0.01;
                inst.multiplier = 1.0;
                inst.lot_size = 1.0;
                inst.listing = TimestampNs{0};
                inst.delisting = kMaxTimestamp;
                master.add(inst);
            }
        }
    }
    eng.with_instruments(master);
    for (const auto& feed : data) {
        const auto period = seconds(feed.value("period_sec", int64_t{86400}));
        eng.with_bars(feed.at("symbol").get<string>(),
                      open_bars(feed.at("path").get<string>(), period));
    }
    if (cfg.contains("corporate_actions"))
        eng.with_corporate_actions(
            CorporateActionBook::from_csv(cfg["corporate_actions"].get<string>()));
    const json& strat = cfg.at("strategy");
    eng.with_strategy(make_strategy(strat.at("name").get<string>(), strat));
    map<string, double> params;
    if (strat.contains("params"))
        for (const auto& [k, v] : strat["params"].items()) params[k] = v.get<double>();
    eng.with_params(params);
    if (cfg.contains("execution")) {
        const json& ex = cfg["execution"];
        if (ex.contains("latency_ms"))
            eng.with_latency(make_unique<FixedLatency>(milliseconds(ex["latency_ms"].get<int64_t>())));
        if (ex.contains("slippage_bps"))
            eng.with_slippage(make_unique<FixedBpsSlippage>(ex["slippage_bps"].get<double>()));
        if (ex.contains("impact_k"))
            eng.with_slippage(make_unique<SquareRootImpact>(ex["impact_k"].get<double>()));
        if (ex.contains("commission_bps"))
            eng.with_commission(make_unique<BpsCommission>(ex["commission_bps"].get<double>()));
        if (ex.contains("commission_per_share"))
            eng.with_commission(
                make_unique<PerShareCommission>(ex["commission_per_share"].get<double>()));
    }
    if (cfg.contains("risk")) {
        const json& r = cfg["risk"];
        RiskLimits lim;
        lim.max_position_qty = r.value("max_position_qty", 0.0);
        lim.max_position_value = r.value("max_position_value", 0.0);
        lim.max_concentration = r.value("max_concentration", 0.0);
        lim.max_gross_leverage = r.value("max_gross_leverage", 0.0);
        lim.max_drawdown_kill = r.value("max_drawdown_kill", 0.0);
        eng.with_risk_limits(lim);
    }
    if (cfg.contains("portfolio")) {
        const json& p = cfg["portfolio"];
        PortfolioConfig pc;
        pc.initial_capital = p.value("initial_capital", 1'000'000.0);
        pc.base_currency = p.value("base_currency", string("USD"));
        pc.borrow_rate_annual = p.value("borrow_rate_annual", 0.0);
        pc.financing_rate_annual = p.value("financing_rate_annual", 0.0);
        eng.with_portfolio(pc);
    }
    eng.with_seed(has_override ? seed_override : cfg.value("seed", uint64_t{0}));
}

string config_hash_of(const json& cfg) {
    Fnv1a64 h;
    h.update(cfg.dump());
    return to_hex(h.digest());
}

void record_run(const string& db_path, const string& run_id, const string& config_hash,
                uint64_t seed, const string& metrics_json) {
    if (db_path.empty()) return;
    auto db = open_experiment_store(db_path);
    ExperimentRecord rec;
    rec.run_id = run_id;
    rec.config_hash = config_hash;
    rec.dataset_hash = run_id.substr(0, 16);
    rec.git_commit = current_git_commit(".");
    rec.seed = seed;
    rec.metrics_json = metrics_json;
    rec.created_utc = format_iso8601_utc(TimestampNs{
        static_cast<int64_t>(chrono::duration_cast<chrono::nanoseconds>(
                                 chrono::system_clock::now().time_since_epoch())
                                 .count())});
    db->insert(rec);
}

int cmd_ingest(const map<string, string>& f) {
    const string csv = flag_or(f, "csv", "");
    const string out = flag_or(f, "out", "");
    if (csv.empty() || out.empty()) return usage();
    const auto period = seconds(stoll(flag_or(f, "period-sec", "86400")));
    CsvBarReader reader(csv, period);
    write_btosd(out, reader.bars(), period);
    cout << "wrote " << reader.bars().size() << " bars to " << out << "\n";
    return 0;
}

int cmd_synth(const map<string, string>& f) {
    const string out = flag_or(f, "out", "");
    if (out.empty()) return usage();
    const auto n = static_cast<size_t>(stoull(flag_or(f, "bars", "2000")));
    const auto seed = static_cast<uint64_t>(stoull(flag_or(f, "seed", "42")));
    const auto period = seconds(stoll(flag_or(f, "period-sec", "86400")));
    RngProvider rng(seed);
    auto stream = rng.stream("synthetic");
    vector<Bar> bars;
    const string model = flag_or(f, "model", "gbm");
    const TimestampNs start = parse_iso8601_utc("2020-01-01");
    if (model == "heston") {
        HestonParams p;
        p.s0 = stod(flag_or(f, "s0", "100"));
        p.mu = stod(flag_or(f, "mu", "0.05"));
        bars = generate_heston_bars(stream, p, start, period, n);
    } else {
        GbmParams p;
        p.s0 = stod(flag_or(f, "s0", "100"));
        p.mu = stod(flag_or(f, "mu", "0.05"));
        p.sigma = stod(flag_or(f, "sigma", "0.2"));
        bars = generate_gbm_bars(stream, p, start, period, n);
    }
    write_btosd(out, bars, period);
    cout << "wrote " << bars.size() << " synthetic " << model << " bars to " << out << "\n";
    return 0;
}

int cmd_validate(const map<string, string>& f) {
    const string path = flag_or(f, "data", "");
    if (path.empty()) return usage();
    const auto period = seconds(stoll(flag_or(f, "period-sec", "86400")));
    auto reader = open_bars(path, period);
    const auto issues = validate_bars(reader->bars(), reader->period(), Calendar::always_open());
    if (issues.empty()) {
        cout << "OK: " << reader->bars().size() << " bars, no issues\n";
        return 0;
    }
    cout << format_issues(issues);
    cout << issues.size() << " issue(s) found in " << reader->bars().size() << " bars\n";
    return 1;
}

int cmd_run(const map<string, string>& f) {
    const string cfg_path = flag_or(f, "config", "");
    if (cfg_path.empty()) return usage();
    ifstream in(cfg_path);
    if (!in) throw runtime_error("cannot open config: " + cfg_path);
    json cfg = json::parse(in);
    Engine eng;
    build_engine_from_config(eng, cfg, 0, false);
    const string log_path = flag_or(f, "event-log", "");
    if (!log_path.empty()) eng.with_event_log(log_path);
    RunResult res = eng.run();
    MetricsOptions mopts;
    if (cfg.contains("analytics"))
        mopts.periods_per_year = cfg["analytics"].value("periods_per_year", 252.0);
    Metrics m = compute_metrics(res.equity_curve, res.fills, res.exposure_curve, {}, mopts);
    const string metrics_json = metrics_to_json(m);
    cout << "run_id " << res.run_id << "\n"
         << "events " << res.events_dispatched << ", fills " << res.fills.size() << "\n"
         << "final equity " << res.final_equity << "\n"
         << "event log hash " << to_hex(res.event_log_hash) << "\n"
         << metrics_json << "\n";
    const string report_path = flag_or(f, "report", "");
    if (!report_path.empty()) {
        ofstream rep(report_path);
        rep << render_html_report("BTOS run " + res.run_id, m, res.equity_curve);
        cout << "report written to " << report_path << "\n";
    }
    const string metrics_path = flag_or(f, "metrics", "");
    if (!metrics_path.empty()) {
        ofstream mj(metrics_path);
        mj << metrics_json << "\n";
    }
    record_run(flag_or(f, "db", ""), res.run_id, config_hash_of(cfg), cfg.value("seed", uint64_t{0}),
               metrics_json);
    return 0;
}

int cmd_optimize(const map<string, string>& f) {
    const string cfg_path = flag_or(f, "config", "");
    if (cfg_path.empty()) return usage();
    ifstream in(cfg_path);
    if (!in) throw runtime_error("cannot open config: " + cfg_path);
    json cfg = json::parse(in);
    const json& opt = cfg.at("optimize");
    vector<ParamSpec> space;
    for (const auto& p : opt.at("space"))
        space.push_back(ParamSpec{p.at("name").get<string>(), p.at("lo").get<double>(),
                                  p.at("hi").get<double>(), p.value("step", 0.0)});
    const auto budget = opt.value("budget", size_t{32});
    const auto seed = cfg.value("seed", uint64_t{0});
    const string method = opt.value("method", string("random"));
    const string metric = opt.value("metric", string("sharpe"));

    vector<double> trial_sharpes;
    vector<double> best_returns;
    double best_obj = -1e300;
    Objective objective = [&](const ParamMap& params) {
        json run_cfg = cfg;
        for (const auto& [k, v] : params) run_cfg["strategy"]["params"][k] = v;
        Engine eng;
        build_engine_from_config(eng, run_cfg, seed, true);
        RunResult res = eng.run();
        Metrics m = compute_metrics(res.equity_curve, res.fills, res.exposure_curve, {}, {});
        const double obj = metric == "total_return" ? m.total_return
                           : metric == "cagr"       ? m.cagr
                                                    : m.sharpe;
        trial_sharpes.push_back(m.sharpe);
        if (obj > best_obj) {
            best_obj = obj;
            best_returns.clear();
            for (size_t i = 1; i < res.equity_curve.size(); ++i) {
                const double prev = res.equity_curve[i - 1].second;
                best_returns.push_back(prev != 0 ? res.equity_curve[i].second / prev - 1.0 : 0.0);
            }
        }
        return obj;
    };

    unique_ptr<IOptimizer> optimizer;
    if (method == "grid") optimizer = make_unique<GridOptimizer>();
    else if (method == "bayes") optimizer = make_unique<BayesianGpOptimizer>();
    else if (method == "cmaes") optimizer = make_unique<CmaEsOptimizer>();
    else optimizer = make_unique<RandomOptimizer>();

    SerialBackend backend;
    auto evals = optimizer->optimize(space, objective, budget, seed, backend);
    const Evaluation& best = evals.back();
    cout << "method " << optimizer->name() << ", evaluated " << evals.size() << " candidates\n";
    cout << "best " << metric << " " << best.objective << " with params ";
    for (const auto& [k, v] : best.params) cout << k << "=" << v << " ";
    cout << "\n";

    if (best_returns.size() >= 4 && trial_sharpes.size() >= 2) {
        double msr = 0;
        for (double s : trial_sharpes) msr += s;
        msr /= static_cast<double>(trial_sharpes.size());
        double vsr = 0;
        for (double s : trial_sharpes) vsr += (s - msr) * (s - msr);
        vsr /= static_cast<double>(trial_sharpes.size() - 1);
        const double sr_period = sharpe_ratio(best_returns, 1.0);
        const double dsr = deflated_sharpe_ratio(
            sr_period, best_returns.size(), sample_skewness(best_returns),
            sample_kurtosis(best_returns), trial_sharpes.size(),
            vsr / max(1.0, 252.0));
        cout << "deflated Sharpe ratio (prob true SR > 0 after " << trial_sharpes.size()
             << " trials): " << dsr << "\n";
    }

    json best_json;
    for (const auto& [k, v] : best.params) best_json[k] = v;
    record_run(flag_or(f, "db", ""), "opt_" + config_hash_of(cfg), config_hash_of(cfg), seed,
               json{{"method", optimizer->name()},
                    {"metric", metric},
                    {"best_objective", best.objective},
                    {"best_params", best_json}}
                   .dump());
    return 0;
}

int cmd_report(const map<string, string>& f) {
    const string db_path = flag_or(f, "db", "");
    if (db_path.empty()) return usage();
    auto db = open_experiment_store(db_path);
    const string run_id = flag_or(f, "run-id", "");
    if (!run_id.empty()) {
        auto rec = db->get(run_id);
        if (!rec) {
            cout << "run not found: " << run_id << "\n";
            return 1;
        }
        cout << "run_id " << rec->run_id << "\nconfig_hash " << rec->config_hash
             << "\ndataset_hash " << rec->dataset_hash << "\ngit_commit " << rec->git_commit
             << "\nseed " << rec->seed << "\ncreated " << rec->created_utc << "\nmetrics "
             << rec->metrics_json << "\n";
        return 0;
    }
    const auto limit = static_cast<size_t>(stoul(flag_or(f, "limit", "10")));
    for (const auto& rec : db->recent(limit))
        cout << rec.created_utc << "  " << rec.run_id << "  seed=" << rec.seed << "  "
             << rec.metrics_json.substr(0, 90) << "\n";
    return 0;
}

int cmd_replay(const map<string, string>& f) {
    const string log_path = flag_or(f, "event-log", "");
    if (log_path.empty()) return usage();
    EventLogReader reader(log_path);
    Kernel kernel;
    uint64_t bars = 0, fills = 0;
    kernel.subscribe([&](const Event& ev) {
        if (holds_alternative<BarEvent>(ev.payload)) ++bars;
        else if (holds_alternative<FillEvent>(ev.payload)) ++fills;
    });
    reader.replay_into(kernel);
    cout << "replayed " << reader.events().size() << " events (" << bars << " bars, " << fills
         << " fills)\n"
         << "log digest " << to_hex(reader.digest()) << "\n";
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const string cmd = argv[1];
    const auto flags = parse_flags(argc, argv, 2);
    try {
        if (cmd == "ingest") return cmd_ingest(flags);
        if (cmd == "synth") return cmd_synth(flags);
        if (cmd == "validate") return cmd_validate(flags);
        if (cmd == "run") return cmd_run(flags);
        if (cmd == "optimize") return cmd_optimize(flags);
        if (cmd == "report") return cmd_report(flags);
        if (cmd == "replay") return cmd_replay(flags);
    } catch (const exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return usage();
}
