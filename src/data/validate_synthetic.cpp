#include <algorithm>
#include <cmath>
#include <sstream>

#include "btos/data/synthetic.hpp"
#include "btos/data/validate.hpp"

using namespace std;

namespace btos {

std::vector<ValidationIssue> validate_bars(const std::vector<Bar>& bars, DurationNs period,
                                           const Calendar& calendar,
                                           const ValidationOptions& opts) {
    std::vector<ValidationIssue> issues;
    std::vector<double> rets;
    rets.reserve(bars.size());
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        if (b.open <= 0 || b.high <= 0 || b.low <= 0 || b.close <= 0)
            issues.push_back({ValidationIssue::Kind::NonPositivePrice, i, "non-positive price"});
        if (b.volume < 0)
            issues.push_back({ValidationIssue::Kind::NegativeVolume, i, "negative volume"});
        if (b.high < b.low || b.open > b.high || b.open < b.low || b.close > b.high ||
            b.close < b.low)
            issues.push_back({ValidationIssue::Kind::OhlcInconsistent, i, "OHLC bounds violated"});
        if (i > 0) {
            if (bars[i].ts < bars[i - 1].ts)
                issues.push_back(
                    {ValidationIssue::Kind::NonMonotonicTimestamp, i, "timestamp decreased"});
            else if (bars[i].ts == bars[i - 1].ts)
                issues.push_back(
                    {ValidationIssue::Kind::DuplicateTimestamp, i, "duplicate timestamp"});
            else if (opts.check_gaps && period.ns > 0) {
                const std::int64_t delta = (bars[i].ts - bars[i - 1].ts).ns;
                if (delta > period.ns && calendar.is_open(bars[i - 1].ts + period)) {
                    std::ostringstream os;
                    os << "gap of " << delta / period.ns << " periods";
                    issues.push_back({ValidationIssue::Kind::Gap, i, os.str()});
                }
            }
            if (bars[i - 1].close > 0 && bars[i].close > 0)
                rets.push_back(std::log(bars[i].close / bars[i - 1].close));
            else
                rets.push_back(0.0);
        }
    }
    if (rets.size() >= 20 && opts.outlier_z > 0) {
        double mean = 0;
        for (double r : rets) mean += r;
        mean /= static_cast<double>(rets.size());
        double var = 0;
        for (double r : rets) var += (r - mean) * (r - mean);
        var /= static_cast<double>(rets.size() - 1);
        const double sd = std::sqrt(var);
        if (sd > 0) {
            for (std::size_t i = 0; i < rets.size(); ++i) {
                const double z = std::fabs(rets[i] - mean) / sd;
                if (z > opts.outlier_z) {
                    std::ostringstream os;
                    os << "log return z-score " << z;
                    issues.push_back({ValidationIssue::Kind::ReturnOutlier, i + 1, os.str()});
                }
            }
        }
    }
    return issues;
}

std::string format_issues(const std::vector<ValidationIssue>& issues) {
    static const char* names[] = {"non_monotonic_timestamp", "duplicate_timestamp", "gap",
                                  "ohlc_inconsistent",       "non_positive_price",  "negative_volume",
                                  "return_outlier"};
    std::ostringstream os;
    for (const auto& is : issues)
        os << "row " << is.row << ": " << names[static_cast<int>(is.kind)] << " (" << is.detail
           << ")\n";
    return os.str();
}

std::vector<Bar> generate_gbm_bars(std::mt19937_64& rng, const GbmParams& p, TimestampNs start,
                                   DurationNs period, std::size_t n) {
    std::normal_distribution<double> z(0.0, 1.0);
    std::lognormal_distribution<double> volgen(std::log(p.base_volume), 0.5);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    const double dt = static_cast<double>(period.ns) / p.year_ns;
    const double drift = (p.mu - 0.5 * p.sigma * p.sigma) * dt;
    const double diff = p.sigma * std::sqrt(dt);
    std::vector<Bar> bars(n);
    double s = p.s0;
    TimestampNs ts = start + period;
    for (std::size_t i = 0; i < n; ++i) {
        const double open = s;
        const double close = s * std::exp(drift + diff * z(rng));
        const double lo0 = std::min(open, close);
        const double hi0 = std::max(open, close);
        const double range = std::fabs(diff) * s;
        const double high = hi0 + range * u(rng) * 0.5;
        const double low = std::max(1e-9, lo0 - range * u(rng) * 0.5);
        bars[i] = Bar{ts, open, high, low, close, volgen(rng)};
        s = close;
        ts = ts + period;
    }
    return bars;
}

std::vector<Bar> generate_heston_bars(std::mt19937_64& rng, const HestonParams& p,
                                      TimestampNs start, DurationNs period, std::size_t n) {
    std::normal_distribution<double> z(0.0, 1.0);
    std::lognormal_distribution<double> volgen(std::log(p.base_volume), 0.5);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    const double dt = static_cast<double>(period.ns) / p.year_ns;
    const double sq_dt = std::sqrt(dt);
    const double rho_c = std::sqrt(1.0 - p.rho * p.rho);
    std::vector<Bar> bars(n);
    double s = p.s0;
    double v = p.v0;
    TimestampNs ts = start + period;
    for (std::size_t i = 0; i < n; ++i) {
        const double z1 = z(rng);
        const double z2 = p.rho * z1 + rho_c * z(rng);
        const double vp = std::max(v, 0.0);
        const double sv = std::sqrt(vp);
        const double open = s;
        const double close = s * std::exp((p.mu - 0.5 * vp) * dt + sv * sq_dt * z1);
        v = v + p.kappa * (p.theta - vp) * dt + p.xi * sv * sq_dt * z2;
        const double lo0 = std::min(open, close);
        const double hi0 = std::max(open, close);
        const double range = sv * sq_dt * s;
        const double high = hi0 + range * u(rng) * 0.5;
        const double low = std::max(1e-9, lo0 - range * u(rng) * 0.5);
        bars[i] = Bar{ts, open, high, low, close, volgen(rng)};
        s = close;
        ts = ts + period;
    }
    return bars;
}

std::vector<Trade> generate_poisson_trades(std::mt19937_64& rng, const PoissonTradeParams& p,
                                           const std::vector<Bar>& mid_bars, TimestampNs start,
                                           TimestampNs end) {
    std::exponential_distribution<double> gap(p.lambda_per_second);
    std::exponential_distribution<double> size(1.0 / p.size_mean);
    std::bernoulli_distribution buy(0.5);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<Trade> trades;
    if (mid_bars.empty()) return trades;
    double t = static_cast<double>(start.ns);
    const double t_end = static_cast<double>(end.ns);
    std::size_t bi = 0;
    while (true) {
        t += gap(rng) * 1e9;
        if (t >= t_end) break;
        const TimestampNs ts{static_cast<std::int64_t>(t)};
        while (bi + 1 < mid_bars.size() && mid_bars[bi + 1].ts <= ts) ++bi;
        const double mid = mid_bars[bi].close;
        const bool is_buy = buy(rng);
        const double half_spread = mid * p.spread_bps * 1e-4 * 0.5;
        const double price = is_buy ? mid + half_spread * (0.5 + u(rng))
                                    : mid - half_spread * (0.5 + u(rng));
        trades.push_back(Trade{ts, price, std::max(1.0, size(rng)), is_buy ? Side::Buy : Side::Sell});
    }
    return trades;
}

}
