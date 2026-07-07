#include "btos/portfolio/portfolio.hpp"

#include <sstream>

using namespace std;

namespace btos {

namespace {
constexpr double kQtyEps = 1e-9;
}

void Portfolio::apply_fill(const FillEvent& f, TimestampNs ts) {
    const Instrument& inst = instruments_->get(f.instrument);
    const double fx = fx_->to_base(inst.currency, ts);
    Position& pos = positions_[f.instrument.value];
    const double signed_qty = f.quantity * sign(f.side);
    const double notional_local = f.quantity * f.price * inst.multiplier;

    cash_[inst.currency] -= signed_qty * f.price * inst.multiplier;
    cash_[cfg_.base_currency] -= f.commission;
    commissions_base_ += f.commission;
    (void)notional_local;

    double remaining = f.quantity;
    if (pos.quantity * signed_qty < 0) {
        while (remaining > kQtyEps && !pos.lots.empty()) {
            TaxLot& lot = pos.lots.front();
            const double lot_abs = std::fabs(lot.quantity);
            const double closed = std::min(remaining, lot_abs);
            const double lot_sign = lot.quantity > 0 ? 1.0 : -1.0;
            const double pnl_local = (f.price - lot.price) * closed * lot_sign * inst.multiplier;
            const double pnl_base = pnl_local * fx;
            pos.realized_pnl += pnl_base;
            realized_base_ += pnl_base;
            realized_local_[inst.currency] += pnl_local;
            lot.quantity -= closed * lot_sign;
            remaining -= closed;
            if (std::fabs(lot.quantity) <= kQtyEps) pos.lots.pop_front();
        }
    }
    if (remaining > kQtyEps)
        pos.lots.push_back(TaxLot{ts, remaining * sign(f.side), f.price});

    pos.quantity += signed_qty;
    if (std::fabs(pos.quantity) <= kQtyEps) {
        pos.quantity = 0;
        pos.lots.clear();
        pos.avg_cost = 0;
    } else {
        double q = 0, c = 0;
        for (const auto& lot : pos.lots) {
            q += lot.quantity;
            c += lot.quantity * lot.price;
        }
        pos.avg_cost = std::fabs(q) > kQtyEps ? c / q : 0;
    }
    marks_[f.instrument.value] = f.price;
}

void Portfolio::apply_corporate_action(const CorporateActionEvent& ca, TimestampNs ts) {
    Position& pos = positions_[ca.instrument.value];
    const Instrument& inst = instruments_->get(ca.instrument);
    if (ca.kind == CorporateActionEvent::Kind::Split && ca.ratio > 0) {
        pos.quantity *= ca.ratio;
        pos.avg_cost /= ca.ratio;
        for (auto& lot : pos.lots) {
            lot.quantity *= ca.ratio;
            lot.price /= ca.ratio;
        }
        auto it = marks_.find(ca.instrument.value);
        if (it != marks_.end()) it->second /= ca.ratio;
    } else if (ca.kind == CorporateActionEvent::Kind::CashDividend && ca.amount != 0) {
        const double fx = fx_->to_base(inst.currency, ts);
        const double amt_local = pos.quantity * ca.amount * inst.multiplier;
        cash_[inst.currency] += amt_local;
        dividends_base_ += amt_local * fx;
        dividends_local_[inst.currency] += amt_local;
    }
}

void Portfolio::accrue_financing(DurationNs dt) {
    if (cfg_.borrow_rate_annual == 0 && cfg_.financing_rate_annual == 0) return;
    const double years = static_cast<double>(dt.ns) / (365.25 * 86400.0 * 1e9);
    double& base_cash = cash_[cfg_.base_currency];
    if (base_cash < 0) {
        const double cost = -base_cash * cfg_.borrow_rate_annual * years;
        base_cash -= cost;
        financing_base_ += cost;
    } else if (cfg_.financing_rate_annual != 0) {
        const double earn = base_cash * cfg_.financing_rate_annual * years;
        base_cash += earn;
        financing_base_ -= earn;
    }
}

const Position& Portfolio::position(InstrumentId id) const {
    static const Position kEmpty{};
    auto it = positions_.find(id.value);
    return it == positions_.end() ? kEmpty : it->second;
}

double Portfolio::equity(TimestampNs t) const {
    double eq = 0;
    for (const auto& [ccy, amt] : cash_) eq += amt * fx_->to_base(ccy, t);
    for (const auto& [iid, pos] : positions_) {
        if (std::fabs(pos.quantity) <= kQtyEps) continue;
        const Instrument& inst = instruments_->get(InstrumentId{iid});
        auto mit = marks_.find(iid);
        if (mit == marks_.end()) continue;
        eq += pos.quantity * mit->second * inst.multiplier * fx_->to_base(inst.currency, t);
    }
    return eq;
}

double Portfolio::gross_exposure(TimestampNs t) const {
    double g = 0;
    for (const auto& [iid, pos] : positions_) {
        if (std::fabs(pos.quantity) <= kQtyEps) continue;
        const Instrument& inst = instruments_->get(InstrumentId{iid});
        auto mit = marks_.find(iid);
        if (mit == marks_.end()) continue;
        g += std::fabs(pos.quantity * mit->second * inst.multiplier) *
             fx_->to_base(inst.currency, t);
    }
    return g;
}

double Portfolio::unrealized_pnl(TimestampNs t) const {
    double u = 0;
    for (const auto& [iid, pos] : positions_) {
        if (std::fabs(pos.quantity) <= kQtyEps) continue;
        const Instrument& inst = instruments_->get(InstrumentId{iid});
        auto mit = marks_.find(iid);
        if (mit == marks_.end()) continue;
        const double fx = fx_->to_base(inst.currency, t);
        for (const auto& lot : pos.lots)
            u += lot.quantity * (mit->second - lot.price) * inst.multiplier * fx;
    }
    return u;
}

void Portfolio::check_identity(TimestampNs t, double eps) const {
    const double eq = equity(t);
    double realized_now = 0, dividends_now = 0;
    for (const auto& [ccy, amt] : realized_local_) realized_now += amt * fx_->to_base(ccy, t);
    for (const auto& [ccy, amt] : dividends_local_) dividends_now += amt * fx_->to_base(ccy, t);
    const double expected = cfg_.initial_capital + realized_now + unrealized_pnl(t) -
                            commissions_base_ - financing_base_ + dividends_now;
    const double scale = std::max(1.0, std::fabs(eq));
    if (std::fabs(eq - expected) > eps * scale) {
        std::ostringstream os;
        os << "accounting identity violated: equity=" << eq << " expected=" << expected
           << " diff=" << eq - expected;
        throw std::logic_error(os.str());
    }
}

std::string Portfolio::serialize_state() const {
    std::ostringstream os;
    os.precision(12);
    os << "cash:";
    for (const auto& [ccy, amt] : cash_) os << ccy << '=' << amt << ';';
    os << "positions:";
    for (const auto& [iid, pos] : positions_) {
        if (std::fabs(pos.quantity) <= kQtyEps) continue;
        os << iid << "=qty:" << pos.quantity << ",avg:" << pos.avg_cost
           << ",real:" << pos.realized_pnl << ";";
    }
    os << "realized:" << realized_base_ << ";commissions:" << commissions_base_
       << ";financing:" << financing_base_ << ";dividends:" << dividends_base_;
    return os.str();
}

}
