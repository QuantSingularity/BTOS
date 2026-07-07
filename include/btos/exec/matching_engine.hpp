#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "btos/core/kernel.hpp"
#include "btos/data/instrument_master.hpp"
#include "btos/exec/latency.hpp"
#include "btos/exec/order.hpp"
#include "btos/exec/order_book.hpp"
#include "btos/exec/slippage.hpp"

using namespace std;

namespace btos {

class IExchangeSim {
  public:
    virtual ~IExchangeSim() = default;

    virtual void submit(const Order& order) = 0;

    virtual void cancel(OrderId id) = 0;

    [[nodiscard]] virtual const Order& order(OrderId id) const = 0;
};

class IAlgoOrder {
  public:
    virtual ~IAlgoOrder() = default;

    virtual void on_activate(TimestampNs now) = 0;

    virtual std::vector<Order> on_slice(TimestampNs now) = 0;

    virtual void on_child_fill(const FillEvent& fill) = 0;
};

class QueuePositionEstimator {
  public:

    void rest(OrderId id, double displayed_ahead) { ahead_[id] = displayed_ahead; }

    void remove(OrderId id) { ahead_.erase(id); }

    double consume(OrderId id, double traded) {
        auto it = ahead_.find(id);
        if (it == ahead_.end()) return 0.0;
        const double eat = std::min(it->second, traded);
        it->second -= eat;
        return traded - eat;
    }

  private:
    std::unordered_map<OrderId, double> ahead_;
};

class BarExchangeSim final : public IExchangeSim {
  public:

    BarExchangeSim(Kernel& kernel, const InstrumentMaster& instruments,
                   std::unique_ptr<ILatencyModel> latency, std::unique_ptr<ISlippageModel> slippage,
                   double participation_cap = 0.1);

    void submit(const Order& order) override;
    void cancel(OrderId id) override;
    [[nodiscard]] const Order& order(OrderId id) const override;

    void on_event(const Event& ev);

    [[nodiscard]] std::uint64_t fill_count() const { return next_fill_; }

  private:
    void on_arrival(OrderId id);
    void on_bar(const BarEvent& bar, TimestampNs ts);
    void try_fill_against_bar(Order& o, const BarEvent& bar, TimestampNs ts, double& volume_left);
    void emit_fill(Order& o, double qty, double px, TimestampNs ts);

    Kernel& kernel_;
    std::unique_ptr<ILatencyModel> latency_;
    std::unique_ptr<ISlippageModel> slippage_;
    double participation_cap_;
    std::unordered_map<OrderId, Order> orders_;
    std::vector<OrderId> working_;
    std::unordered_map<std::uint32_t, bool> halted_;
    std::unordered_map<std::uint32_t, BarEvent> last_bar_;
    std::uint64_t next_fill_{1};
};

class BookExchangeSim final : public IExchangeSim {
  public:
    BookExchangeSim(Kernel& kernel, const InstrumentMaster& instruments,
                    std::unique_ptr<ILatencyModel> latency);

    void submit(const Order& order) override;
    void cancel(OrderId id) override;
    [[nodiscard]] const Order& order(OrderId id) const override;

    void on_event(const Event& ev);

    OrderBook& book(InstrumentId id);

  private:
    void on_arrival(OrderId id);
    void on_tick(const TickEvent& tick, TimestampNs ts);
    void emit_fill(Order& o, double qty, double px, TimestampNs ts);

    Kernel& kernel_;
    const InstrumentMaster& instruments_;
    std::unique_ptr<ILatencyModel> latency_;
    std::unordered_map<OrderId, Order> orders_;
    std::unordered_map<std::uint32_t, OrderBook> books_;
    QueuePositionEstimator queue_;
    std::uint64_t next_fill_{1};
};

}
