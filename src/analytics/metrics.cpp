#include "btos/analytics/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <numeric>
#include <sstream>

using namespace std;

namespace btos {
namespace {

std::vector<double> pct_returns(const std::vector<std::pair<TimestampNs, double>>& equity) {
    std::vector<double> r;
    if (equity.size() < 2) return r;
    r.reserve(equity.size() - 1);
    for (std::size_t i = 1; i < equity.size(); ++i) {
        const double prev = equity[i - 1].second;
        r.push_back(prev != 0 ? equity[i].second / prev - 1.0 : 0.0);
    }
    return r;
}

double mean_of(const std::vector<double>& v) {
    if (v.empty()) return 0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double stdev_of(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    const double m = mean_of(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

}

Metrics compute_metrics(const std::vector<std::pair<TimestampNs, double>>& equity,
                        const std::vector<FillEvent>& fills,
                        const std::vector<std::pair<TimestampNs, double>>& exposure,
                        const std::vector<double>& benchmark_returns, const MetricsOptions& opts) {
    Metrics m;
    if (equity.size() < 2) return m;
    const std::vector<double> rets = pct_returns(equity);
    const double first = equity.front().second;
    const double last = equity.back().second;
    m.total_return = first != 0 ? last / first - 1.0 : 0.0;

    const double span_years =
        static_cast<double>((equity.back().first - equity.front().first).ns) /
        (365.25 * 86400.0 * 1e9);
    if (span_years > 1e-9 && first > 0 && last > 0)
        m.cagr = std::pow(last / first, 1.0 / span_years) - 1.0;

    m.volatility_annual = stdev_of(rets) * std::sqrt(opts.periods_per_year);
    const double sd = stdev_of(rets);
    if (sd > 1e-15) m.sharpe = mean_of(rets) / sd * std::sqrt(opts.periods_per_year);
    std::vector<double> downside;
    for (double r : rets)
        if (r < 0) downside.push_back(r);
    if (!downside.empty()) {
        double dd = 0;
        for (double r : downside) dd += r * r;
        dd = std::sqrt(dd / static_cast<double>(rets.size()));
        if (dd > 1e-15) m.sortino = mean_of(rets) / dd * std::sqrt(opts.periods_per_year);
    }

    double peak = equity.front().second;
    TimestampNs peak_ts = equity.front().first;
    DrawdownEpisode cur{};
    bool in_dd = false;
    m.underwater.reserve(equity.size());
    for (const auto& [ts, eq] : equity) {
        if (eq >= peak) {
            if (in_dd) {
                cur.recovery_ts = ts;
                m.drawdowns.push_back(cur);
                in_dd = false;
            }
            peak = eq;
            peak_ts = ts;
            m.underwater.emplace_back(ts, 0.0);
        } else {
            const double depth = peak > 0 ? (peak - eq) / peak : 0.0;
            m.underwater.emplace_back(ts, depth);
            if (!in_dd) {
                in_dd = true;
                cur = DrawdownEpisode{peak_ts, ts, kNoTimestamp, depth};
            } else if (depth > cur.depth) {
                cur.depth = depth;
                cur.trough_ts = ts;
            }
            m.max_drawdown = std::max(m.max_drawdown, depth);
        }
    }
    if (in_dd) m.drawdowns.push_back(cur);
    std::sort(m.drawdowns.begin(), m.drawdowns.end(),
              [](const DrawdownEpisode& a, const DrawdownEpisode& b) { return a.depth > b.depth; });

    double traded_notional = 0;
    for (const auto& f : fills) traded_notional += f.quantity * f.price;
    const double avg_equity =
        std::accumulate(equity.begin(), equity.end(), 0.0,
                        [](double acc, const auto& p) { return acc + p.second; }) /
        static_cast<double>(equity.size());
    if (avg_equity > 0 && span_years > 1e-9)
        m.turnover_annual = traded_notional / avg_equity / span_years;

    if (!exposure.empty()) {
        double s = 0;
        for (const auto& [ts, g] : exposure) s += g;
        m.exposure_mean = s / static_cast<double>(exposure.size());
    }

    struct OpenLot {
        TimestampNs ts;
        double qty;
        double px;
    };
    std::map<std::uint32_t, std::deque<OpenLot>> open;
    for (const auto& f : fills) {
        auto& lots = open[f.instrument.value];
        double remaining = f.quantity;
        const double s = sign(f.side);
        while (remaining > 1e-9 && !lots.empty() && lots.front().qty * s < 0) {
            OpenLot& lot = lots.front();
            const double closed = std::min(remaining, std::fabs(lot.qty));
            const double lot_sign = lot.qty > 0 ? 1.0 : -1.0;
            TradeRecord tr;
            tr.instrument = f.instrument;
            tr.open_ts = lot.ts;
            tr.close_ts = TimestampNs{0};
            tr.side = lot_sign > 0 ? Side::Buy : Side::Sell;
            tr.quantity = closed;
            tr.entry_price = lot.px;
            tr.exit_price = f.price;
            tr.pnl = (f.price - lot.px) * closed * lot_sign;
            m.trades.push_back(tr);
            lot.qty -= closed * lot_sign;
            remaining -= closed;
            if (std::fabs(lot.qty) <= 1e-9) lots.pop_front();
        }
        if (remaining > 1e-9) lots.push_back(OpenLot{TimestampNs{0}, remaining * s, f.price});
    }
    m.n_trades = m.trades.size();
    if (!m.trades.empty()) {
        std::size_t wins = 0;
        for (const auto& t : m.trades)
            if (t.pnl > 0) ++wins;
        m.win_rate = static_cast<double>(wins) / static_cast<double>(m.trades.size());
    }

    if (benchmark_returns.size() == rets.size() && rets.size() >= 2) {
        const double mb = mean_of(benchmark_returns);
        const double mr = mean_of(rets);
        double cov = 0, varb = 0;
        for (std::size_t i = 0; i < rets.size(); ++i) {
            cov += (rets[i] - mr) * (benchmark_returns[i] - mb);
            varb += (benchmark_returns[i] - mb) * (benchmark_returns[i] - mb);
        }
        cov /= static_cast<double>(rets.size() - 1);
        varb /= static_cast<double>(rets.size() - 1);
        if (varb > 1e-15) {
            m.beta = cov / varb;
            m.alpha_annual = (mr - m.beta * mb) * opts.periods_per_year;
        }
        std::vector<double> active(rets.size());
        for (std::size_t i = 0; i < rets.size(); ++i) active[i] = rets[i] - benchmark_returns[i];
        const double te = stdev_of(active);
        if (te > 1e-15)
            m.information_ratio = mean_of(active) / te * std::sqrt(opts.periods_per_year);
    }

    const std::size_t w = opts.rolling_window;
    if (rets.size() >= w && w >= 2) {
        for (std::size_t i = 0; i + w <= rets.size(); ++i) {
            std::vector<double> win(rets.begin() + static_cast<std::ptrdiff_t>(i),
                                    rets.begin() + static_cast<std::ptrdiff_t>(i + w));
            const double wsd = stdev_of(win);
            m.rolling_vol.push_back(wsd * std::sqrt(opts.periods_per_year));
            m.rolling_sharpe.push_back(
                wsd > 1e-15 ? mean_of(win) / wsd * std::sqrt(opts.periods_per_year) : 0.0);
        }
    }

    std::map<std::string, double> month_start, month_end;
    for (const auto& [ts, eq] : equity) {
        const std::string key = format_iso8601_utc(ts).substr(0, 7);
        if (month_start.find(key) == month_start.end()) month_start[key] = eq;
        month_end[key] = eq;
    }
    for (const auto& [key, start] : month_start) {
        const double end = month_end[key];
        m.returns_by_month.emplace_back(key, start != 0 ? end / start - 1.0 : 0.0);
    }
    return m;
}

std::string metrics_to_json(const Metrics& m) {
    std::ostringstream os;
    os.precision(10);
    os << "{"
       << "\"total_return\":" << m.total_return << ",\"cagr\":" << m.cagr
       << ",\"volatility_annual\":" << m.volatility_annual << ",\"sharpe\":" << m.sharpe
       << ",\"sortino\":" << m.sortino << ",\"max_drawdown\":" << m.max_drawdown
       << ",\"turnover_annual\":" << m.turnover_annual << ",\"exposure_mean\":" << m.exposure_mean
       << ",\"n_trades\":" << m.n_trades << ",\"win_rate\":" << m.win_rate
       << ",\"alpha_annual\":" << m.alpha_annual << ",\"beta\":" << m.beta
       << ",\"information_ratio\":" << m.information_ratio << ",\"returns_by_month\":{";
    for (std::size_t i = 0; i < m.returns_by_month.size(); ++i)
        os << (i ? "," : "") << "\"" << m.returns_by_month[i].first
           << "\":" << m.returns_by_month[i].second;
    os << "}}";
    return os.str();
}

}
