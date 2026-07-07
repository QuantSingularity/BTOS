#pragma once

#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "btos/core/events.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/portfolio/fx.hpp"

using namespace std;

namespace btos {

struct TaxLot {
    TimestampNs opened;
    double quantity{0};
    double price{0};
};

struct Position {
    double quantity{0};
    double avg_cost{0};
    double realized_pnl{0};
    std::deque<TaxLot> lots;
};

struct PortfolioConfig {
    double initial_capital{1'000'000.0};
    std::string base_currency{"USD"};
    double max_gross_leverage{0.0};
    double borrow_rate_annual{0.0};
    double financing_rate_annual{0.0};
};

class Portfolio {
  public:

    Portfolio(PortfolioConfig cfg, const InstrumentMaster& instruments, const FxRates& fx)
        : cfg_(std::move(cfg)), instruments_(&instruments), fx_(&fx) {
        cash_[cfg_.base_currency] = cfg_.initial_capital;
    }

    void apply_fill(const FillEvent& f, TimestampNs ts);

    void apply_corporate_action(const CorporateActionEvent& ca, TimestampNs ts);

    void mark(InstrumentId id, double price) { marks_[id.value] = price; }

    void accrue_financing(DurationNs dt);

    [[nodiscard]] const Position& position(InstrumentId id) const;

    [[nodiscard]] const std::map<std::string, double>& cash() const { return cash_; }

    [[nodiscard]] double equity(TimestampNs t) const;

    [[nodiscard]] double gross_exposure(TimestampNs t) const;

    [[nodiscard]] double unrealized_pnl(TimestampNs t) const;

    [[nodiscard]] double realized_pnl_base() const { return realized_base_; }

    [[nodiscard]] double commissions_base() const { return commissions_base_; }

    [[nodiscard]] double financing_base() const { return financing_base_; }

    [[nodiscard]] double dividends_base() const { return dividends_base_; }

    void check_identity(TimestampNs t, double eps = 1e-6) const;

    [[nodiscard]] bool would_breach_leverage(TimestampNs t, double delta_notional) const {
        if (cfg_.max_gross_leverage <= 0) return false;
        const double eq = equity(t);
        if (eq <= 0) return true;
        return (gross_exposure(t) + delta_notional) / eq > cfg_.max_gross_leverage;
    }

    [[nodiscard]] const PortfolioConfig& config() const { return cfg_; }

    [[nodiscard]] std::string serialize_state() const;

  private:
    PortfolioConfig cfg_;
    const InstrumentMaster* instruments_;
    const FxRates* fx_;
    std::map<std::string, double> cash_;
    std::map<std::uint32_t, Position> positions_;
    std::map<std::uint32_t, double> marks_;
    double realized_base_{0}, commissions_base_{0}, financing_base_{0}, dividends_base_{0};
    std::map<std::string, double> realized_local_;
    std::map<std::string, double> dividends_local_;
};

}
