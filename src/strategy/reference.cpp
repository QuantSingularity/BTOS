#include "btos/strategy/reference.hpp"

#include <cmath>
#include <numeric>
#include <sstream>

using namespace std;

namespace btos {

MaCrossoverStrategy::MaCrossoverStrategy(std::string symbol) : symbol_(std::move(symbol)) {}

void MaCrossoverStrategy::on_start(StrategyContext& ctx) { inst_ = ctx.instrument(symbol_); }

void MaCrossoverStrategy::set_param(const std::string& name, double value) {
    if (name == "fast") fast_ = static_cast<std::size_t>(value);
    else if (name == "slow") slow_ = static_cast<std::size_t>(value);
    else if (name == "quantity") quantity_ = value;
}

std::string MaCrossoverStrategy::save_state() const {
    std::ostringstream os;
    os << "{\"position_state\":" << position_state_ << ",\"closes\":[";
    for (std::size_t i = 0; i < closes_.size(); ++i)
        os << (i ? "," : "") << closes_[i];
    os << "]}";
    return os.str();
}

void MaCrossoverStrategy::load_state(const std::string& json) {
    closes_.clear();
    auto p = json.find("\"position_state\":");
    if (p != std::string::npos) position_state_ = std::stoi(json.substr(p + 17));
    auto lb = json.find('[');
    auto rb = json.find(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb + 1) {
        std::stringstream ss(json.substr(lb + 1, rb - lb - 1));
        std::string tok;
        while (std::getline(ss, tok, ',')) closes_.push_back(std::stod(tok));
    }
}

void MaCrossoverStrategy::on_bar(StrategyContext& ctx, const BarEvent& bar) {
    if (bar.instrument != inst_) return;
    closes_.push_back(bar.close);
    if (closes_.size() > slow_ + 1) closes_.pop_front();
    if (closes_.size() < slow_) return;
    const auto mean_last = [this](std::size_t k) {
        double s = 0;
        for (std::size_t i = closes_.size() - k; i < closes_.size(); ++i) s += closes_[i];
        return s / static_cast<double>(k);
    };
    const double fast_ma = mean_last(fast_);
    const double slow_ma = mean_last(slow_);
    if (fast_ma > slow_ma && position_state_ <= 0) {
        const double qty = quantity_ + (position_state_ < 0 ? quantity_ : 0);
        ctx.submit_order(inst_, Side::Buy, OrderType::Market, qty);
        position_state_ = 1;
    } else if (fast_ma < slow_ma && position_state_ > 0) {
        ctx.submit_order(inst_, Side::Sell, OrderType::Market, quantity_);
        position_state_ = 0;
    }
}

PairsStrategy::PairsStrategy(std::string symbol_y, std::string symbol_x)
    : sym_y_(std::move(symbol_y)), sym_x_(std::move(symbol_x)) {}

void PairsStrategy::on_start(StrategyContext& ctx) {
    inst_y_ = ctx.instrument(sym_y_);
    inst_x_ = ctx.instrument(sym_x_);
}

void PairsStrategy::set_param(const std::string& name, double value) {
    if (name == "window") window_ = static_cast<std::size_t>(value);
    else if (name == "entry_z") entry_z_ = value;
    else if (name == "exit_z") exit_z_ = value;
    else if (name == "quantity") quantity_ = value;
}

std::string PairsStrategy::save_state() const {
    std::ostringstream os;
    os << "{\"state\":" << state_ << ",\"beta\":" << beta_ << "}";
    return os.str();
}

void PairsStrategy::load_state(const std::string& json) {
    auto p = json.find("\"state\":");
    if (p != std::string::npos) state_ = std::stoi(json.substr(p + 8));
    p = json.find("\"beta\":");
    if (p != std::string::npos) beta_ = std::stod(json.substr(p + 7));
}

void PairsStrategy::on_bar(StrategyContext& ctx, const BarEvent& bar) {
    if (bar.instrument == inst_y_) seen_y_ = true;
    else if (bar.instrument == inst_x_) seen_x_ = true;
    else return;
    const TimestampNs now = ctx.now();
    if (last_bar_ts_ != now) {
        last_bar_ts_ = now;
        seen_y_ = bar.instrument == inst_y_;
        seen_x_ = bar.instrument == inst_x_;
        return;
    }
    if (seen_y_ && seen_x_) {
        seen_y_ = seen_x_ = false;
        evaluate(ctx);
    }
}

void PairsStrategy::evaluate(StrategyContext& ctx) {
    const auto& by = ctx.bars(inst_y_);
    const auto& bx = ctx.bars(inst_x_);
    const std::size_t ny = by.size();
    const std::size_t nx = bx.size();
    const std::size_t n = std::min({ny, nx, window_});
    if (n < window_) return;
    std::vector<double> y(n), x(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::log(by.at(ny - n + i).close);
        x[i] = std::log(bx.at(nx - n + i).close);
    }
    const double mx = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    const double my = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(n);
    double sxx = 0, sxy = 0;
    for (std::size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    if (sxx <= 1e-12) return;
    beta_ = sxy / sxx;
    std::vector<double> spread(n);
    for (std::size_t i = 0; i < n; ++i) spread[i] = y[i] - beta_ * x[i];
    const double ms = std::accumulate(spread.begin(), spread.end(), 0.0) / static_cast<double>(n);
    double vs = 0;
    for (double s : spread) vs += (s - ms) * (s - ms);
    vs /= static_cast<double>(n - 1);
    const double sd = std::sqrt(vs);
    if (sd <= 1e-12) return;
    const double z = (spread.back() - ms) / sd;
    const double qty_y = quantity_;
    const double qty_x = std::fabs(beta_) * quantity_;
    if (state_ == 0) {
        if (z > entry_z_) {
            ctx.submit_order(inst_y_, Side::Sell, OrderType::Market, qty_y);
            ctx.submit_order(inst_x_, beta_ > 0 ? Side::Buy : Side::Sell, OrderType::Market, qty_x);
            state_ = -1;
        } else if (z < -entry_z_) {
            ctx.submit_order(inst_y_, Side::Buy, OrderType::Market, qty_y);
            ctx.submit_order(inst_x_, beta_ > 0 ? Side::Sell : Side::Buy, OrderType::Market, qty_x);
            state_ = 1;
        }
    } else if (std::fabs(z) < exit_z_) {
        const auto& py = ctx.portfolio().position(inst_y_);
        const auto& px = ctx.portfolio().position(inst_x_);
        if (std::fabs(py.quantity) > 1e-9)
            ctx.submit_order(inst_y_, py.quantity > 0 ? Side::Sell : Side::Buy, OrderType::Market,
                             std::fabs(py.quantity));
        if (std::fabs(px.quantity) > 1e-9)
            ctx.submit_order(inst_x_, px.quantity > 0 ? Side::Sell : Side::Buy, OrderType::Market,
                             std::fabs(px.quantity));
        state_ = 0;
    }
}

NaiveMarketMaker::NaiveMarketMaker(std::string symbol) : symbol_(std::move(symbol)) {}

void NaiveMarketMaker::on_start(StrategyContext& ctx) { inst_ = ctx.instrument(symbol_); }

void NaiveMarketMaker::set_param(const std::string& name, double value) {
    if (name == "spread_bps") spread_bps_ = value;
    else if (name == "quote_size") quote_size_ = value;
    else if (name == "max_inventory") max_inventory_ = value;
}

void NaiveMarketMaker::on_bar(StrategyContext& ctx, const BarEvent& bar) {
    if (bar.instrument != inst_) return;
    for (OrderId id : live_quotes_) ctx.cancel_order(id);
    live_quotes_.clear();
    const double inv = ctx.portfolio().position(inst_).quantity;
    const double half = bar.close * spread_bps_ * 1e-4 * 0.5;
    const double skew = half * (inv / std::max(1.0, max_inventory_));
    const double bid = bar.close - half - skew;
    const double ask = bar.close + half - skew;
    if (inv < max_inventory_)
        live_quotes_.push_back(
            ctx.submit_order(inst_, Side::Buy, OrderType::Limit, quote_size_, bid));
    if (inv > -max_inventory_)
        live_quotes_.push_back(
            ctx.submit_order(inst_, Side::Sell, OrderType::Limit, quote_size_, ask));
}

void VectorizedSignalStrategy::on_start(StrategyContext& ctx) { inst_ = ctx.instrument(symbol_); }

void VectorizedSignalStrategy::on_bar(StrategyContext& ctx, const BarEvent& bar) {
    if (bar.instrument != inst_) return;
    closes_.push_back(bar.close);
    const double target = signal_(closes_);
    const double current = ctx.portfolio().position(inst_).quantity;
    const double delta = target - current;
    if (std::fabs(delta) < 1e-9) return;
    ctx.submit_order(inst_, delta > 0 ? Side::Buy : Side::Sell, OrderType::Market,
                     std::fabs(delta));
}

}
