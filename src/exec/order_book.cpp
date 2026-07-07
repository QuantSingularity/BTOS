#include "btos/exec/order_book.hpp"

#include <cmath>

using namespace std;

namespace btos {

namespace {
constexpr double kQtyEps = 1e-9;
}

double OrderBook::depth_at(Side side, PriceTicks px) const {
    double total = 0;
    if (side == Side::Buy) {
        auto it = bids_.find(px);
        if (it != bids_.end())
            for (const auto& o : it->second) total += o.remaining;
    } else {
        auto it = asks_.find(px);
        if (it != asks_.end())
            for (const auto& o : it->second) total += o.remaining;
    }
    return total;
}

template <typename BookSide>
std::vector<BookFill> OrderBook::match(OrderId id, double& qty, BookSide& opposite,
                                       std::function<bool(PriceTicks)> price_ok) {
    std::vector<BookFill> fills;
    while (qty > kQtyEps && !opposite.empty()) {
        auto level_it = opposite.begin();
        if (!price_ok(level_it->first)) break;
        Level& level = level_it->second;
        while (qty > kQtyEps && !level.empty()) {
            RestingOrder& maker = level.front();
            const double take = std::min(qty, maker.remaining);
            fills.push_back(BookFill{id, maker.id, level_it->first, take});
            qty -= take;
            maker.remaining -= take;
            if (maker.remaining <= kQtyEps) level.pop_front();
        }
        if (level.empty()) opposite.erase(level_it);
    }
    return fills;
}

void OrderBook::rest(Side side, PriceTicks px, OrderId id, double qty) {
    RestingOrder o{id, qty, entry_counter_++};
    if (side == Side::Buy)
        bids_[px].push_back(o);
    else
        asks_[px].push_back(o);
}

std::vector<BookFill> OrderBook::submit_limit(OrderId id, Side side, PriceTicks limit, double qty,
                                              bool ioc) {
    std::vector<BookFill> fills;
    if (qty <= kQtyEps) return fills;
    if (side == Side::Buy)
        fills = match(id, qty, asks_, [limit](PriceTicks px) { return px <= limit; });
    else
        fills = match(id, qty, bids_, [limit](PriceTicks px) { return px >= limit; });
    if (qty > kQtyEps && !ioc) rest(side, limit, id, qty);
    return fills;
}

std::vector<BookFill> OrderBook::submit_market(OrderId id, Side side, double qty,
                                               double* unfilled_out) {
    std::vector<BookFill> fills;
    if (qty > kQtyEps) {
        if (side == Side::Buy)
            fills = match(id, qty, asks_, [](PriceTicks) { return true; });
        else
            fills = match(id, qty, bids_, [](PriceTicks) { return true; });
    }
    if (unfilled_out != nullptr) *unfilled_out = qty > kQtyEps ? qty : 0.0;
    return fills;
}

bool OrderBook::can_fill_fully(Side side, PriceTicks limit, double qty) const {
    double avail = 0;
    if (side == Side::Buy) {
        for (const auto& [px, level] : asks_) {
            if (px > limit) break;
            for (const auto& o : level) avail += o.remaining;
            if (avail >= qty - kQtyEps) return true;
        }
    } else {
        for (const auto& [px, level] : bids_) {
            if (px < limit) break;
            for (const auto& o : level) avail += o.remaining;
            if (avail >= qty - kQtyEps) return true;
        }
    }
    return avail >= qty - kQtyEps;
}

std::optional<double> OrderBook::cancel(OrderId id) {
    for (auto* book_side : {static_cast<void*>(&bids_), static_cast<void*>(&asks_)}) {
        if (book_side == static_cast<void*>(&bids_)) {
            for (auto it = bids_.begin(); it != bids_.end(); ++it) {
                for (auto lit = it->second.begin(); lit != it->second.end(); ++lit) {
                    if (lit->id == id) {
                        const double rem = lit->remaining;
                        it->second.erase(lit);
                        if (it->second.empty()) bids_.erase(it);
                        return rem;
                    }
                }
            }
        } else {
            for (auto it = asks_.begin(); it != asks_.end(); ++it) {
                for (auto lit = it->second.begin(); lit != it->second.end(); ++lit) {
                    if (lit->id == id) {
                        const double rem = lit->remaining;
                        it->second.erase(lit);
                        if (it->second.empty()) asks_.erase(it);
                        return rem;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

double OrderBook::total_resting() const {
    double total = 0;
    for (const auto& [px, level] : bids_)
        for (const auto& o : level) total += o.remaining;
    for (const auto& [px, level] : asks_)
        for (const auto& o : level) total += o.remaining;
    return total;
}

}
