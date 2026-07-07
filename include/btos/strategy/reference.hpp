#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "btos/strategy/strategy.hpp"

using namespace std;

namespace btos {

class MaCrossoverStrategy final : public IStrategy {
  public:
    explicit MaCrossoverStrategy(std::string symbol);
    void on_start(StrategyContext& ctx) override;
    void on_bar(StrategyContext& ctx, const BarEvent& bar) override;
    void set_param(const std::string& name, double value) override;
    [[nodiscard]] std::string save_state() const override;
    void load_state(const std::string& json) override;
    [[nodiscard]] std::string name() const override { return "ma_crossover"; }

  private:
    std::string symbol_;
    InstrumentId inst_{};
    std::size_t fast_{10}, slow_{30};
    double quantity_{100};
    std::deque<double> closes_;
    int position_state_{0};
};

class PairsStrategy final : public IStrategy {
  public:
    PairsStrategy(std::string symbol_y, std::string symbol_x);
    void on_start(StrategyContext& ctx) override;
    void on_bar(StrategyContext& ctx, const BarEvent& bar) override;
    void set_param(const std::string& name, double value) override;
    [[nodiscard]] std::string save_state() const override;
    void load_state(const std::string& json) override;
    [[nodiscard]] std::string name() const override { return "pairs_stat_arb"; }

  private:
    void evaluate(StrategyContext& ctx);
    std::string sym_y_, sym_x_;
    InstrumentId inst_y_{}, inst_x_{};
    std::size_t window_{60};
    double entry_z_{2.0}, exit_z_{0.5}, quantity_{100};
    int state_{0};
    double beta_{0};
    TimestampNs last_bar_ts_{kNoTimestamp};
    bool seen_y_{false}, seen_x_{false};
};

class NaiveMarketMaker final : public IStrategy {
  public:
    explicit NaiveMarketMaker(std::string symbol);
    void on_start(StrategyContext& ctx) override;
    void on_bar(StrategyContext& ctx, const BarEvent& bar) override;
    void set_param(const std::string& name, double value) override;
    [[nodiscard]] std::string name() const override { return "naive_market_maker"; }

  private:
    std::string symbol_;
    InstrumentId inst_{};
    double spread_bps_{10.0}, quote_size_{100}, max_inventory_{500};
    std::vector<OrderId> live_quotes_;
};

class VectorizedSignalStrategy final : public IStrategy {
  public:

    using Signal = std::function<double(const std::vector<double>& closes)>;

    VectorizedSignalStrategy(std::string symbol, Signal signal)
        : symbol_(std::move(symbol)), signal_(std::move(signal)) {}
    void on_start(StrategyContext& ctx) override;
    void on_bar(StrategyContext& ctx, const BarEvent& bar) override;
    [[nodiscard]] std::string name() const override { return "vectorized_signal"; }

  private:
    std::string symbol_;
    Signal signal_;
    InstrumentId inst_{};
    std::vector<double> closes_;
};

}
