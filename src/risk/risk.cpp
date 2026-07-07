#include "btos/risk/risk.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

using namespace std;

namespace btos {

RiskDecision StandardPreTradeCheck::evaluate(const Order& order, double ref_price,
                                             const Portfolio& pf, TimestampNs now) const {
    if (killed_) return {false, "kill switch active"};
    const auto& pos = pf.position(order.instrument);
    const double new_qty = pos.quantity + order.quantity * sign(order.side);
    if (limits_.max_position_qty > 0 && std::fabs(new_qty) > limits_.max_position_qty)
        return {false, "max position quantity exceeded"};
    const double new_value = std::fabs(new_qty) * ref_price;
    if (limits_.max_position_value > 0 && new_value > limits_.max_position_value)
        return {false, "max position value exceeded"};
    const double eq = pf.equity(now);
    if (limits_.max_concentration > 0 && eq > 0 && new_value / eq > limits_.max_concentration)
        return {false, "max concentration exceeded"};
    if (limits_.max_gross_leverage > 0) {
        const double delta = order.quantity * ref_price;
        if (eq <= 0 || (pf.gross_exposure(now) + delta) / eq > limits_.max_gross_leverage)
            return {false, "max gross leverage exceeded"};
    }
    return {true, ""};
}

double historical_var(std::vector<double> returns, double alpha) {
    if (returns.empty()) throw std::invalid_argument("var: empty sample");
    std::sort(returns.begin(), returns.end());
    const double idx = (1.0 - alpha) * static_cast<double>(returns.size() - 1);
    const auto lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min(lo + 1, returns.size() - 1);
    const double frac = idx - static_cast<double>(lo);
    const double q = returns[lo] * (1.0 - frac) + returns[hi] * frac;
    return -q;
}

double expected_shortfall(std::vector<double> returns, double alpha) {
    if (returns.empty()) throw std::invalid_argument("es: empty sample");
    std::sort(returns.begin(), returns.end());
    const auto cut = static_cast<std::size_t>((1.0 - alpha) * static_cast<double>(returns.size()));
    const std::size_t n = std::max<std::size_t>(1, cut);
    double s = 0;
    for (std::size_t i = 0; i < n; ++i) s += returns[i];
    return -s / static_cast<double>(n);
}

double parametric_var(double mean, double stdev, double alpha) {
    return -(mean + stdev * inverse_normal_cdf(1.0 - alpha));
}

double vol_target_weight(const std::vector<double>& returns, double target_vol_annual,
                         double periods_per_year, double max_leverage) {
    if (returns.size() < 2) return 0;
    const double mean =
        std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
    double var = 0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);
    const double vol = std::sqrt(var * periods_per_year);
    if (vol <= 1e-12) return max_leverage;
    return std::clamp(target_vol_annual / vol, 0.0, max_leverage);
}

double kelly_fraction(const std::vector<double>& returns, double fraction, double cap) {
    if (returns.size() < 2) return 0;
    const double mean =
        std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
    double var = 0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);
    if (var <= 1e-12) return 0;
    return std::clamp(fraction * mean / var, -cap, cap);
}

double inverse_normal_cdf(double p) {
    if (p <= 0.0 || p >= 1.0) throw std::invalid_argument("inverse_normal_cdf: p out of (0,1)");
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00,  2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double plow = 0.02425;
    const double phigh = 1 - plow;
    double q = 0, r = 0;
    if (p < plow) {
        q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p <= phigh) {
        q = p - 0.5;
        r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
               (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
    }
    q = std::sqrt(-2 * std::log(1 - p));
    return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
           ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
}

double normal_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

}
