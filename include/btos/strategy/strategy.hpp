#pragma once

#include <cstdint>
#include <string>

#include "btos/core/events.hpp"
#include "btos/data/readers.hpp"
#include "btos/exec/order.hpp"
#include "btos/portfolio/portfolio.hpp"

using namespace std;

namespace btos {

class StrategyContext {
  public:
    virtual ~StrategyContext() = default;

    [[nodiscard]] virtual TimestampNs now() const = 0;

    virtual OrderId submit_order(InstrumentId instrument, Side side, OrderType type, double qty,
                                 double limit_price = 0, double stop_price = 0,
                                 TimeInForce tif = TimeInForce::Day) = 0;

    virtual void cancel_order(OrderId id) = 0;

    virtual void schedule_timer(TimestampNs when, std::uint64_t token) = 0;

    [[nodiscard]] virtual const PointInTimeBars& bars(InstrumentId id) const = 0;

    [[nodiscard]] virtual const Portfolio& portfolio() const = 0;

    [[nodiscard]] virtual InstrumentId instrument(const std::string& symbol) const = 0;
};

class IStrategy {
  public:
    virtual ~IStrategy() = default;

    virtual void on_start(StrategyContext& ctx) { (void)ctx; }

    virtual void on_bar(StrategyContext& ctx, const BarEvent& bar) { (void)ctx; (void)bar; }

    virtual void on_tick(StrategyContext& ctx, const TickEvent& tick) { (void)ctx; (void)tick; }

    virtual void on_fill(StrategyContext& ctx, const FillEvent& fill) { (void)ctx; (void)fill; }

    virtual void on_timer(StrategyContext& ctx, std::uint64_t token) { (void)ctx; (void)token; }

    virtual void on_stop(StrategyContext& ctx) { (void)ctx; }

    virtual void set_param(const std::string& name, double value) { (void)name; (void)value; }

    [[nodiscard]] virtual std::string save_state() const { return "{}"; }

    virtual void load_state(const std::string& json) { (void)json; }

    [[nodiscard]] virtual std::string name() const = 0;
};

}
