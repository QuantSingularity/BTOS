#include "btos/exec/matching_engine.hpp"

#include <cmath>
#include <stdexcept>

using namespace std;

namespace btos {

namespace {
constexpr double kQtyEps = 1e-9;
}

BarExchangeSim::BarExchangeSim(Kernel& kernel, const InstrumentMaster& instruments,
                               std::unique_ptr<ILatencyModel> latency,
                               std::unique_ptr<ISlippageModel> slippage, double participation_cap)
    : kernel_(kernel),
      latency_(std::move(latency)),
      slippage_(std::move(slippage)),
      participation_cap_(participation_cap) {
    (void)instruments;
}

void BarExchangeSim::submit(const Order& order) {
    Order o = order;
    o.submitted = kernel_.now();
    o.state = OrderState::PendingNew;
    orders_[o.id] = o;
    const TimestampNs arrival = kernel_.now() + latency_->next();
    kernel_.schedule(arrival, EventPriority::OrderAck, OrderArrivalEvent{o.id});
}

void BarExchangeSim::cancel(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;
    Order& o = it->second;
    const TimestampNs when = kernel_.now() + latency_->next();
    const bool live = o.state == OrderState::PendingNew || o.state == OrderState::Accepted ||
                      o.state == OrderState::PartiallyFilled;
    if (live) o.state = OrderState::Cancelled;
    kernel_.schedule(when, EventPriority::OrderAck, CancelAckEvent{id, live});
}

const Order& BarExchangeSim::order(OrderId id) const {
    auto it = orders_.find(id);
    if (it == orders_.end()) throw std::out_of_range("exchange: unknown order id");
    return it->second;
}

void BarExchangeSim::on_event(const Event& ev) {
    if (const auto* arr = std::get_if<OrderArrivalEvent>(&ev.payload)) {
        on_arrival(arr->order);
    } else if (const auto* bar = std::get_if<BarEvent>(&ev.payload)) {
        on_bar(*bar, ev.ts);
    } else if (const auto* sess = std::get_if<SessionEvent>(&ev.payload)) {
        if (sess->kind == SessionEvent::Kind::Halt ||
            sess->kind == SessionEvent::Kind::CircuitBreaker)
            halted_[sess->instrument.value] = true;
        else if (sess->kind == SessionEvent::Kind::Resume ||
                 sess->kind == SessionEvent::Kind::Open)
            halted_[sess->instrument.value] = false;
    }
}

void BarExchangeSim::on_arrival(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;
    Order& o = it->second;
    if (o.state == OrderState::Cancelled) return;
    std::string reason;
    bool accepted = true;
    if (o.quantity <= kQtyEps) {
        accepted = false;
        reason = "non-positive quantity";
    } else if (halted_[o.instrument.value]) {
        accepted = false;
        reason = "instrument halted";
    } else if ((o.type == OrderType::Limit || o.type == OrderType::StopLimit) &&
               o.limit_price <= 0) {
        accepted = false;
        reason = "invalid limit price";
    } else if ((o.type == OrderType::Stop || o.type == OrderType::StopLimit) &&
               o.stop_price <= 0) {
        accepted = false;
        reason = "invalid stop price";
    }
    o.state = accepted ? OrderState::Accepted : OrderState::Rejected;
    const TimestampNs ack_ts = kernel_.now() + latency_->next();
    kernel_.schedule(ack_ts, EventPriority::OrderAck, OrderAckEvent{id, accepted, reason});
    if (accepted) working_.push_back(id);
}

void BarExchangeSim::on_bar(const BarEvent& bar, TimestampNs ts) {
    last_bar_[bar.instrument.value] = bar;
    if (halted_[bar.instrument.value]) return;
    double volume_left = participation_cap_ * bar.volume;
    for (OrderId id : working_) {
        auto it = orders_.find(id);
        if (it == orders_.end()) continue;
        Order& o = it->second;
        if (o.instrument != bar.instrument) continue;
        if (o.state != OrderState::Accepted && o.state != OrderState::PartiallyFilled) continue;
        try_fill_against_bar(o, bar, ts, volume_left);
        if (volume_left <= kQtyEps) break;
    }
    std::erase_if(working_, [this](OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return true;
        const OrderState s = it->second.state;
        return s == OrderState::Filled || s == OrderState::Cancelled ||
               s == OrderState::Rejected;
    });
}

void BarExchangeSim::try_fill_against_bar(Order& o, const BarEvent& bar, TimestampNs ts,
                                          double& volume_left) {
    if (volume_left <= kQtyEps) return;
    bool triggered = true;
    if (o.type == OrderType::Stop || o.type == OrderType::StopLimit) {
        triggered = o.side == Side::Buy ? bar.high >= o.stop_price : bar.low <= o.stop_price;
        if (!triggered) return;
    }
    double px = 0;
    bool fillable = false;
    if (o.type == OrderType::Market || o.type == OrderType::Stop) {
        px = o.type == OrderType::Stop ? std::max(bar.open, o.stop_price) : bar.open;
        if (o.side == Side::Sell && o.type == OrderType::Stop)
            px = std::min(bar.open, o.stop_price);
        fillable = true;
    } else {
        const double limit = o.limit_price;
        if (o.side == Side::Buy && bar.low < limit) {
            px = std::min(bar.open, limit);
            fillable = true;
        } else if (o.side == Side::Sell && bar.high > limit) {
            px = std::max(bar.open, limit);
            fillable = true;
        }
    }
    if (!fillable) {
        if (o.tif == TimeInForce::IOC || o.tif == TimeInForce::FOK) {
            o.state = OrderState::Cancelled;
            kernel_.schedule(ts + latency_->next(), EventPriority::OrderAck,
                             CancelAckEvent{o.id, true});
        }
        return;
    }
    double qty = std::min(o.remaining(), volume_left);
    if (o.tif == TimeInForce::FOK && qty + kQtyEps < o.remaining()) {
        o.state = OrderState::Cancelled;
        kernel_.schedule(ts + latency_->next(), EventPriority::OrderAck,
                         CancelAckEvent{o.id, true});
        return;
    }
    if (qty <= kQtyEps) return;
    if (o.type == OrderType::Market || o.type == OrderType::Stop) {
        const double sigma = std::fabs(std::log(bar.high / std::max(1e-12, bar.low)));
        SlippageContext sc{px, qty, o.side, bar.volume, sigma};
        px = slippage_->apply(sc);
        if (o.type == OrderType::Limit || o.type == OrderType::StopLimit) {
            if (o.side == Side::Buy)
                px = std::min(px, o.limit_price);
            else
                px = std::max(px, o.limit_price);
        }
    }
    volume_left -= qty;
    emit_fill(o, qty, px, ts);
    if (o.tif == TimeInForce::IOC && o.remaining() > kQtyEps) {
        o.state = OrderState::Cancelled;
        kernel_.schedule(ts + latency_->next(), EventPriority::OrderAck,
                         CancelAckEvent{o.id, true});
    }
}

void BarExchangeSim::emit_fill(Order& o, double qty, double px, TimestampNs ts) {
    o.filled += qty;
    o.state = o.remaining() <= kQtyEps ? OrderState::Filled : OrderState::PartiallyFilled;
    FillEvent f;
    f.fill = FillId{next_fill_++};
    f.order = o.id;
    f.instrument = o.instrument;
    f.side = o.side;
    f.quantity = qty;
    f.price = px;
    f.commission = 0;
    kernel_.schedule(ts + latency_->next(), EventPriority::Fill, f);
}

BookExchangeSim::BookExchangeSim(Kernel& kernel, const InstrumentMaster& instruments,
                                 std::unique_ptr<ILatencyModel> latency)
    : kernel_(kernel), instruments_(instruments), latency_(std::move(latency)) {}

OrderBook& BookExchangeSim::book(InstrumentId id) {
    auto it = books_.find(id.value);
    if (it == books_.end()) {
        const Instrument& inst = instruments_.get(id);
        it = books_.emplace(id.value, OrderBook{inst.tick_size}).first;
    }
    return it->second;
}

void BookExchangeSim::submit(const Order& order) {
    Order o = order;
    o.submitted = kernel_.now();
    o.state = OrderState::PendingNew;
    orders_[o.id] = o;
    kernel_.schedule(kernel_.now() + latency_->next(), EventPriority::OrderAck,
                     OrderArrivalEvent{o.id});
}

void BookExchangeSim::cancel(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;
    Order& o = it->second;
    const bool live = o.state == OrderState::Accepted || o.state == OrderState::PartiallyFilled ||
                      o.state == OrderState::PendingNew;
    bool removed = false;
    if (live) {
        removed = book(o.instrument).cancel(id).has_value() || o.state == OrderState::PendingNew;
        o.state = OrderState::Cancelled;
        queue_.remove(id);
    }
    kernel_.schedule(kernel_.now() + latency_->next(), EventPriority::OrderAck,
                     CancelAckEvent{id, removed});
}

const Order& BookExchangeSim::order(OrderId id) const {
    auto it = orders_.find(id);
    if (it == orders_.end()) throw std::out_of_range("exchange: unknown order id");
    return it->second;
}

void BookExchangeSim::on_event(const Event& ev) {
    if (const auto* arr = std::get_if<OrderArrivalEvent>(&ev.payload))
        on_arrival(arr->order);
    else if (const auto* tick = std::get_if<TickEvent>(&ev.payload))
        on_tick(*tick, ev.ts);
}

void BookExchangeSim::on_arrival(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;
    Order& o = it->second;
    if (o.state == OrderState::Cancelled) return;
    if (o.type != OrderType::Limit) {
        o.state = OrderState::Rejected;
        kernel_.schedule(kernel_.now() + latency_->next(), EventPriority::OrderAck,
                         OrderAckEvent{id, false, "book sim accepts limit orders only"});
        return;
    }
    OrderBook& b = book(o.instrument);
    const PriceTicks limit = to_ticks(o.limit_price, b.tick_size());
    if (o.tif == TimeInForce::FOK && !b.can_fill_fully(o.side, limit, o.quantity)) {
        o.state = OrderState::Cancelled;
        kernel_.schedule(kernel_.now() + latency_->next(), EventPriority::OrderAck,
                         OrderAckEvent{id, false, "FOK not fully fillable"});
        return;
    }
    o.state = OrderState::Accepted;
    kernel_.schedule(kernel_.now() + latency_->next(), EventPriority::OrderAck,
                     OrderAckEvent{id, true, ""});
    const auto fills = b.submit_limit(id, o.side, limit, o.quantity, o.tif == TimeInForce::IOC);
    for (const auto& bf : fills) {
        emit_fill(o, bf.quantity, from_ticks(bf.price, b.tick_size()), kernel_.now());
        auto mit = orders_.find(bf.maker);
        if (mit != orders_.end())
            emit_fill(mit->second, bf.quantity, from_ticks(bf.price, b.tick_size()), kernel_.now());
    }
    if (o.remaining() > kQtyEps) {
        if (o.tif == TimeInForce::IOC) {
            o.state = OrderState::Cancelled;
            kernel_.schedule(kernel_.now() + latency_->next(),
                             EventPriority::OrderAck, CancelAckEvent{id, true});
        } else {
            const double ahead = b.depth_at(o.side, limit) - o.remaining();
            queue_.rest(id, std::max(0.0, ahead));
        }
    }
}

void BookExchangeSim::on_tick(const TickEvent& tick, TimestampNs ts) {
    OrderBook& b = book(tick.instrument);
    const PriceTicks px = to_ticks(tick.price, b.tick_size());
    std::vector<OrderId> to_fill;
    for (auto& [id, o] : orders_) {
        if (o.instrument != tick.instrument) continue;
        if (o.state != OrderState::Accepted && o.state != OrderState::PartiallyFilled) continue;
        const PriceTicks limit = to_ticks(o.limit_price, b.tick_size());
        const bool crossed = o.side == Side::Buy ? px < limit : px > limit;
        const bool at_price = px == limit;
        if (crossed) {
            to_fill.push_back(id);
        } else if (at_price) {
            const double overflow = queue_.consume(id, tick.size);
            if (overflow > kQtyEps) to_fill.push_back(id);
        }
    }
    for (OrderId id : to_fill) {
        Order& o = orders_.at(id);
        const double qty = std::min(o.remaining(), tick.size);
        if (qty <= kQtyEps) continue;
        b.cancel(id);
        queue_.remove(id);
        emit_fill(o, qty, o.limit_price, ts);
        if (o.remaining() > kQtyEps) {
            const PriceTicks limit = to_ticks(o.limit_price, b.tick_size());
            b.submit_limit(id, o.side, limit, o.remaining());
            queue_.rest(id, 0.0);
        }
    }
}

void BookExchangeSim::emit_fill(Order& o, double qty, double px, TimestampNs ts) {
    o.filled += qty;
    o.state = o.remaining() <= kQtyEps ? OrderState::Filled : OrderState::PartiallyFilled;
    FillEvent f;
    f.fill = FillId{next_fill_++};
    f.order = o.id;
    f.instrument = o.instrument;
    f.side = o.side;
    f.quantity = qty;
    f.price = px;
    f.commission = 0;
    kernel_.schedule(ts + latency_->next(), EventPriority::Fill, f);
}

}
