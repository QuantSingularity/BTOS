#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "btos/exec/order.hpp"
#include "btos/portfolio/portfolio.hpp"

using namespace std;

namespace btos {

struct RiskDecision {
    bool accepted{true};
    std::string reason;
};

class IPreTradeCheck {
  public:
    virtual ~IPreTradeCheck() = default;

    [[nodiscard]] virtual RiskDecision evaluate(const Order& order, double ref_price,
                                                const Portfolio& pf, TimestampNs now) const = 0;

    [[nodiscard]] virtual std::string name() const = 0;
};

struct RiskLimits {
    double max_position_qty{0};
    double max_position_value{0};
    double max_concentration{0};
    double max_gross_leverage{0};
    double max_drawdown_kill{0};
};

class StandardPreTradeCheck final : public IPreTradeCheck {
  public:
    explicit StandardPreTradeCheck(RiskLimits limits) : limits_(limits) {}

    [[nodiscard]] RiskDecision evaluate(const Order& order, double ref_price, const Portfolio& pf,
                                        TimestampNs now) const override;
    [[nodiscard]] std::string name() const override { return "standard_pretrade"; }

    void observe_equity(double equity) {
        peak_ = std::max(peak_.value_or(equity), equity);
        if (limits_.max_drawdown_kill > 0 && peak_ && *peak_ > 0 &&
            (*peak_ - equity) / *peak_ >= limits_.max_drawdown_kill)
            killed_ = true;
    }

    [[nodiscard]] bool killed() const { return killed_; }

  private:
    RiskLimits limits_;
    std::optional<double> peak_;
    bool killed_{false};
};

double historical_var(std::vector<double> returns, double alpha);

double expected_shortfall(std::vector<double> returns, double alpha);

double parametric_var(double mean, double stdev, double alpha);

double vol_target_weight(const std::vector<double>& returns, double target_vol_annual,
                         double periods_per_year, double max_leverage = 3.0);

double kelly_fraction(const std::vector<double>& returns, double fraction = 0.5, double cap = 2.0);

double inverse_normal_cdf(double p);

double normal_cdf(double x);

}
