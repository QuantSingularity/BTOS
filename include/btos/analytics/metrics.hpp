#pragma once

#include <string>
#include <vector>

#include "btos/core/events.hpp"
#include "btos/core/time.hpp"

using namespace std;

namespace btos {

struct DrawdownEpisode {
    TimestampNs peak_ts, trough_ts, recovery_ts;
    double depth{0};
};

struct TradeRecord {
    InstrumentId instrument;
    TimestampNs open_ts, close_ts;
    Side side{Side::Buy};
    double quantity{0};
    double entry_price{0}, exit_price{0};
    double pnl{0};
};

struct Metrics {
    double total_return{0};
    double cagr{0};
    double volatility_annual{0};
    double sharpe{0};
    double sortino{0};
    double max_drawdown{0};
    double turnover_annual{0};
    double exposure_mean{0};
    std::size_t n_trades{0};
    double win_rate{0};

    double alpha_annual{0};
    double beta{0};
    double information_ratio{0};
    std::vector<DrawdownEpisode> drawdowns;
    std::vector<double> rolling_sharpe;
    std::vector<double> rolling_vol;
    std::vector<TradeRecord> trades;
    std::vector<std::pair<TimestampNs, double>> underwater;
    std::vector<std::pair<std::string, double>> returns_by_month;
};

struct MetricsOptions {
    double periods_per_year{252.0};
    std::size_t rolling_window{20};
};

Metrics compute_metrics(const std::vector<std::pair<TimestampNs, double>>& equity,
                        const std::vector<FillEvent>& fills,
                        const std::vector<std::pair<TimestampNs, double>>& exposure,
                        const std::vector<double>& benchmark_returns, const MetricsOptions& opts);

std::string metrics_to_json(const Metrics& m);

std::string render_html_report(const std::string& title, const Metrics& m,
                               const std::vector<std::pair<TimestampNs, double>>& equity);

}
