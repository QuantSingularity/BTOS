#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "btos/exec/order.hpp"

using namespace std;

namespace btos {

using PriceTicks = std::int64_t;

inline PriceTicks to_ticks(double price, double tick_size) {
    return static_cast<PriceTicks>(price / tick_size + (price >= 0 ? 0.5 : -0.5));
}

inline double from_ticks(PriceTicks t, double tick_size) { return static_cast<double>(t) * tick_size; }

struct RestingOrder {
    OrderId id;
    double remaining{0};
    std::uint64_t entry_seq{0};
};

struct BookFill {
    OrderId taker;
    OrderId maker;
    PriceTicks price{0};
    double quantity{0};
};

class OrderBook {
  public:

    explicit OrderBook(double tick_size) : tick_size_(tick_size) {}

    [[nodiscard]] std::optional<PriceTicks> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    [[nodiscard]] std::optional<PriceTicks> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    [[nodiscard]] double depth_at(Side side, PriceTicks px) const;

    std::vector<BookFill> submit_limit(OrderId id, Side side, PriceTicks limit, double qty,
                                       bool ioc = false);

    std::vector<BookFill> submit_market(OrderId id, Side side, double qty,
                                        double* unfilled_out = nullptr);

    [[nodiscard]] bool can_fill_fully(Side side, PriceTicks limit, double qty) const;

    std::optional<double> cancel(OrderId id);

    [[nodiscard]] double total_resting() const;

    [[nodiscard]] double tick_size() const { return tick_size_; }

  private:
    using Level = std::deque<RestingOrder>;
    template <typename BookSide>
    std::vector<BookFill> match(OrderId id, double& qty, BookSide& opposite,
                                std::function<bool(PriceTicks)> price_ok);
    void rest(Side side, PriceTicks px, OrderId id, double qty);

    double tick_size_;
    std::map<PriceTicks, Level, std::greater<>> bids_;
    std::map<PriceTicks, Level, std::less<>> asks_;
    std::uint64_t entry_counter_{0};
};

}
