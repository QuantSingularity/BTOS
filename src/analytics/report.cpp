#include <algorithm>
#include <cmath>
#include <sstream>

#include "btos/analytics/metrics.hpp"

using namespace std;

namespace btos {
namespace {

std::string svg_line_chart(const std::vector<std::pair<TimestampNs, double>>& series,
                           const std::string& stroke, int width, int height, bool fill_under) {
    std::ostringstream os;
    os << "<svg viewBox=\"0 0 " << width << " " << height
       << "\" xmlns=\"http://www.w3.org/2000/svg\" preserveAspectRatio=\"none\" "
          "style=\"width:100%;height:"
       << height << "px;background:#fafafa;border:1px solid #e0e0e0\">";
    if (series.size() >= 2) {
        double lo = series.front().second, hi = lo;
        for (const auto& [ts, v] : series) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (hi - lo < 1e-12) hi = lo + 1.0;
        const double t0 = static_cast<double>(series.front().first.ns);
        const double t1 = static_cast<double>(series.back().first.ns);
        const double tspan = std::max(1.0, t1 - t0);
        std::ostringstream pts;
        for (const auto& [ts, v] : series) {
            const double x = (static_cast<double>(ts.ns) - t0) / tspan * width;
            const double y = height - (v - lo) / (hi - lo) * (height - 8) - 4;
            pts << x << "," << y << " ";
        }
        if (fill_under) {
            os << "<polygon points=\"0," << height << " " << pts.str() << width << "," << height
               << "\" fill=\"" << stroke << "\" opacity=\"0.15\"/>";
        }
        os << "<polyline points=\"" << pts.str() << "\" fill=\"none\" stroke=\"" << stroke
           << "\" stroke-width=\"1.5\"/>";
    }
    os << "</svg>";
    return os.str();
}

std::string pct(double v) {
    std::ostringstream os;
    os.precision(2);
    os << std::fixed << v * 100.0 << "%";
    return os.str();
}

std::string num(double v) {
    std::ostringstream os;
    os.precision(3);
    os << std::fixed << v;
    return os.str();
}

}

std::string render_html_report(const std::string& title, const Metrics& m,
                               const std::vector<std::pair<TimestampNs, double>>& equity) {
    std::ostringstream os;
    os << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>" << title
       << "</title><style>"
          "body{font-family:Georgia,serif;max-width:960px;margin:2rem auto;color:#222;"
          "padding:0 1rem}h1{font-size:1.6rem;border-bottom:2px solid #333;padding-bottom:.4rem}"
          "h2{font-size:1.15rem;margin-top:2rem}table{border-collapse:collapse;width:100%;"
          "font-size:.92rem}td,th{border:1px solid #ddd;padding:.4rem .6rem;text-align:right}"
          "th{background:#f4f4f4}td:first-child,th:first-child{text-align:left}"
          ".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:.6rem;margin:1rem 0}"
          ".card{border:1px solid #ddd;padding:.6rem;text-align:center}"
          ".card .v{font-size:1.25rem;font-weight:bold}.card .k{font-size:.78rem;color:#666}"
          "</style></head><body><h1>"
       << title << "</h1>";

    os << "<div class=\"grid\">";
    const std::pair<std::string, std::string> cards[] = {
        {"Total return", pct(m.total_return)},   {"CAGR", pct(m.cagr)},
        {"Sharpe", num(m.sharpe)},               {"Sortino", num(m.sortino)},
        {"Volatility", pct(m.volatility_annual)},{"Max drawdown", pct(m.max_drawdown)},
        {"Turnover", num(m.turnover_annual)},    {"Win rate", pct(m.win_rate)},
    };
    for (const auto& [k, v] : cards)
        os << "<div class=\"card\"><div class=\"v\">" << v << "</div><div class=\"k\">" << k
           << "</div></div>";
    os << "</div>";

    os << "<h2>Equity curve</h2>" << svg_line_chart(equity, "#1a5276", 900, 240, true);
    os << "<h2>Underwater (drawdown)</h2>" << svg_line_chart(m.underwater, "#922b21", 900, 140, true);

    os << "<h2>Monthly returns</h2><table><tr><th>Month</th><th>Return</th></tr>";
    for (const auto& [key, r] : m.returns_by_month)
        os << "<tr><td>" << key << "</td><td>" << pct(r) << "</td></tr>";
    os << "</table>";

    os << "<h2>Worst drawdowns</h2><table><tr><th>Peak</th><th>Trough</th><th>Depth</th></tr>";
    std::size_t shown = 0;
    for (const auto& d : m.drawdowns) {
        if (++shown > 5) break;
        os << "<tr><td>" << format_iso8601_utc(d.peak_ts).substr(0, 10) << "</td><td>"
           << format_iso8601_utc(d.trough_ts).substr(0, 10) << "</td><td>" << pct(d.depth)
           << "</td></tr>";
    }
    os << "</table>";

    os << "<h2>Trade statistics</h2><table><tr><th>Metric</th><th>Value</th></tr>"
       << "<tr><td>Round trips</td><td>" << m.n_trades << "</td></tr>"
       << "<tr><td>Win rate</td><td>" << pct(m.win_rate) << "</td></tr>"
       << "<tr><td>Mean exposure / equity</td><td>" << num(m.exposure_mean) << "</td></tr>"
       << "<tr><td>Beta</td><td>" << num(m.beta) << "</td></tr>"
       << "<tr><td>Annualized alpha</td><td>" << pct(m.alpha_annual) << "</td></tr>"
       << "<tr><td>Information ratio</td><td>" << num(m.information_ratio) << "</td></tr>"
       << "</table></body></html>";
    return os.str();
}

}
