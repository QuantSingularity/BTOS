#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "btos/core/kernel.hpp"
#include "btos/core/rng.hpp"
#include "btos/data/calendar.hpp"
#include "btos/data/corporate_actions.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/data/readers.hpp"
#include "btos/exec/matching_engine.hpp"
#include "btos/portfolio/commission.hpp"
#include "btos/portfolio/portfolio.hpp"
#include "btos/risk/risk.hpp"
#include "btos/strategy/strategy.hpp"

using namespace std;

namespace btos {

struct RunResult {
    std::string run_id;
    std::uint64_t event_log_hash{0};
    std::uint64_t fill_log_hash{0};
    std::string final_state;
    std::vector<std::pair<TimestampNs, double>> equity_curve;
    std::vector<FillEvent> fills;
    std::vector<std::pair<TimestampNs, double>> exposure_curve;
    std::uint64_t events_dispatched{0};
    double final_equity{0};
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    Engine& with_instruments(InstrumentMaster master);

    Engine& with_bars(const std::string& symbol, std::shared_ptr<IBarReader> reader);

    Engine& with_calendar(Calendar cal);

    Engine& with_corporate_actions(CorporateActionBook book);

    Engine& with_strategy(std::unique_ptr<IStrategy> strategy);

    Engine& with_params(std::map<std::string, double> params);

    Engine& with_latency(std::unique_ptr<ILatencyModel> latency);

    Engine& with_slippage(std::unique_ptr<ISlippageModel> slippage);

    Engine& with_commission(std::unique_ptr<ICommissionModel> commission);

    Engine& with_risk_limits(RiskLimits limits);

    Engine& with_portfolio(PortfolioConfig cfg);

    Engine& with_seed(std::uint64_t seed);

    Engine& with_window(TimestampNs start, TimestampNs end);

    Engine& with_event_log(const std::string& path);

    RunResult run();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
